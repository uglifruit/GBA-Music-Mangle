// scan.c — MP2K song-table detection. See scan.h for the methods.

#include "scan.h"
#include "rom.h"
#include "m4a.h"
#include "gba.h"

ScanResult g_tables[SCAN_MAX_TABLES];
uint8_t    g_tableCount;
uint8_t    g_tableIx;
uint8_t    g_listMode;
uint16_t   g_songIx[SCAN_MAX_SONGS];
uint8_t    g_songPlayer[SCAN_MAX_SONGS];
uint16_t   g_songCount;
uint8_t    g_engines;
uint16_t   g_xmCount;
char       g_engineInfo[ENGINE_INFO_LEN + 1];

static void add_table(uint8_t method, uint32_t selectSong, uint32_t table);

// ---- other engines' signatures (see scan.h) ----
// Every signature is at least 7 bytes long, so wherever it sits, one of its first four 4-byte
// windows is word-aligned: a word-by-word pass can find it by comparing each word with those four.
// A 1024-bit filter on a hash of the word keeps that to a test per word; a full compare runs only
// on the rare words that pass.
typedef struct { const char *s; uint8_t len, engine; } Sniff;
static const Sniff kSniff[] = {                     // in order of preference for g_engineInfo
    { "GAX Sound Engine",  16, ENG_GAX     },
    { "GAX ENGINE V",      12, ENG_GAX     },
    { "Krawall",            7, ENG_KRAWALL },
    { "Extended Module: ", 17, ENG_XM      },
};
#define SNIFF_N ((int)(sizeof(kSniff) / sizeof(kSniff[0])))
#define SNIFF_HASH(w) (((w) * 0x9E3779B1u) >> 22)
static uint32_t s_sniffWord[SNIFF_N][4];
static uint32_t s_sniffBits[32];
static uint8_t  s_infoFrom;                         // kSniff index that wrote g_engineInfo

static void sniff_init(void)
{
    g_engines = 0;
    g_xmCount = 0;
    g_engineInfo[0] = 0;
    s_infoFrom = 0xFF;
    for (int i = 0; i < 32; i++) s_sniffBits[i] = 0;
    for (int p = 0; p < SNIFF_N; p++) {
        const uint8_t *s = (const uint8_t *)kSniff[p].s;
        for (int k = 0; k < 4; k++) {
            uint32_t w = s[k] | (uint32_t)s[k + 1] << 8 | (uint32_t)s[k + 2] << 16 | (uint32_t)s[k + 3] << 24;
            s_sniffWord[p][k] = w;
            uint32_t h = SNIFF_HASH(w);
            s_sniffBits[h >> 5] |= 1u << (h & 31);
        }
    }
}

// The word w at the aligned address a passed the filter: which signatures really start near it?
static void sniff_hit(uint32_t a, uint32_t w)
{
    for (int p = 0; p < SNIFF_N; p++) {
        for (uint32_t k = 0; k < 4; k++) {
            if (s_sniffWord[p][k] != w || a - ROM_BASE < k) continue;
            const Sniff *s = &kSniff[p];
            uint32_t at = a - k;
            if (!rom_ok(at, s->len)) continue;
            unsigned i = 0;
            while (i < s->len && rom_u8(at + i) == (uint8_t)s->s[i]) i++;
            if (i < s->len) continue;
            g_engines |= s->engine;
            if (s->engine == ENG_XM && g_xmCount < 0xFFFF) g_xmCount++;
            if (p < s_infoFrom) {
                s_infoFrom = (uint8_t)p;
                int n = 0;
                while (n < ENGINE_INFO_LEN && rom_ok(at + n, 1)) {
                    uint8_t c = rom_u8(at + n);
                    if (c < 0x20 || c > 0x7E) break;
                    g_engineInfo[n++] = (char)c;
                }
                while (n > 0 && g_engineInfo[n - 1] == ' ') n--;
                g_engineInfo[n] = 0;
            }
        }
    }
}

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
            add_table(SCAN_SIGNATURE, a, table);        // keep going: there may be another copy
        }
        *done += CHUNK;
        if (progress) progress(PROGRESS(*done));
    }
    return g_tableCount != 0;
}

static int find_heuristic(ScanProgressFn progress, uint32_t *done)
{
    // The longest few runs, kept sorted longest first.
    uint32_t best[SCAN_MAX_TABLES], bestLen[SCAN_MAX_TABLES];
    int nBest = 0;
    for (uint32_t base = ROM_BASE; base < g_romEnd; base += CHUNK) {
        uint32_t end = base + CHUNK;
        if (end > g_romEnd) end = g_romEnd;
        for (uint32_t a = base; a < end; a += 4) {
            // Cheap prefilter before any header is followed: a word-aligned pointer into the
            // image, and a small player index after it.
            uint32_t w = *(const volatile uint32_t *)ROM_PTR(a);
            uint32_t h = SNIFF_HASH(w);
            if (s_sniffBits[h >> 5] & (1u << (h & 31))) sniff_hit(a, w);
            if ((w & 3) || !rom_isptr(w)) continue;
            if (!rom_ok(a, 8) || rom_u16(a + 4) >= 32) continue;

            uint32_t len = 0, real = 0;
            int tc;
            while (entry_ok(a + len * SONG_ENTRY_SIZE, &tc)) {
                if (tc > 0) real++;
                if (++len >= SCAN_MAX_SONGS) break;
            }
            if (real >= 4 && len >= 8 && (nBest < SCAN_MAX_TABLES || len > bestLen[nBest - 1])) {
                int k = (nBest < SCAN_MAX_TABLES) ? nBest++ : nBest - 1;
                while (k > 0 && bestLen[k - 1] < len) {
                    best[k] = best[k - 1];
                    bestLen[k] = bestLen[k - 1];
                    k--;
                }
                best[k] = a;
                bestLen[k] = len;
            }
            if (len) a += (len - 1) * SONG_ENTRY_SIZE;     // a run is never a candidate twice
        }
        *done += CHUNK;
        if (progress) progress(PROGRESS(*done));
    }
    for (int i = 0; i < nBest; i++) add_table(SCAN_HEURISTIC, 0, best[i]);
    return g_tableCount != 0;
}

// The nearest 4-aligned match of one of the patterns in [pos - len, pos), searching backwards:
// saptapper's find_backwards. 0 if none.
static uint32_t find_back(uint32_t pos, uint32_t len, const uint8_t *pats, int nPats, unsigned patLen)
{
    for (uint32_t a = pos - 4; a + len >= pos && a >= ROM_BASE; a -= 4) {
        if (!rom_ok(a, patLen)) continue;
        for (int p = 0; p < nPats; p++) {
            unsigned i = 0;
            while (i < patLen && rom_u8(a + i) == pats[p * patLen + i]) i++;
            if (i == patLen) return a;
        }
    }
    return 0;
}

// The m4aSoundMode word that m4aSoundInit loads from its literal pool: it holds the game's mixing
// rate, which the fixed-rate samples (most drum kits) play at.
//
// saptapper's route to m4aSoundInit (FindMainFn, FindInitFn): m4aSoundMain is the nearest PUSH {LR}
// within 0x20 bytes before SelectSong, and m4aSoundInit the nearest of two prologues within 0x100
// bytes before that. The library builds them in that order (pret's m4a.c), so m4aSoundInit's
// literal pool lies between it and m4aSoundMain. Its other literals are RAM and ROM pointers and a
// CpuSet word, none of which pass the field tests below.
//
// This replaced a search of the 16 KB around SelectSong, which took the first word passing the
// field tests. Code and data pass them by chance often enough that a game could get a wrong rate,
// and every drum then played too fast or too slow (Pokemon Ruby's percussion came out as hiss).
//
// The field tests: DA bits 8..B, a rate index 1..12, 1..12 channels, a master volume of at least 4,
// a clear top byte. With no prologue matched, the 0x200 bytes before SelectSong are searched
// instead. Either way the LAST passing word wins, as the pool sits at the end of m4aSoundInit.
static const uint8_t kPushLr[2]  = { 0x00, 0xb5 };
static const uint8_t kInitFn[8]  = { 0x70, 0xb5, 0x14, 0x48,       // push {r4-r6,lr}; ldr r0, =SoundMainRAM+1
                                     0xf0, 0xb5, 0x47, 0x46 };     // push {r4-r7,lr}; mov r7, r8
static uint32_t find_sound_mode(uint32_t selectSong)
{
    uint32_t mainFn = find_back(selectSong, 0x20, kPushLr, 1, 2);
    uint32_t initFn = mainFn ? find_back(mainFn, 0x100, kInitFn, 2, 4) : 0;
    uint32_t lo = initFn ? initFn : (selectSong > ROM_BASE + 0x200u ? selectSong - 0x200u : ROM_BASE);
    uint32_t hi = initFn ? mainFn : selectSong;
    uint32_t found = 0;
    for (uint32_t a = lo & ~3u; a + 4 <= hi; a += 4) {
        uint32_t w = rom_u32(a);
        uint32_t da = (w >> 20) & 0xF, fq = (w >> 16) & 0xF, mv = (w >> 12) & 0xF, ch = (w >> 8) & 0xF;
        if ((w >> 24) == 0 && da >= 8 && da <= 0xB && fq >= 1 && fq <= 12 && mv >= 4 && ch >= 1 && ch <= 12
            && ((w & 0xFF) == 0 || (w & 0x80)))
            found = w;
    }
    return found;
}

static void add_table(uint8_t method, uint32_t selectSong, uint32_t table)
{
    for (int i = 0; i < g_tableCount; i++) if (g_tables[i].songTable == table) return;
    if (g_tableCount >= SCAN_MAX_TABLES) return;
    ScanResult *t = &g_tables[g_tableCount++];
    t->method       = method;
    t->selectSong   = selectSong;
    t->songTable    = table;
    t->tableEntries = 0;
    t->soundMode    = selectSong ? find_sound_mode(selectSong) : 0;
    t->gameRate     = t->soundMode ? gM4aRates[((t->soundMode >> 16) & 0xF) - 1] : 13379;
}

int scan_select(int table, int listMode)
{
    if (table < 0 || table >= g_tableCount) table = 0;
    if (listMode < 0 || listMode >= LIST_COUNT) listMode = LIST_ALL;
    g_tableIx   = (uint8_t)table;
    g_listMode  = (uint8_t)listMode;
    g_songCount = 0;
    if (g_tableCount == 0) return 0;

    uint32_t prevHdr = 0;
    g_scan.tableEntries = 0;
    for (uint32_t i = 0; i < 0xFFFF; i++) {
        uint32_t e = g_scan.songTable + i * SONG_ENTRY_SIZE;
        if (!rom_ok(e, SONG_ENTRY_SIZE)) break;
        uint32_t hdr = rom_u32(SONG_HEADER(e));
        if (!rom_isptr(hdr)) break;                         // saptapper's end-of-table rule
        g_scan.tableEntries = (uint16_t)(i + 1);
        uint32_t ms = rom_u16(SONG_MS(e));
        int want = (listMode == LIST_ALL) || ((listMode == LIST_MUSIC) == (ms == 0));
        int tc;
        if (want && hdr != prevHdr && scan_header_ok(hdr, &tc) && tc > 0 && g_songCount < SCAN_MAX_SONGS) {
            g_songPlayer[g_songCount] = (uint8_t)(ms > 255 ? 255 : ms);
            g_songIx[g_songCount++]   = (uint16_t)i;
        }
        prevHdr = hdr;
    }
    return g_songCount;
}

// Drop tables with no playable song at all (false positives), so the count means something.
// Returns the tables kept.
static int drop_empty(void)
{
    int found = g_tableCount, keep = 0;
    for (int i = 0; i < found; i++) {
        g_tableCount = (uint8_t)found;
        if (scan_select(i, LIST_ALL) == 0) continue;
        if (keep != i) {
            volatile uint8_t *d = (volatile uint8_t *)&g_tables[keep];
            const volatile uint8_t *s = (const volatile uint8_t *)&g_tables[i];
            for (unsigned b = 0; b < sizeof(ScanResult); b++) d[b] = s[b];
        }
        keep++;
    }
    g_tableCount = (uint8_t)keep;
    g_tableIx = 0;
    return keep;
}

int scan_run(ScanProgressFn progress)
{
    g_tableCount = 0;
    g_tableIx    = 0;
    g_songCount  = 0;
    g_tables[0].method       = SCAN_NONE;
    g_tables[0].selectSong   = 0;
    g_tables[0].songTable    = 0;
    g_tables[0].tableEntries = 0;
    g_tables[0].soundMode    = 0;
    g_tables[0].gameRate     = 13379;
    if (g_romSize == 0) return SCAN_NONE;

    // Progress runs 0..128 for the signature pass and 128..256 for the heuristic, so the bar keeps
    // moving forward if the first pass comes up empty.
    uint32_t total = g_romSize * 2, done = 0;
    s_progShift = 0;
    while ((total >> s_progShift) > 256u) s_progShift++;
    sniff_init();
    find_signature(progress, &done);
    // The heuristic (and the engine sniff riding on it) runs when the signature found no table
    // with a playable song - none at all, or only false positives.
    if (drop_empty() == 0) {
        done = g_romSize;
        find_heuristic(progress, &done);
        drop_empty();
    }
    if (progress) progress(256);
    if (g_tableCount == 0) {
        g_tableIx = 0;
        g_songCount = 0;
        g_tables[0].method = SCAN_NONE;
        return SCAN_NONE;
    }
    // Start on the MUSIC list when there is any. Games often put the effects first (Pokemon's
    // first 250-odd songs are effects), and song 1 should be a song.
    if (scan_select(0, LIST_MUSIC) == 0) scan_select(0, LIST_ALL);
    return g_scan.method;
}

int scan_install(int n, const uint32_t *addr, const uint16_t *rate)
{
    g_tableCount = 0;
    g_tableIx    = 0;
    g_songCount  = 0;
    sniff_init();
    for (int i = 0; i < n && i < SCAN_MAX_TABLES; i++) {
        uint32_t a = addr[i];
        if ((a & 3) || !rom_ok(a, SONG_ENTRY_SIZE)) continue;
        ScanResult *t = &g_tables[g_tableCount++];
        t->method       = SCAN_SAVED;
        t->selectSong   = 0;
        t->songTable    = a;
        t->tableEntries = 0;
        t->soundMode    = 0;
        t->gameRate     = (rate[i] >= 5734 && rate[i] <= 42048) ? rate[i] : 13379;
    }
    if (drop_empty() == 0) {
        g_tables[0].method   = SCAN_NONE;
        g_tables[0].songTable = 0;
        g_tables[0].gameRate = 13379;
        g_songCount = 0;
        return 0;
    }
    if (scan_select(0, LIST_MUSIC) == 0) scan_select(0, LIST_ALL);
    return g_tableCount;
}

uint32_t scan_song_entry(int n)
{
    if (n < 0 || n >= g_songCount) return 0;
    return g_scan.songTable + (uint32_t)g_songIx[n] * SONG_ENTRY_SIZE;
}
