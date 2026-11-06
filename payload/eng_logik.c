// eng_logik.c — Logik State's GBAModPlay ("LS_Play") behind the shared transport. See eng_core.h.
//
// A driver tick is a 24-PPQN tick, as Krawall's is: a tracker beat is 24 ticks (4 rows at speed 6),
// and Puzzle Fighter's modules, all at speed 1 and 150 BPM, put 24 rows to a beat. So the transport's
// tick is one ls_tick(), and tempo_i is the driver's own tick rate (BPM x 2 / 5).
//
// THE BANK. A module plays instruments from a sample bank the game hands the driver; nothing in the
// module points at it. Discovery takes a bank that holds every instrument the modules play: first
// right after a module (where Puzzle Fighter's converter put it), else anywhere near them.
//
// Every module is a song. Its seek silences the voices, so RESTART BAR returns to the section's
// order and row with the notes cut at the jump.

#include "eng_core.h"
#include "gba.h"
#include "player.h"
#include "scan.h"
#include "mod.h"
#include "rom.h"
#include "mixer.h"
#include "../engines/logik/lsplay.h"

#define LS_MAX_MODULES 64

static LsPlayer  s_ls;
static EngMixCtx s_mix;
static LsMods    s_last;
static uint8_t   s_ready;
static uint16_t  s_snapOrder, s_snapRow, s_snapTick, s_snapSpeed, s_snapBpm;
static uint32_t  s_mods[LS_MAX_MODULES], s_bank;
static unsigned  s_modCount, s_bankSounds;

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
    s_mix.rom = &s_ls.rom;
    s_mix.flags = 0;                   // signed samples
    s_mix.volShift = 0;                // lsplay.c gives the driver's own levels
    eng_mixer_hooks(&h, &s_mix);
    ls_init(&s_ls, eng_cart_rom(), &h);
    s_ready = 1;
}

static void sync(void)
{
    if (!s_ready) return;
    LsMods *m = &s_ls.mods;
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
    if (!same_bytes(&s_last, m, sizeof(LsMods))) {
        copy_bytes(&s_last, m, sizeof(LsMods));
        if (s_ls.running) ls_update(&s_ls);
    }
}

static void lanes(void)
{
    int tc = 0;
    for (int n = 0; n < 16 && n < PL_TRACKS; ++n) {
        const LsTrack *s = &s_ls.tracks[n];
        Track *t = &g_tracks[n];
        int exists = s_ls.running && n < s_ls.channels;
        t->flags   = exists ? MPT_FLG_EXIST : 0;
        t->lastKey = s->key;
        t->gate    = exists && s->active;
        t->act     = s->activity;
        t->notes   = s->notes;
        if (exists) tc = n + 1;
    }
    g_plTrackCount = (uint8_t)tc;
}

static int start(int n)
{
    ready();
    unsigned i = g_songIx[n];
    if (i >= s_modCount || !s_bank) return 0;
    sync();
    s_ls.repeat = (uint8_t)(g_plRepeat && !g_plOnce);
    if (!ls_start(&s_ls, s_mods[i], s_bank)) return 0;
    copy_bytes(&s_last, &s_ls.mods, sizeof(LsMods));
    return 1;
}

static void stop(void) { if (s_ready) ls_stop(&s_ls); }
static int  running(void) { return s_ready && s_ls.running; }
static uint32_t loops(void) { return s_ls.loops; }
static int  tick(void) { ls_tick(&s_ls); return 0; }

static void snap_take(void)
{
    s_snapOrder = s_ls.order; s_snapRow = s_ls.row; s_snapTick = s_ls.tick;
    s_snapSpeed = s_ls.speed; s_snapBpm = s_ls.bpm;
}

static void snap_restore(void)
{
    if (!ls_seek(&s_ls, s_snapOrder, s_snapRow)) return;
    s_ls.speed = s_snapSpeed; s_ls.bpm = s_snapBpm;
    s_ls.tick = s_snapTick < s_ls.speed ? s_snapTick : 0;
}

// ls_tick_hz_q16 is driver ticks/s in Q16 (= 24-PPQN ticks/s); tempoI = ticks/s x 150 / 59.7275.
static uint32_t tempo_i(void)
{
    ready();
    if (!s_ls.bpm) return 0;
    return (uint32_t)(((uint64_t)ls_tick_hz_q16(&s_ls) * 164589u) >> 32);
}

static void diag(char *t, int a)
{
    // M modules, S the bank's instruments with a sample, U effects skipped, E the decoder's error.
    t[a++] = ' '; t[a++] = 'M'; a = eng_put_hex(t, a, s_modCount, 2);
    t[a++] = ' '; t[a++] = 'S'; a = eng_put_hex(t, a, s_bankSounds, 2);
    t[a++] = ' '; t[a++] = 'U'; a = eng_put_hex(t, a, s_ls.unsupported, 8);
    t[a++] = ' '; t[a++] = 'E'; t[a++] = s_ls.error ? '1' : '0';
    t[a] = 0;
}

static int put(char *t, int a, const char *s) { while (*s) t[a++] = *s++; t[a] = 0; return a; }

int lsp_scan_note(char *t)
{
    int a = put(t, 0, s_modCount ? "MODPLAY: NO SAMPLE BANK M" : "MODPLAY: NO GBAMOD30 MODULES");
    if (s_modCount) eng_put_hex(t, a, s_modCount, 2);
    return 1;
}

static const EngCore s_core = {
    start, stop, running, tick, loops, snap_take, snap_restore, 0, lanes, sync, tempo_i, diag, 0,
};

// No envelopes or LFOs to mangle; the rest is the shared set.
#define LS_UNSUPPORTED ((1u << DEST_ATTACK) | (1u << DEST_DECAY) | (1u << DEST_SUSTAIN) | \
                        (1u << DEST_RELEASE) | (1u << DEST_LENGTH) | (1u << DEST_VIBRATO) | \
                        (1u << DEST_TREMOLO) | (1u << DEST_LFO))

const Engine g_engLogik = {
    "MODPLAY", ENG_ID_LOGIK, LS_UNSUPPORTED,
    tr_start, tr_stop, tr_running, tr_tick, tr_update, tr_frame, tr_tempo_i, tr_restart_loop,
    tr_restart_bar, tr_queue_restart, tr_queue_start, tr_reroll, &s_core,
};

// ---- discovery: modules by their magic, then the bank ------------------------------------------

void lsp_discover_reset(void) { s_modCount = 0; s_bank = 0; s_bankSounds = 0; }

// The pass's word loop, in IWRAM: "GBAM" "OD30" at a word, then the full check.
IWRAM_CODE static void ls_words(const EngRom *r, uint32_t begin, uint32_t end)
{
    uint32_t base = r->base, size = r->size;
    if (size < 8) return;
    uint32_t stop = base + size - 4;
    if (end > stop) end = stop;
    if (end <= begin) return;
    const volatile uint32_t *p = (const volatile uint32_t *)ROM_PTR(begin);
    uint32_t n = (end - begin) >> 2;
    for (uint32_t i = 0; i < n && s_modCount < LS_MAX_MODULES; i++) {
        if (p[i] != 0x4D414247u || p[i + 1] != 0x3033444Fu) continue;
        uint32_t a = begin + (i << 2);
        if (ls_module_valid(r, a)) s_mods[s_modCount++] = a;
    }
}

void lsp_discover(uint32_t begin, uint32_t end)
{
    EngRom r = eng_cart_rom();
    ls_words(&r, begin, end);
}

// Where a module's data ends (its last pattern's last byte + 1).
static uint32_t module_end(const EngRom *r, uint32_t m)
{
    (void)r;
    uint32_t np = rom_u32(m + 0x1B1C), a = m + 0x2034;
    for (uint32_t i = 0; i < np; i++) a += rom_u32(m + 0x1C30 + 4 * i);
    return a;
}

void lsp_discover_finish(void)
{
    if (!s_modCount) return;
    EngRom r = eng_cart_rom();
    uint32_t used[4] = { 0, 0, 0, 0 }, u[4];
    for (unsigned i = 0; i < s_modCount; i++)
        if (ls_module_instruments(&r, s_mods[i], u)) for (int k = 0; k < 4; k++) used[k] |= u[k];
    // Right after a module first (word-aligned), then every word within 1 MB of the modules.
    uint32_t lo = s_mods[0], hi = 0;
    for (unsigned i = 0; i < s_modCount && !s_bank; i++) {
        uint32_t e = (module_end(&r, s_mods[i]) + 3) & ~3u;
        if (e > hi) hi = e;
        unsigned n = ls_bank_valid(&r, e, used);
        if (n) { s_bank = e; s_bankSounds = n; }
    }
    if (!s_bank) {
        uint32_t from = lo - r.base > 0x100000 ? lo - 0x100000 : r.base;
        uint32_t to = hi + 0x100000 < r.base + r.size - LS_BANK_SIZE ? hi + 0x100000 : r.base + r.size - LS_BANK_SIZE;
        for (uint32_t a = from; a < to && !s_bank; a += 4) {
            uint32_t e0 = rom_u16(a), v0 = rom_u16(a + 4);
            if (!e0 || v0 > 64 || rom_u16(a + 6) + rom_u16(a + 8) > e0) continue;
            unsigned n = ls_bank_valid(&r, a, used);
            if (n) { s_bank = a; s_bankSounds = n; }
        }
    }
    if (s_bank) scan_add_table(ENG_ID_LOGIK, s_mods[0]);
}

// Every module, a song: music if it loops (a position jump back), else an effect or a jingle.
int lsp_table_songs(const ScanResult *t, uint16_t *ix, uint8_t *kind, int max)
{
    (void)t;
    EngRom r = eng_cart_rom();
    int k = 0;
    if (!s_bank) return 0;
    for (unsigned i = 0; i < s_modCount && k < max; i++) {
        uint32_t ticks;
        int loops;
        if (!ls_module_shape(&r, s_mods[i], &ticks, &loops)) continue;
        ix[k] = (uint16_t)i;
        kind[k++] = eng_kind(ticks, loops);
    }
    return k;
}
