// eng_krawall.c — Krawall (XM/S3M modules) behind the shared transport. See eng_core.h.
//
// A tracker tick at the usual speed 6 is a 24-PPQN tick (6 ticks a row, 4 rows a beat), so the
// transport's tick is one kr_tick(), and a module's own speed changes move its beat with it.
//
// THE BANKS. A Krawall module names its samples and instruments by number, in banks the game keeps
// elsewhere; discovery finds bank candidates but cannot prove which go with the modules. Krawall's
// converter writes one global bank of each, so the largest of each is taken; the CAL page steps
// through the rest, and the choice is the patch's (krSampleBank / krInstrBank, 0 = the largest).
//
// Its seek silences the voices, so RESTART BAR returns to the section's order and row (and tick)
// with the notes cut at the jump.
//
// SONGS. A module can hold several songs, split by 254 ("+++") in its order list (games keep
// jingles and effects that way). Each is listed: a song number is module x 64 + song, and a module
// with more than one plays its song in Krawall's song mode (loop or stop at the next marker).

#include "eng_core.h"
#include "gba.h"
#include "player.h"
#include "scan.h"
#include "mod.h"
#include "rom.h"
#include "mixer.h"
#include "../engines/krawall/krawall.h"
#include "../engines/krawall/discovery.h"

static KrPlayer    s_kr;
static EngMixCtx   s_mix;
static KrMods      s_last;
static uint8_t     s_ready;
static uint16_t    s_snapOrder, s_snapRow, s_snapTick, s_snapSpeed, s_snapBpm;
static KrDiscovery s_d;
static int         s_sb = -1, s_ib = -1;   // the banks in use: indices into s_d, -1 = none
uint32_t g_krBankWant[2];                  // the banks asked for (0 = the largest): mod.c <-> patch

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

static void ready(void)
{
    if (s_ready) return;
    EngHooks h;
    s_mix.rom = &s_kr.rom;
    s_mix.flags = MIX_F_UNSIGNED;      // Krawall samples are offset-binary
    s_mix.volShift = 1;                // half volume, as its trial ran
    eng_mixer_hooks(&h, &s_mix);
    kr_init(&s_kr, eng_cart_rom(), &h);
    s_ready = 1;
}

static void sync(void)
{
    if (!s_ready) return;
    KrMods *m = &s_kr.mods;
    m->mute           = g_mods.mute;
    m->tempo          = g_mods.tempoU;
    m->pitch          = g_mods.pitch;
    m->voice_shift    = g_mods.voiceShift;
    m->table_offset   = g_mods.tableOffset;
    m->scrub          = g_mods.romScrub;
    m->sample_start   = g_mods.sampleStart;
    m->sample_end     = g_mods.sampleEnd;
    m->reverse_chance = g_mods.reverseChance;
    m->drop_chance    = g_mods.dropChance;
    if (!same_bytes(&s_last, m, sizeof(KrMods))) {
        copy_bytes(&s_last, m, sizeof(KrMods));
        if (s_kr.running) kr_update(&s_kr);
    }
}

static void lanes(void)
{
    int tc = 0;
    for (int n = 0; n < 16 && n < PL_TRACKS; ++n) {
        const KrTrack *s = &s_kr.tracks[n];
        Track *t = &g_tracks[n];
        int exists = s_kr.running && n < s_kr.channels;
        t->flags   = exists ? MPT_FLG_EXIST : 0;
        t->lastKey = s->note;
        t->gate    = exists && s->gate;
        t->act     = s->activity;
        t->notes   = s->notes;
        if (exists) tc = n + 1;
    }
    g_plTrackCount = (uint8_t)tc;
}

#define KR_SONGS 64                         // songs per module (Krawall's songIndex holds 64)

static int song_for(unsigned ix, KrSong *s)
{
    unsigned i = ix >> 6, k = ix & (KR_SONGS - 1);
    if (i >= s_d.module_count || s_sb < 0) return 0;
    EngRom r = eng_cart_rom();
    uint8_t starts[KR_SONGS];
    unsigned n = kr_module_songs(&r, s_d.modules[i], s_d.formats[i], starts, 0, KR_SONGS);
    if (k >= n) return 0;
    s->module = s_d.modules[i];
    s->format = s_d.formats[i];
    s->start_order = starts[k];
    s->song_mode = (uint8_t)(n > 1);
    s->samples = s_d.samples[s_sb].address;
    s->sample_count = s_d.samples[s_sb].count;
    s->instruments = s_ib >= 0 ? s_d.instruments[s_ib].address : 0;
    s->instrument_count = s_ib >= 0 ? s_d.instruments[s_ib].count : 0;
    return 1;
}

static int start(int n)
{
    KrSong s;
    ready();
    if (!song_for(g_songIx[n], &s)) return 0;
    sync();
    s_kr.repeat = (uint8_t)(g_plRepeat && !g_plOnce);
    if (!kr_start(&s_kr, &s)) return 0;
    copy_bytes(&s_last, &s_kr.mods, sizeof(KrMods));
    return 1;
}

static void stop(void) { if (s_ready) kr_stop(&s_kr); }
static int  running(void) { return s_ready && s_kr.running; }
static uint32_t loops(void) { return s_kr.loops; }
static int  tick(void) { kr_tick(&s_kr); return 0; }

static void snap_take(void)
{
    s_snapOrder = s_kr.order; s_snapRow = s_kr.row; s_snapTick = s_kr.tick;
    s_snapSpeed = s_kr.speed; s_snapBpm = s_kr.bpm;
}

static void snap_restore(void)
{
    if (!kr_seek(&s_kr, s_snapOrder, s_snapRow)) return;
    s_kr.speed = s_snapSpeed; s_kr.bpm = s_snapBpm;
    s_kr.tick = s_snapTick < s_kr.speed ? s_snapTick : 0;
}

static void restart_loop(void) { kr_seek(&s_kr, s_kr.restart, 0); }

// kr_tick_hz_q16 is 24-PPQN ticks/s in Q16, the tempo mangle in it; tempoI = ticks/s x 150 / 59.7275,
// i.e. x 2.51141 / 65536 = 164589 / 2^32.
static uint32_t tempo_i(void)
{
    ready();
    return (uint32_t)(((uint64_t)kr_tick_hz_q16(&s_kr) * 164589u) >> 32);
}

static void diag(char *t, int a)
{
    // M modules, S/I the sample and instrument banks found, U unknown effects, E the decoder's error.
    t[a++] = ' '; t[a++] = 'M'; a = eng_put_hex(t, a, s_d.module_count, 2);
    t[a++] = ' '; t[a++] = 'S'; a = eng_put_hex(t, a, s_d.sample_count, 1);
    t[a++] = 'I'; a = eng_put_hex(t, a, s_d.instrument_count, 1);
    t[a++] = ' '; t[a++] = 'U'; a = eng_put_hex(t, a, s_kr.unsupported[0], 8);
    t[a++] = ' '; t[a++] = 'E'; t[a++] = s_kr.error ? '1' : '0';
    t[a] = 0;
}

static int put(char *t, int a, const char *s) { while (*s) t[a++] = *s++; t[a] = 0; return a; }

// Hex counts: M modules, S/I sample and instrument banks, L pointer runs judged.
int krw_scan_note(char *t)
{
    int a;
    if (!s_d.module_count)        a = put(t, 0, "KRAWALL: NO MODULES");
    else if (!s_d.sample_count)   a = put(t, 0, "KRAWALL: NO SAMPLE BANK");
    else                          a = put(t, 0, "KRAWALL: NO INSTR BANK");
    a = put(t, a, " M"); a = eng_put_hex(t, a, s_d.module_count, 2);
    a = put(t, a, " S"); a = eng_put_hex(t, a, s_d.sample_count, 1);
    a = put(t, a, "I");  a = eng_put_hex(t, a, s_d.instrument_count, 1);
    a = put(t, a, " L"); eng_put_hex(t, a, s_d.lists, 3);
    return 1;
}

static const EngCore s_core = {
    start, stop, running, tick, loops, snap_take, snap_restore, restart_loop, lanes, sync, tempo_i, diag, 0,
};

#define KR_UNSUPPORTED ((1u << DEST_ATTACK) | (1u << DEST_DECAY) | (1u << DEST_SUSTAIN) | \
                        (1u << DEST_RELEASE) | (1u << DEST_LENGTH) | (1u << DEST_VIBRATO) | \
                        (1u << DEST_TREMOLO) | (1u << DEST_LFO))

const Engine g_engKrawall = {
    "KRAWALL", ENG_ID_KRAWALL, KR_UNSUPPORTED,
    tr_start, tr_stop, tr_running, tr_tick, tr_update, tr_frame, tr_tempo_i, tr_restart_loop,
    tr_restart_bar, tr_queue_restart, tr_queue_start, tr_reroll, &s_core,
};

// ---- discovery ----------------------------------------------------------------------------------

void krw_discover_reset(void)
{
    volatile uint8_t *p = (volatile uint8_t *)&s_d;
    for (unsigned i = 0; i < sizeof(s_d); i++) p[i] = 0;
    s_sb = s_ib = -1;
}

// The pass's word loop, in IWRAM: it reads every word of the cart. A module is tried only where its
// fixed fields can hold (channels 1..32, speed 1..31, BPM 32 or more, the five flags 0 or 1, the pad
// byte 0: the words at +0, +356 and +360), and a run of pointers is judged as a bank when it ends.
// It does what kr_discover_slice() does, without a function call per word.
IWRAM_CODE static void kr_words(const EngRom *r, uint32_t begin, uint32_t end)
{
    const volatile uint32_t *p = (const volatile uint32_t *)ROM_PTR(begin);
    uint32_t n = (end - begin) >> 2, base = r->base, size = r->size;
    uint32_t lastModule = size >= 368 ? base + size - 368 : 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t a = begin + (i << 2), w = p[i];
        if (a <= lastModule) {
            uint32_t f = p[i + 89], g = p[i + 90];
            if (!(g & 0xFFFEFEFEu) && !(f & 0xFEFE0000u) && (uint8_t)(f - 1u) < 31u &&
                ((f >> 8) & 0xFFu) >= 32u && (uint8_t)(w - 1u) < 32u)
                kr_discover_module(&s_d, r, a);
        }
        if (w - base < size && w != 0x08080808u) {
            if (!s_d.run_count++) s_d.run_start = a;
            continue;
        }
        if (s_d.run_count) kr_discover_list(&s_d, r, s_d.run_start, s_d.run_count);
        s_d.run_count = 0;
    }
}

void krw_discover(uint32_t begin, uint32_t end)
{
    EngRom r = eng_cart_rom();
    kr_words(&r, begin, end);
}

// The patch's bank if it is a candidate, else the largest.
static int pick(const KrBank *b, unsigned n, uint32_t want)
{
    int best = -1;
    for (unsigned i = 0; i < n; i++) {
        if (want && b[i].address == want) return (int)i;
        if (best < 0 || b[i].count > b[best].count) best = (int)i;
    }
    return best;
}

void krw_discover_finish(void)
{
    if (s_d.run_count) {                           // a run that reached the cart's end
        EngRom r = eng_cart_rom();
        kr_discover_list(&s_d, &r, s_d.run_start, s_d.run_count);
        s_d.run_count = 0;
    }
    s_sb = pick(s_d.samples, s_d.sample_count, g_krBankWant[0]);
    s_ib = pick(s_d.instruments, s_d.instrument_count, g_krBankWant[1]);
    if (s_d.module_count && s_sb >= 0) scan_add_table(ENG_ID_KRAWALL, s_d.modules[0]);
}

// Every song of every module the banks can serve (an instrument-based one needs an instrument
// bank). Kind from one pass at its starting speed: a module's only song loops to its restart order,
// so it is music; of several, a short one is an effect and the rest music (Krawall's song mode
// loops them; the game may play one once, which the data cannot say).
int krw_table_songs(const ScanResult *t, uint16_t *ix, uint8_t *kind, int max)
{
    (void)t;
    EngRom r = eng_cart_rom();
    int k = 0;
    for (unsigned i = 0; i < s_d.module_count && k < max; i++) {
        uint32_t m = s_d.modules[i];
        if (s_sb < 0 || (rom_u8(m + 358) && s_ib < 0)) continue;
        uint8_t starts[KR_SONGS];
        uint32_t ticks[KR_SONGS];
        unsigned n = kr_module_songs(&r, m, s_d.formats[i], starts, ticks, KR_SONGS);
        for (unsigned j = 0; j < n && k < max; j++) {
            ix[k] = (uint16_t)((i << 6) | j);
            kind[k++] = eng_kind(ticks[j], n == 1 || ticks[j] >= KIND_SFX_TICKS);
        }
    }
    return k;
}

// ---- the bank choice (CAL page) ------------------------------------------------------------------

int eng_kr_bank_count(int which) { return which ? (int)s_d.instrument_count : (int)s_d.sample_count; }

void eng_kr_bank_step(int which, int delta)
{
    int n = eng_kr_bank_count(which);
    if (n < 2) return;
    int *ix = which ? &s_ib : &s_sb;
    int v = *ix + delta;
    while (v < 0) v += n;
    while (v >= n) v -= n;
    *ix = v;
    g_krBankWant[which] = which ? s_d.instruments[v].address : s_d.samples[v].address;
    scan_forget();
}

int eng_kr_bank_apply(void)
{
    if (!s_d.module_count) return 0;
    int sb = pick(s_d.samples, s_d.sample_count, g_krBankWant[0]);
    int ib = pick(s_d.instruments, s_d.instrument_count, g_krBankWant[1]);
    if (sb == s_sb && ib == s_ib) return 0;
    s_sb = sb; s_ib = ib;
    scan_forget();
    return 1;
}

void eng_kr_bank_text(int which, char *t)
{
    int a = 0, n = eng_kr_bank_count(which), ix = which ? s_ib : s_sb;
    const char *label = which ? "INSTR   " : "SAMPLES ";
    while (*label) t[a++] = *label++;
    if (ix < 0) { t[a++] = '-'; t[a] = 0; return; }
    const KrBank *b = which ? &s_d.instruments[ix] : &s_d.samples[ix];
    a = eng_put_hex(t, a, (uint32_t)ix + 1, 2);
    t[a++] = '/';
    a = eng_put_hex(t, a, (uint32_t)n, 2);
    t[a++] = ' ';
    a = eng_put_hex(t, a, b->address, 8);
    t[a++] = ' '; t[a++] = 'X';
    a = eng_put_hex(t, a, b->count, 3);
    t[a] = 0;
}
