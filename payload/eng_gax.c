// eng_gax.c — GAX (Shin'en, 3.x song data) behind the shared transport. See eng_core.h.
//
// GAX has no musical tick: it runs per video frame (59.73 Hz), a row every `speed` frames (two
// speeds alternating for swing). Taking 4 rows to a beat, as trackers do, a 24-PPQN tick is speed/6
// frames; the transport's tick runs that many gx_frame()s through a Q8 accumulator. On the internal
// clock that is exactly the game's tempo; on an external clock the rows follow the clock, which is
// the approximation (a song written 3 or 8 rows to a beat follows it at that ratio).
//
// GAX's voices are its channels, and its seek re-decodes from the pattern start silencing them, so
// RESTART BAR is "back to the order and row the section began on" with the notes cut at the jump.
//
// GAX 2 (2.01 .. 2.35, since 2026-11-05): the cart's version text says which. Its songs are found by
// their song entries (gax.h, gx_v2_entry) instead of V3 headers, s_songs holds the entry addresses,
// and they play in gax.c's V2 mode with SIGNED samples (V3's are unsigned).

#include "eng_core.h"
#include "gba.h"
#include "player.h"
#include "scan.h"
#include "mod.h"
#include "rom.h"
#include "mixer.h"
#include "../engines/gax/gax.h"

#define GX_MAX_SONGS 128
#define GX2_VOL_SHIFT 1               // GAX 2 mix level (tuned on real carts below)

static GxPlayer  s_gx;
static EngMixCtx s_mix;
static GxMods    s_last;
static uint8_t   s_ready;
static uint32_t  s_frameAcc;           // Q8 frames owed
static uint16_t  s_snapOrder, s_snapRow;
static uint8_t   s_snapSpeed[2], s_snapPhase;

// Discovery's finds, and what it saw on the way (the CAL page reports it: a cart whose songs are
// turned away says at which check).
static uint32_t    s_songs[GX_MAX_SONGS];
static uint16_t    s_songCount, s_strictKeep;
static GxScanStats s_stats;
static uint8_t     s_v2;               // the cart's GAX is 2.x: s_songs are V2 song entries

static void copy_bytes(void *dst, const void *src, unsigned size)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    for (unsigned i = 0; i < size; ++i) d[i] = s[i];
}

static int same_bytes(const void *a, const void *b, unsigned size)
{
    const volatile uint8_t *x = (const volatile uint8_t *)a, *y = (const volatile uint8_t *)b;
    for (unsigned i = 0; i < size; ++i) if (x[i] != y[i]) return 0;
    return 1;
}

static uint32_t udiv(uint32_t n, uint32_t d)            // shift-subtract (-nostdlib: no __aeabi_uidiv)
{
    uint32_t q = 0, r = 0;
    if (!d) return 0;
    for (int i = 31; i >= 0; --i) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= 1u << i; }
    }
    return q;
}

static void ready(void)
{
    if (s_ready) return;
    EngHooks h;
    s_mix.rom = &s_gx.rom;
    s_mix.flags = MIX_F_UNSIGNED;      // GAX samples are offset-binary
    s_mix.volShift = 1;                // half volume, as its trial ran
    eng_mixer_hooks(&h, &s_mix);
    gx_init(&s_gx, eng_cart_rom(), &h);
    s_ready = 1;
}

static void sync(void)
{
    if (!s_ready) return;
    GxMods *m = &s_gx.mods;
    m->mute           = g_mods.mute;
    m->tempo          = 256;           // the transport schedules the frames; gx_frame_hz is unused
    m->pitch          = g_mods.pitch;
    m->voice_shift    = g_mods.voiceShift;
    m->table_offset   = g_mods.tableOffset;
    m->scrub          = g_mods.romScrub;
    m->sample_start   = g_mods.sampleStart;
    m->sample_end     = g_mods.sampleEnd;
    m->reverse_chance = g_mods.reverseChance;
    m->drop_chance    = g_mods.dropChance;
    if (!same_bytes(&s_last, m, sizeof(GxMods))) {
        copy_bytes(&s_last, m, sizeof(GxMods));
        if (s_gx.running) gx_update(&s_gx);
    }
}

static void lanes(void)
{
    int tc = 0;
    for (int n = 0; n < 16 && n < PL_TRACKS; ++n) {
        const GxTrack *s = &s_gx.tracks[n];
        Track *t = &g_tracks[n];
        int exists = s_gx.running && n < s_gx.channels;
        t->flags   = exists ? MPT_FLG_EXIST : 0;
        t->lastKey = s->note;
        t->gate    = exists && s->gate;
        t->act     = s->activity;
        t->notes   = s->notes;
        if (exists) tc = n + 1;
    }
    g_plTrackCount = (uint8_t)tc;
}

static int start(int n)
{
    ready();
    if (g_songIx[n] >= s_songCount) return 0;
    sync();
    s_gx.repeat = (uint8_t)(g_plRepeat && !g_plOnce);
    s_mix.flags = s_v2 ? 0 : MIX_F_UNSIGNED;         // GAX 2 samples are signed, GAX 3's offset-binary
    s_mix.volShift = s_v2 ? GX2_VOL_SHIFT : 1;
    if (s_v2) {
        EngRom r = eng_cart_rom();
        uint32_t hdr, chans[GX_CHANNELS];
        unsigned ch = gx_v2_entry(&r, s_songs[g_songIx[n]], &hdr, chans, GX_CHANNELS);
        if (!ch || !gx_start_v2(&s_gx, hdr, chans, ch)) return 0;
    } else if (!gx_start(&s_gx, s_songs[g_songIx[n]])) return 0;
    copy_bytes(&s_last, &s_gx.mods, sizeof(GxMods));
    s_frameAcc = 256;                  // the first tick plays its row at once
    return 1;
}

static void stop(void) { if (s_ready) gx_stop(&s_gx); }
static int  running(void) { return s_ready && s_gx.running; }
static uint32_t loops(void) { return s_gx.loops; }

// Frames per row, both speeds summed (they alternate); 0 (a hold) counts as the default 6 each.
static unsigned speed_sum(void)
{
    unsigned s = (unsigned)s_gx.speed[0] + s_gx.speed[1];
    return s ? s : 12;
}

static int tick(void)
{
    // A row is 6 ticks; a row is speed frames: speed/6 frames a tick, speed_sum/12 on average.
    s_frameAcc += (speed_sum() * 256u) / 12u;
    for (int k = 0; s_frameAcc >= 256 && k < 64; ++k) {
        s_frameAcc -= 256;
        gx_frame(&s_gx);
        if (!s_gx.running) return 0;
    }
    if (s_frameAcc >= 256) s_frameAcc = 0;
    return 0;
}

static void snap_take(void)
{
    s_snapOrder = s_gx.order; s_snapRow = s_gx.row;
    s_snapSpeed[0] = s_gx.speed[0]; s_snapSpeed[1] = s_gx.speed[1]; s_snapPhase = s_gx.phase;
}

static void snap_restore(void)
{
    if (!gx_seek(&s_gx, s_snapOrder, s_snapRow)) return;
    s_gx.speed[0] = s_snapSpeed[0]; s_gx.speed[1] = s_snapSpeed[1]; s_gx.phase = s_snapPhase;
    s_frameAcc = 256;
}

static void restart_loop(void)
{
    gx_seek(&s_gx, s_gx.restart, 0);
    s_frameAcc = 256;
}

// tempoI (ticks per frame x 150) = 24-PPQN ticks/s x 150 / 59.7275, and ticks/s = 6 x 59.7275 /
// speed: so 900 / speed, x the tempo mangle. Speeds change rarely: the divide is cached.
static uint32_t tempo_i(void)
{
    static unsigned lastS, lastU;
    static uint32_t last;
    unsigned s = speed_sum(), u = g_mods.tempoU;
    if (s != lastS || u != lastU) {
        lastS = s; lastU = u;
        last = udiv(1800u * u, s * 256u);   // 900 / (s / 2), x tempoU / 256
        if (!last) last = 1;
    }
    return last;
}

static void diag(char *t, int a)
{
    // S songs, OLD how many the first (stricter) scan would have kept, FX banks, then the check
    // that turned away the most near misses, and the decoder's own error flag.
    static const char *stage[GX_REJECT_COUNT] = { "", "", "", "SHP", "NOT", "INS", "SMP", "CHN" };
    if (s_v2) {                                      // GAX 2: its songs, and the decoder's error flag
        t[a++] = ' '; t[a++] = 'V'; t[a++] = '2';
        t[a++] = ' '; t[a++] = 'S'; a = eng_put_hex(t, a, s_songCount, 2);
        t[a++] = ' '; t[a++] = 'E'; t[a++] = s_gx.error ? '1' : '0';
        t[a] = 0;
        return;
    }
    unsigned top = 0;
    for (unsigned k = GX_REJECT_SHAPE; k < GX_REJECT_COUNT; k++) if (s_stats.count[k] > s_stats.count[top]) top = k;
    t[a++] = ' '; t[a++] = 'S'; a = eng_put_hex(t, a, s_songCount, 2);
    t[a++] = ' '; t[a++] = 'O'; t[a++] = 'L'; t[a++] = 'D'; a = eng_put_hex(t, a, s_strictKeep, 2);
    t[a++] = ' '; t[a++] = 'F'; t[a++] = 'X'; a = eng_put_hex(t, a, s_stats.count[GX_HDR_FX], 2);
    if (top) {
        t[a++] = ' ';
        for (const char *m = stage[top]; *m; ) t[a++] = *m++;
        a = eng_put_hex(t, a, s_stats.count[top], 3);
    }
    t[a++] = ' '; t[a++] = 'E'; t[a++] = s_gx.error ? '1' : '0';
    t[a] = 0;
}

static int put(char *t, int a, const char *s) { while (*s) t[a++] = *s++; t[a] = 0; return a; }

int gx_scan_note(char *t)
{
    if (s_v2) { put(t, 0, "GAX 2: NO SONG ENTRIES MATCHED"); return 1; }
    int a = put(t, 0, "GAX: NO SONGS MATCHED");
    unsigned top = 0;
    for (unsigned k = GX_REJECT_SHAPE; k < GX_REJECT_COUNT; k++) if (s_stats.count[k] > s_stats.count[top]) top = k;
    if (top) {
        static const char *stage[GX_REJECT_COUNT] = { "", "", "", "SHP", "NOT", "INS", "SMP", "CHN" };
        a = put(t, a, " ");
        a = put(t, a, stage[top]);
        eng_put_hex(t, a, s_stats.count[top], 3);
    }
    return 1;
}

static const EngCore s_core = {
    start, stop, running, tick, loops, snap_take, snap_restore, restart_loop, lanes, sync, tempo_i, diag, 0,
};

#define GX_UNSUPPORTED ((1u << DEST_ATTACK) | (1u << DEST_DECAY) | (1u << DEST_SUSTAIN) | \
                        (1u << DEST_RELEASE) | (1u << DEST_LENGTH) | (1u << DEST_VIBRATO) | \
                        (1u << DEST_TREMOLO) | (1u << DEST_LFO))

const Engine g_engGax = {
    "GAX", ENG_ID_GAX, GX_UNSUPPORTED,
    tr_start, tr_stop, tr_running, tr_tick, tr_update, tr_frame, tr_tempo_i, tr_restart_loop,
    tr_restart_bar, tr_queue_restart, tr_queue_start, tr_reroll, &s_core,
};

// ---- discovery: song headers by gaxtapper's v3 rules, every word of the cart ----------------------

void gx_discover_reset(void)
{
    s_songCount = 0;
    s_strictKeep = 0;
    s_v2 = 0;
    for (int k = 0; k < GX_REJECT_COUNT; k++) s_stats.count[k] = 0;
}

// A word that passed the loop's test: classified and counted as gx_scan_stats() does.
static void gx_candidate(const EngRom *r, uint32_t a)
{
    int k = gx_header_kind(r, a);
    if (k == GX_HDR_SONG && s_songCount < GX_MAX_SONGS) s_songs[s_songCount++] = a;
    if (k > 0 && k < GX_REJECT_COUNT) s_stats.count[k]++;
}

// The pass's word loop, in IWRAM: it reads every word of the cart. The full check runs only where a
// header could start: a channel count of 32 or less, the reserved +0x1E zero, and an instrument or
// sample pointer into the cart (every song and FX header has both). The near-miss counts therefore
// cover only candidates with one of those pointers (all of them before 2026-11-03).
IWRAM_CODE static void gx_words(const EngRom *r, uint32_t begin, uint32_t end)
{
    uint32_t base = r->base, size = r->size;
    if (size < 32) return;
    uint32_t stop = base + size - 28;                   // a header needs 32 bytes from a
    if (end > stop) end = stop;
    if (end <= begin) return;
    const volatile uint32_t *p = (const volatile uint32_t *)ROM_PTR(begin);
    uint32_t n = (end - begin) >> 2;
    for (uint32_t i = 0; i < n; i++) {
        if ((p[i] & 0xFFFFu) > 32u || (p[i + 7] >> 16)) continue;
        if (p[i + 4] - base >= size && p[i + 5] - base >= size) continue;
        gx_candidate(r, begin + (i << 2));
    }
}

// The version the sniff read; see gax_major() below.
static int gax_major(void);

// GAX 2's word loop, in IWRAM: a song entry is a count 4..40 followed by a pointer into the cart, and
// only those words get the full check (gx_v2_entry, which follows the handlers). Entries are kept once
// each by their header (a song may be reachable from more than one entry).
IWRAM_CODE static void gx2_words(const EngRom *r, uint32_t begin, uint32_t end)
{
    uint32_t base = r->base, size = r->size;
    if (size < 8) return;
    uint32_t stop = base + size - 4;
    if (end > stop) end = stop;
    if (end <= begin) return;
    const volatile uint32_t *p = (const volatile uint32_t *)ROM_PTR(begin);
    uint32_t n = (end - begin) >> 2;
    for (uint32_t i = 0; i < n && s_songCount < GX_MAX_SONGS; i++) {
        uint32_t c = p[i];
        if (c - 4u > 36u || p[i + 1] - base >= size) continue;
        uint32_t hdr, chans[GX_CHANNELS], a = begin + (i << 2);
        if (!gx_v2_entry(r, a, &hdr, chans, GX_CHANNELS)) continue;
        int seen = 0;
        for (unsigned k = 0; k < s_songCount && !seen; k++) {
            uint32_t h2, c2[GX_CHANNELS];
            if (gx_v2_entry(r, s_songs[k], &h2, c2, GX_CHANNELS) && h2 == hdr) seen = 1;
        }
        if (!seen) s_songs[s_songCount++] = a;
    }
}

void gx_discover(uint32_t begin, uint32_t end)
{
    EngRom r = eng_cart_rom();
    if (gax_major() == 2) { s_v2 = 1; gx2_words(&r, begin, end); }
    else gx_words(&r, begin, end);
}

// The version the sniff read ("GAX Sound Engine 3.05", "GAX ENGINE V3.05"): 2.x uses another
// header layout, so its "songs" would be chance matches. Those carts get no table, and say why.
static int gax_major(void)
{
    const char *s = g_engineInfo;
    if (s[0] != 'G' || s[1] != 'A' || s[2] != 'X') return 0;
    for (int i = 3; s[i]; i++) if (s[i] >= '1' && s[i] <= '9' && s[i + 1] == '.') return s[i] - '0';
    return 0;
}

void gx_discover_finish(void)
{
    EngRom r = eng_cart_rom();
    if (gax_major() == 2) s_v2 = 1;
    else for (unsigned i = 0; i < s_songCount; i++) if (!gx_strict_reason(&r, s_songs[i])) s_strictKeep++;
    if (s_songCount) scan_add_table(ENG_ID_GAX, s_songs[0]);
}

// Every header found is a song; its kind from one pass at the starting speed (6 frames a row, so
// 6 ticks a row): GAX songs always loop back to their restart order.
int gx_table_songs(const ScanResult *t, uint16_t *ix, uint8_t *kind, int max)
{
    (void)t;
    int k = 0;
    EngRom r = eng_cart_rom();
    for (unsigned i = 0; i < s_songCount && k < max; i++) {
        uint32_t a = s_songs[i];
        if (s_v2) {                                  // the entry's header
            uint32_t chans[GX_CHANNELS];
            if (!gx_v2_entry(&r, s_songs[i], &a, chans, GX_CHANNELS)) continue;
        }
        uint32_t ticks = (uint32_t)rom_u16(a + 2) * rom_u16(a + 4) * 6u;
        ix[k] = (uint16_t)i;
        kind[k++] = eng_kind(ticks, 1);
    }
    return k;
}
