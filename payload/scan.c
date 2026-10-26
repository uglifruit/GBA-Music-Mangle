// scan.c — MP2K song-table detection. See scan.h for the methods.

#include "scan.h"
#include "rom.h"
#include "m4a.h"
#include "gba.h"

ScanResult g_scan;
uint16_t   g_songIx[SCAN_MAX_SONGS];
uint16_t   g_songCount;

// saptapper's FindSelectSongFn pattern (src/saptapper/mp2k_driver.cpp), and its offsets.
static const uint8_t kSelectSong[30] = {
    0x00, 0xb5, 0x00, 0x04, 0x07, 0x4a, 0x08, 0x49, 0x40, 0x0b,
    0x40, 0x18, 0x83, 0x88, 0x59, 0x00, 0xc9, 0x18, 0x89, 0x00,
    0x89, 0x18, 0x0a, 0x68, 0x01, 0x68, 0x10, 0x1c, 0x00, 0xf0,
};
#define SELECT_SONG_MAX_DIFF  8
#define SELECT_SONG_TABLE_OFF 40

#define CHUNK 0x10000u      // progress / link-service granularity: 64 KB

// Progress is done/total in 1/256ths. total is twice the ROM size, a power of two, so the divide is
// a shift - a divide by a variable would be a LINK error here (-nostdlib), which is the point.
static uint32_t s_progShift;
#define PROGRESS(done) ((done) >> s_progShift)

// The hot loop of the signature search: the next 4-aligned word in [from, to) whose low halfword
// is 0xB500 (PUSH {LR}, how the routine opens). Every candidate is then compared loosely, which is
// far too slow to do at every one of eight million offsets but cheap for the few thousand that
// survive this. In IWRAM because it reads up to 32 MB.
IWRAM_CODE static uint32_t next_push_lr(uint32_t from, uint32_t to)
{
    const volatile uint32_t *p = (const volatile uint32_t *)ROM_PTR(from);
    uint32_t n = (to - from) >> 2;
    for (uint32_t i = 0; i < n; i++) {
        if ((p[i] & 0xFFFFu) == 0xB500u) return from + (i << 2);
    }
    return to;
}

static int loose_match(uint32_t a)
{
    if (!rom_ok(a, sizeof(kSelectSong))) return 0;
    const uint8_t *p = ROM_PTR(a);
    int diff = 0;
    for (unsigned i = 0; i < sizeof(kSelectSong); i++) {
        if (p[i] != kSelectSong[i] && ++diff > SELECT_SONG_MAX_DIFF) return 0;
    }
    return 1;
}

int scan_header_ok(uint32_t hdr, int *tracks)
{
    if ((hdr & 3) || !rom_ok(hdr, 8)) return 0;
    int tc = rom_u8(SH_TRACKS(hdr));
    if (tc > MAX_TRACKS) return 0;
    if (tracks) *tracks = tc;
    if (tc == 0) return 1;                       // a dummy song: legal, silent
    if (!rom_ok(hdr, 8 + 4u * (uint32_t)tc)) return 0;
    uint32_t tone = rom_u32(SH_TONE(hdr));
    if ((tone & 3) || !rom_ok(tone, TONE_SIZE)) return 0;
    for (int i = 0; i < tc; i++) {
        if (!rom_isptr(rom_u32(SH_PART(hdr, i)))) return 0;
    }
    return 1;
}

// One table entry that a heuristic search may believe: a header pointer that checks out, and a
// music-player index small enough to be real.
static int entry_ok(uint32_t e, int *tracks)
{
    if (!rom_ok(e, SONG_ENTRY_SIZE)) return 0;
    if (rom_u16(SONG_MS(e)) >= 32) return 0;
    return scan_header_ok(rom_u32(SONG_HEADER(e)), tracks);
}

static int find_signature(ScanProgressFn progress, uint32_t *done)
{
    for (uint32_t base = ROM_BASE; base < g_romEnd; base += CHUNK) {
        uint32_t end = base + CHUNK;
        if (end > g_romEnd) end = g_romEnd;
        for (uint32_t a = base; (a = next_push_lr(a, end)) < end; a += 4) {
            if (!loose_match(a)) continue;
            if (!rom_ok(a + SELECT_SONG_TABLE_OFF, 4)) continue;
            uint32_t table = rom_u32(a + SELECT_SONG_TABLE_OFF);
            if ((table & 3) || !rom_ok(table, SONG_ENTRY_SIZE)) continue;
            g_scan.selectSong = a;
            g_scan.songTable  = table;
            return 1;
        }
        *done += CHUNK;
        if (progress) progress(PROGRESS(*done));
    }
    return 0;
}

static int find_heuristic(ScanProgressFn progress, uint32_t *done)
{
    uint32_t best = 0, bestLen = 0;
    for (uint32_t base = ROM_BASE; base < g_romEnd; base += CHUNK) {
        uint32_t end = base + CHUNK;
        if (end > g_romEnd) end = g_romEnd;
        for (uint32_t a = base; a < end; a += 4) {
            // Cheap prefilter before any header is followed: a word-aligned pointer into the
            // image, and a small player index after it.
            uint32_t w = *(const volatile uint32_t *)ROM_PTR(a);
            if ((w & 3) || !rom_isptr(w)) continue;
            if (!rom_ok(a, 8) || rom_u16(a + 4) >= 32) continue;

            uint32_t len = 0, real = 0;
            int tc;
            while (entry_ok(a + len * SONG_ENTRY_SIZE, &tc)) {
                if (tc > 0) real++;
                if (++len >= SCAN_MAX_SONGS) break;
            }
            if (real >= 4 && len > bestLen) { best = a; bestLen = len; }
            if (len) a += (len - 1) * SONG_ENTRY_SIZE;     // a run is never a candidate twice
        }
        *done += CHUNK;
        if (progress) progress(PROGRESS(*done));
    }
    if (bestLen < 8) return 0;
    g_scan.songTable = best;
    return 1;
}

// Best effort: the m4aSoundMode word m4aSoundInit loads from its literal pool, somewhere near
// SelectSong (the library is one object file). Recognised by its fields all being legal at once:
// DA bits 8..B, a rate index 1..12, 1..12 channels, a master volume of at least 4, a clear top byte.
// The routine's own 48 bytes are skipped: its word at +16 (c9 18 89 00) passes every one of those
// tests, so without the exclusion it is found every time.
static uint32_t find_sound_mode(uint32_t near)
{
    uint32_t lo = (near > ROM_BASE + 0x2000u) ? near - 0x2000u : ROM_BASE;
    uint32_t hi = near + 0x2000u;
    if (hi > g_romEnd) hi = g_romEnd;
    for (uint32_t a = lo & ~3u; a + 4 <= hi; a += 4) {
        if (a >= near && a < near + 48) continue;
        uint32_t w = rom_u32(a);
        uint32_t da = (w >> 20) & 0xF, fq = (w >> 16) & 0xF, mv = (w >> 12) & 0xF, ch = (w >> 8) & 0xF;
        if ((w >> 24) == 0 && da >= 8 && da <= 0xB && fq >= 1 && fq <= 12 && mv >= 4 && ch >= 1 && ch <= 12
            && ((w & 0xFF) == 0 || (w & 0x80)))
            return w;
    }
    return 0;
}

static void build_song_list(void)
{
    g_songCount = 0;
    g_scan.tableEntries = 0;
    uint32_t prevHdr = 0;
    for (uint32_t i = 0; i < 0xFFFF; i++) {
        uint32_t e = g_scan.songTable + i * SONG_ENTRY_SIZE;
        if (!rom_ok(e, SONG_ENTRY_SIZE)) break;
        uint32_t hdr = rom_u32(SONG_HEADER(e));
        if (!rom_isptr(hdr)) break;                         // saptapper's end-of-table rule
        g_scan.tableEntries = (uint16_t)(i + 1);
        int tc;
        if (hdr != prevHdr && scan_header_ok(hdr, &tc) && tc > 0 && g_songCount < SCAN_MAX_SONGS)
            g_songIx[g_songCount++] = (uint16_t)i;
        prevHdr = hdr;
    }
}

int scan_run(ScanProgressFn progress)
{
    g_scan.method     = SCAN_NONE;
    g_scan.selectSong = 0;
    g_scan.songTable  = 0;
    g_scan.soundMode  = 0;
    g_scan.gameRate   = 13379;
    g_songCount       = 0;
    if (g_romSize == 0) return SCAN_NONE;

    // Progress runs 0..128 for the signature pass and 128..256 for the heuristic, so the bar keeps
    // moving forward if the first pass comes up empty.
    uint32_t total = g_romSize * 2, done = 0;
    s_progShift = 0;
    while ((total >> s_progShift) > 256u) s_progShift++;
    if (find_signature(progress, &done)) {
        g_scan.method = SCAN_SIGNATURE;
        g_scan.soundMode = find_sound_mode(g_scan.selectSong);
    } else {
        done = g_romSize;
        if (find_heuristic(progress, &done)) g_scan.method = SCAN_HEURISTIC;
    }
    if (progress) progress(256);

    if (g_scan.soundMode) {
        uint32_t fq = (g_scan.soundMode >> 16) & 0xF;
        g_scan.gameRate = gM4aRates[fq - 1];
    }
    if (g_scan.method != SCAN_NONE) build_song_list();
    if (g_songCount == 0) g_scan.method = SCAN_NONE;
    return g_scan.method;
}

uint32_t scan_song_entry(int n)
{
    if (n < 0 || n >= g_songCount) return 0;
    return g_scan.songTable + (uint32_t)g_songIx[n] * SONG_ENTRY_SIZE;
}
