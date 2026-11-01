// eng_musyx.c — MusyX (Factor 5) behind the shared transport. See eng_core.h.
//
// 96 PPQN native, so one tick is four of its. It reads the mods live (pitch and volume are
// recomputed on every update), so a bar snapshot only holds the sequence: the tracks, the tick
// count, the tempo and its dice. Voices keep ringing across a jump, as MP2K's do.

#include "eng_core.h"
#include "player.h"
#include "scan.h"
#include "mod.h"
#include "../engines/musyx/musyx.h"

static MsPlayer  s_ms;
static EngMixCtx s_mix;
static MsMods    s_last;               // the mods last pushed, to update only on a change
static uint8_t   s_ready;
static MsTrack   s_snap[MS_TRACKS];
static uint32_t  s_snapTicks, s_snapRng;
static uint16_t  s_snapBpm;

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
    s_mix.rom = &s_ms.rom;
    s_mix.flags = 0;                   // signed samples, like MP2K's
    s_mix.volShift = 1;                // half volume, as the trial that sounded right (Lady Sia)
    eng_mixer_hooks(&h, &s_mix);
    ms_init(&s_ms, eng_cart_rom(), &h);
    s_ready = 1;
}

static void sync(void)
{
    if (!s_ready) return;
    MsMods *m = &s_ms.mods;
    m->mute           = g_mods.mute;
    m->tempo          = g_mods.tempoU;
    m->pitch          = g_mods.pitch;
    m->voice_shift    = g_mods.voiceShift;
    m->table_offset   = g_mods.tableOffset;          // bytes into the macro table
    m->scrub          = g_mods.romScrub;
    m->sample_start   = g_mods.sampleStart;
    m->sample_end     = g_mods.sampleEnd;
    m->reverse_chance = g_mods.reverseChance;
    m->drop_chance    = g_mods.dropChance;
    if (!same_bytes(&s_last, m, sizeof(MsMods))) {
        copy_bytes(&s_last, m, sizeof(MsMods));
        if (s_ms.running) ms_update(&s_ms);
    }
}

// The lanes: MusyX's 16 note tracks (its 17th is the tempo track).
static void lanes(void)
{
    int tc = 0;
    for (int n = 0; n < 16 && n < PL_TRACKS; ++n) {
        const MsTrack *s = &s_ms.tracks[n];
        Track *t = &g_tracks[n];
        int exists = s_ms.running && s->entries;
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
    MsSong song;
    ready();
    if (!ms_song_info(&s_ms.rom, g_scan.songTable, g_songIx[n], &song)) return 0;
    sync();
    s_ms.repeat = (uint8_t)(g_plRepeat && !g_plOnce);
    if (!ms_start(&s_ms, &song)) return 0;
    copy_bytes(&s_last, &s_ms.mods, sizeof(MsMods));
    return 1;
}

static void stop(void) { if (s_ready) ms_stop(&s_ms); }
static int  running(void) { return s_ready && s_ms.running; }
static uint32_t loops(void) { return s_ms.loops; }

static int tick(void)
{
    for (int k = 0; k < 4; ++k) {
        ms_tick(&s_ms);
        if (!s_ms.running) return 0;
        if (s_ms.ticks == 0) return 1;       // ended and repeated: ms_tick restarted it at tick 0
    }
    return 0;
}

static void snap_take(void)
{
    copy_bytes(s_snap, s_ms.tracks, sizeof(s_snap));
    s_snapTicks = s_ms.ticks; s_snapBpm = s_ms.bpm; s_snapRng = s_ms.rng;
}

static void snap_restore(void)
{
    copy_bytes(s_ms.tracks, s_snap, sizeof(s_snap));
    s_ms.ticks = s_snapTicks;
    s_ms.bpm   = s_snapBpm;
    s_ms.rng   = s_snapRng;
    // A held note (no length) waits for a note-off the jump may skip: let the next tick release it.
    for (int i = 0; i < MS_VOICES; ++i) {
        MsVoice *v = &s_ms.voices[i];
        if (v->active && !v->released && !v->gate_left) v->gate_left = 1;
    }
}

// 96-PPQN ticks a second in Q16, the tempo mangle in it. A 24-PPQN tick is four, and tempoI = 24-PPQN
// ticks/s x 150 / 59.7275; together x 150 / (4 x 59.7275 x 65536) = 41147.4 / 2^32.
static uint32_t tempo_i(void)
{
    ready();
    return (uint32_t)(((uint64_t)ms_tick_hz_q16(&s_ms) * 41147u) >> 32);
}

static void diag(char *t, int a)
{
    // Unknown opcodes (two masks), warnings, and E1 once the decoder stopped on bad data.
    t[a++] = ' '; t[a++] = 'U'; a = eng_put_hex(t, a, s_ms.unsupported[0], 8);
    t[a++] = ':'; a = eng_put_hex(t, a, s_ms.unsupported[1], 8);
    t[a++] = ' '; t[a++] = 'W'; a = eng_put_hex(t, a, s_ms.warnings, 2);
    t[a++] = ' '; t[a++] = 'E'; t[a++] = s_ms.error ? '1' : '0';
    t[a] = 0;
}

static const EngCore s_core = {
    start, stop, running, tick, loops, snap_take, snap_restore, 0, lanes, sync, tempo_i, diag,
};

// MP2K's own envelopes, note length and LFO act inside player.c; MusyX has none of them yet.
#define MX_UNSUPPORTED ((1u << DEST_ATTACK) | (1u << DEST_DECAY) | (1u << DEST_SUSTAIN) | \
                        (1u << DEST_RELEASE) | (1u << DEST_LENGTH) | (1u << DEST_VIBRATO) | \
                        (1u << DEST_TREMOLO) | (1u << DEST_LFO))

const Engine g_engMusyx = {
    "MUSYX", ENG_ID_MUSYX, MX_UNSUPPORTED,
    tr_start, tr_stop, tr_running, tr_tick, tr_update, tr_frame, tr_tempo_i, tr_restart_loop,
    tr_restart_bar, tr_queue_restart, tr_queue_start, tr_reroll, &s_core,
};

// ---- discovery: root candidates (structural; MusyX leaves no text), kept if a song resolves ----

void mx_discover_reset(void) {}

void mx_discover(uint32_t begin, uint32_t end)
{
    EngRom r = eng_cart_rom();
    uint32_t roots[8];
    unsigned n = ms_scan(&r, begin, end, roots, 8);
    for (unsigned i = 0; i < n; i++) {
        MsCatalogue c;
        MsSong s;
        if (ms_catalogue(&r, roots[i], &c) && c.songs && ms_song_info(&r, roots[i], 0, &s))
            scan_add_table(ENG_ID_MUSYX, roots[i]);
    }
}

void mx_discover_finish(void) {}

int mx_table_songs(const ScanResult *t, uint16_t *ix, uint8_t *kind, int max)
{
    EngRom r = eng_cart_rom();
    MsCatalogue c;
    int k = 0;
    if (!ms_catalogue(&r, t->songTable, &c)) return 0;
    for (unsigned i = 0; i < c.songs && k < max; i++) {
        uint32_t ticks96;
        int loops;
        if (!ms_song_shape(&r, t->songTable, i, &ticks96, &loops)) continue;
        ix[k] = (uint16_t)i;
        kind[k++] = eng_kind(ticks96 >> 2, loops);
    }
    return k;
}
