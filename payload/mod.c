// mod.c — the modulator: inputs -> matrix -> g_mods, plus songs, the follower and the buttons.
// See mod.h for the model and the factory map.

#include "mod.h"
#include "player.h"
#include "clock.h"
#include "scan.h"
#include "rom.h"
#include "link.h"
#include "mixer.h"
#include "gba.h"

#define REG_KEYINPUT R16(0x0130)

const char *src_name[SRC_COUNT]   = { "CV1", "CV2", "AU1", "AU2", "PU2", "MAIN", "X", "Y" };
const char *dest_name[DEST_COUNT] = {
    "---", "CLOCK", "RESET", "NEXT SONG", "SONG", "VOICE SHIFT", "TABLE OFFSET", "SAMPLE START",
    "ROM SCRUB", "LAYERS", "SOLO", "TRANSPOSE", "FINE PITCH", "TEMPO", "REVERSE", "CRUSH",
};
const char *swu_name[SWU_COUNT] = { "NOTHING", "BYPASS", "PAUSE" };
const char *swd_name[SWD_COUNT] = { "NOTHING", "RESTART", "NEXT SONG" };
const uint32_t scrub_len[SCRUB_LEN_COUNT] = { 1024, 2048, 4096, 8192, 16384, 32768, 65536 };

#define KIND_EDGE 0
#define KIND_POS  1
#define KIND_BIP  2
static const uint8_t s_kind[DEST_COUNT] = {
    KIND_BIP,  KIND_EDGE, KIND_EDGE, KIND_EDGE, KIND_BIP, KIND_BIP, KIND_BIP, KIND_POS,
    KIND_BIP,  KIND_POS,  KIND_POS,  KIND_BIP,  KIND_BIP, KIND_BIP, KIND_POS, KIND_POS,
};

Patch    g_patch;
int      g_song    = -1;
uint16_t g_uiMute  = 0;
uint8_t  g_uiSolo  = 0xFF;
int32_t  g_destVal[DEST_COUNT];
uint8_t  g_bypass  = 0;
uint8_t  g_srcHigh = 0;
uint16_t g_btn     = 0;

static uint16_t s_presses;
static uint32_t s_nextRepeat;
static uint32_t s_gateSeen, s_audSeen[2];
static uint8_t  s_audInit;
static uint8_t  s_lastSwitch = 1;
static int      s_songBase;               // the song SONG CV is relative to
static int      s_songCvOff;

// The follower (CV Out 2 / CV Out 1).
static uint32_t s_followNotes;
static uint8_t  s_outKey = 60, s_outGate = 0;
static uint32_t s_retrigUntil;
static uint8_t  s_retrig;

// Schmitt trigger thresholds, in input counts: up through about +1 V, down through about +0.5 V.
// The same pair the host uses for the audio inputs (main.cpp), so an edge means the same thing
// whichever jack it arrives on.
#define EDGE_UP   341
#define EDGE_DOWN 170

// Auto-repeat for held d-pad and shoulder buttons, in Timer 2 counts.
#define REPEAT_DELAY (16384u * 2 / 5)       // 400 ms
#define REPEAT_RATE  (16384u / 12)          // ~83 ms
#define REPEAT_KEYS  (GBA_KEY_UP | GBA_KEY_DOWN | GBA_KEY_LEFT | GBA_KEY_RIGHT | GBA_KEY_L | GBA_KEY_R)
#define GBA_KEY_RIGHT (1u << 4)
#define GBA_KEY_LEFT  (1u << 5)
#define GBA_KEY_UP    (1u << 6)
#define GBA_KEY_DOWN  (1u << 7)
#define GBA_KEY_R     (1u << 8)
#define GBA_KEY_L     (1u << 9)

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---- patch ---------------------------------------------------------------------------------------

void mod_default_patch(void)
{
    volatile uint8_t *p = (volatile uint8_t *)&g_patch;
    for (unsigned i = 0; i < sizeof(g_patch); i++) p[i] = 0;
    g_patch.magic    = PATCH_MAGIC;
    g_patch.version  = PATCH_VERSION;
    g_patch.clkMode  = CLK_AUTO;
    g_patch.ppqnIx   = 2;                 // 4 PPQN
    g_patch.maxChans = 8;
    g_patch.gain     = 16;
    g_patch.follow   = 0;
    g_patch.repeat   = 1;
    g_patch.scrubLenIx = 2;               // 4096 samples
    g_patch.swUp     = SWU_BYPASS;
    g_patch.swDown   = SWD_RESET;
    g_patch.cartSafe = 0;

    static const uint8_t factory[SRC_COUNT] = {
        DEST_VSHIFT, DEST_TRANS, DEST_RESET, DEST_START, DEST_CLOCK, DEST_LAYERS, DEST_TEMPO, DEST_SCRUB,
    };
    for (int s = 0; s < SRC_COUNT; s++) {
        g_patch.mod[s].dest  = factory[s];
        g_patch.mod[s].depth = 63;
    }
}

int mod_patch_valid(const Patch *p)
{
    if (p->magic != PATCH_MAGIC || p->version != PATCH_VERSION) return 0;
    for (int s = 0; s < SRC_COUNT; s++) {
        if (p->mod[s].dest >= DEST_COUNT || p->mod[s].depth < -64 || p->mod[s].depth > 63) return 0;
    }
    return p->clkMode <= CLK_EXT && p->ppqnIx < CLK_PPQN_COUNT && p->maxChans >= 1
        && p->maxChans <= PL_DS_MAX && p->follow < PL_TRACKS && p->scrubLenIx < SCRUB_LEN_COUNT
        && p->swUp < SWU_COUNT && p->swDown < SWD_COUNT;
}

void mod_patch_applied(void)
{
    g_clkMode     = g_patch.clkMode;
    g_clkPpqnIx   = g_patch.ppqnIx;
    g_plMaxChans  = g_patch.maxChans;
    g_plRepeat    = g_patch.repeat;
    g_mixGain     = g_patch.gain;
    g_mods.scrubLen = scrub_len[g_patch.scrubLenIx];
    rom_set_speed(g_patch.cartSafe ? ROM_SPEED_SAFE : ROM_SPEED_FAST);
}

void mod_patch_prepare_save(void)
{
    for (int i = 0; i < 4; i++) g_patch.game[i] = g_romCode[i];
    g_patch.song = (uint16_t)(g_song < 0 ? 0 : g_song);
    g_patch.mute = g_uiMute;
}

void mod_patch_loaded(void)
{
    mod_patch_applied();
    int same = 1;
    for (int i = 0; i < 4; i++) if (g_patch.game[i] != g_romCode[i]) same = 0;
    if (same && g_patch.song < g_songCount) {
        mod_song_select(g_patch.song);
        g_uiMute = g_patch.mute;
    }
}

// ---- songs ---------------------------------------------------------------------------------------

void mod_restart(void)
{
    if (g_song < 0) return;
    player_start(scan_song_entry(g_song));
    clock_reset();
}

static void play_song(int n)
{
    if (g_songCount == 0) { g_song = -1; player_stop(); return; }
    while (n < 0) n += g_songCount;
    while (n >= g_songCount) n -= g_songCount;
    g_song = n;
    g_uiMute = 0;
    g_uiSolo = 0xFF;
    mod_restart();
}

void mod_song_select(int n)
{
    play_song(n);
    s_songBase  = g_song;
    s_songCvOff = 0;
}

void mod_song_step(int delta) { mod_song_select(g_song + delta); }

// ---- buttons -------------------------------------------------------------------------------------

uint16_t mod_take_presses(void)
{
    uint16_t p = s_presses;
    s_presses = 0;
    return p;
}

static void read_buttons(uint32_t now)
{
    uint16_t held = (uint16_t)((~REG_KEYINPUT) & 0x03FFu);
    uint16_t down = held & (uint16_t)~g_btn;
    s_presses |= down;
    if (held & REPEAT_KEYS) {
        if (down & REPEAT_KEYS) s_nextRepeat = now + REPEAT_DELAY;
        else if ((int32_t)(now - s_nextRepeat) >= 0) {
            s_presses |= held & REPEAT_KEYS;
            s_nextRepeat = now + REPEAT_RATE;
        }
    }
    g_btn = held;
    link_set_buttons(held);
}

// ---- the matrix ----------------------------------------------------------------------------------

// 2^(v/1024) x 256 for v in -2048..2047: x0.25 .. x4, the integer part a shift and the fraction a
// straight line (within 6 % of the curve, and monotonic, which is what a tempo knob needs).
static uint32_t tempo_scale(int32_t v)
{
    int32_t e = v >> 10;                                // -2 .. 1 (arithmetic shift)
    uint32_t m = 256u + (((uint32_t)(v & 1023) * 256u) >> 10);
    return e >= 0 ? (m << e) : (m >> -e);
}

// The ROM SCRUB curve: |v| 0..2047 -> 0 .. 16 MB, exponential - one octave of offset per 64 counts -
// so the start of a knob's travel moves by bytes (a sample slips against itself), the middle by
// kilobytes (into neighbouring samples) and the end by megabytes (another part of the cart).
static int32_t scrub_offset(int32_t v)
{
    uint32_t a = (uint32_t)(v < 0 ? -v : v);
    if (a == 0) return 0;
    uint32_t e = a >> 6;                                // 0..31
    uint32_t off = (e > 23) ? 0x01000000u : ((128u + ((a & 63u) << 1)) << e) >> 7;
    return v < 0 ? -(int32_t)off : (int32_t)off;
}

void mod_init(void)
{
    mod_default_patch();
    mod_patch_applied();
    s_gateSeen = g_gateEdges;
    s_audInit  = 0;
}

void mod_update(void)
{
    uint32_t now = clock_now();
    read_buttons(now);

    // ---- sources: values, centred on 0, and edges ----
    int32_t v[SRC_COUNT];
    for (int i = 0; i < 4; i++) v[i] = (int32_t)g_in[i] - 2048;
    v[SRC_PU2]  = g_gate ? 2047 : 0;
    v[SRC_MAIN] = (int32_t)g_knob[0] - 2048;
    v[SRC_X]    = (int32_t)g_knob[1] - 2048;
    v[SRC_Y]    = (int32_t)g_knob[2] - 2048;

    uint8_t edges[SRC_COUNT];
    for (int s = 0; s < SRC_COUNT; s++) {
        // Schmitt trigger on every value. It is the edge source for anything without its own
        // 48 kHz edge counter, and its level is what the MAP page shows.
        uint8_t bit = (uint8_t)(1u << s);
        edges[s] = 0;
        if (!(g_srcHigh & bit) && v[s] > EDGE_UP) { g_srcHigh |= bit; edges[s] = 1; }
        else if ((g_srcHigh & bit) && v[s] < EDGE_DOWN) g_srcHigh &= (uint8_t)~bit;
    }
    // Pulse In 2 and (when the host counts them) Audio In 1/2 have 48 kHz edge COUNTERS: a 1 ms
    // trigger between two stream words still counts. Work in deltas, capped so a resync after a
    // dropped link cannot fire a burst.
    {
        uint32_t d = g_gateEdges - s_gateSeen;
        s_gateSeen = g_gateEdges;
        edges[SRC_PU2] = (uint8_t)(d > 8 ? 1 : d);
    }
    if (g_hostCaps & GBA_CAP_AUD_EDGES) {
        if (!s_audInit) { s_audSeen[0] = g_audEdges[0]; s_audSeen[1] = g_audEdges[1]; s_audInit = 1; }
        for (int i = 0; i < 2; i++) {
            uint32_t d = g_audEdges[i] - s_audSeen[i];
            s_audSeen[i] = g_audEdges[i];
            edges[SRC_AU1 + i] = (uint8_t)(d > 8 ? 1 : d);
        }
    }

    // ---- destinations: sum, in v x depth units ----
    int32_t  sum[DEST_COUNT];
    uint8_t  mapped[DEST_COUNT];
    uint8_t  edgeN[DEST_COUNT];
    for (int d = 0; d < DEST_COUNT; d++) { sum[d] = 0; mapped[d] = 0; edgeN[d] = 0; }
    for (int s = 0; s < SRC_COUNT; s++) {
        uint8_t d = g_patch.mod[s].dest;
        if (d == DEST_NONE || d >= DEST_COUNT) continue;
        mapped[d] = 1;
        if (s_kind[d] == KIND_EDGE) edgeN[d] = (uint8_t)(edgeN[d] + edges[s]);
        else sum[d] += v[s] * g_patch.mod[s].depth;
    }
    for (int d = 0; d < DEST_COUNT; d++) {
        int32_t x = clampi(sum[d] >> 6, -2048, 2047);
        if (s_kind[d] == KIND_POS && x < 0) x = 0;
        g_destVal[d] = x;
    }

    // ---- the switch ----
    uint8_t sw = g_switch;
    g_bypass = (sw == 2 && g_patch.swUp == SWU_BYPASS);
    g_clkRun = !(sw == 2 && g_patch.swUp == SWU_PAUSE);
    if (sw == 0 && s_lastSwitch != 0) {
        if (g_patch.swDown == SWD_RESET) edgeN[DEST_RESET]++;
        else if (g_patch.swDown == SWD_NEXT) edgeN[DEST_NEXT]++;
    }
    s_lastSwitch = sw;

    // ---- edges: song changes and resets first, then the clock, so a reset and the pulse it was
    //      patched alongside land as "restart, then tick 0" ----
    if (g_song >= 0) {
        if (edgeN[DEST_NEXT]) mod_song_step(1);
        else if (edgeN[DEST_RESET]) mod_restart();
        if (mapped[DEST_SONG]) {
            int off = (int)((g_destVal[DEST_SONG] * 16) >> 11);    // +-16 songs
            if (off != s_songCvOff) {
                s_songCvOff = off;
                play_song(s_songBase + off);
            }
        }
    }
    for (uint8_t n = edgeN[DEST_CLOCK]; n; n--) clock_pulse();

    // ---- the mangles ----
    int bypass = g_bypass;
    int32_t *dv = g_destVal;
    g_mods.voiceShift    = (int16_t)(bypass ? 0 : dv[DEST_VSHIFT] >> 5);       // +-64
    g_mods.tableOffset   = (int16_t)(bypass ? 0 : dv[DEST_TABLE]);             // +-2048 bytes
    g_mods.sampleStart   = (uint8_t)(bypass ? 0 : dv[DEST_START] >> 3);        // 0..255
    g_mods.reverseChance = (uint8_t)(bypass ? 0 : dv[DEST_REVERSE] >> 3);
    g_mods.romScrub      = bypass ? 0 : scrub_offset(dv[DEST_SCRUB]);

    // Transpose: 1V/oct at depth 63. One volt is 341.33 counts, so a semitone at depth 63 is
    // 341.33 x 63 / 12 = 1792 in v x depth units - a constant divisor, rounded to nearest.
    int32_t tr = sum[DEST_TRANS];
    int32_t semis = (tr >= 0 ? tr + 896 : tr - 896) / 1792;
    int32_t pitch = bypass ? 0 : semis * 256 + (dv[DEST_PITCH] >> 3);
    g_mods.pitch  = (int16_t)clampi(pitch, -32767, 32767);

    g_mods.tempoU = (uint16_t)tempo_scale(bypass ? 0 : dv[DEST_TEMPO]);

    uint32_t c = bypass ? 0 : (uint32_t)dv[DEST_CRUSH] >> 7;                // 0..15
    g_crushHold = (uint8_t)(1u + (c >> 1));
    g_crushMask = (uint8_t)(0xFFu << (c >> 2));

    // ---- mute: the PLAY page, then LAYERS and SOLO ----
    int tc = g_plTrackCount ? g_plTrackCount : 1;
    uint16_t all  = (uint16_t)((1u << tc) - 1u);
    uint16_t mute = g_uiMute;
    if (g_uiSolo < PL_TRACKS) mute |= (uint16_t)(all & ~(1u << g_uiSolo));
    if (!bypass && mapped[DEST_LAYERS] && dv[DEST_LAYERS] > 0) {
        // 0 keeps every track; full scale keeps only track 1.
        int keep = tc - (int)(((uint32_t)dv[DEST_LAYERS] * (uint32_t)tc) >> 11);
        if (keep < 1) keep = 1;
        mute |= (uint16_t)(all & ~((1u << keep) - 1u));
    }
    if (!bypass && mapped[DEST_SOLO] && dv[DEST_SOLO] > 0) {
        int k = (int)(((uint32_t)dv[DEST_SOLO] * (uint32_t)tc) >> 11);
        if (k >= tc) k = tc - 1;
        mute |= (uint16_t)(all & ~(1u << k));
    }
    g_mods.mute = mute;

    // ---- the follower: CV Out 2 pitch, CV Out 1 gate ----
    {
        const Track *t = &g_tracks[g_patch.follow & 15];
        int key = clampi(t->lastKey + (g_mods.pitch >> 8), 0, 127);
        uint8_t gate = (t->flags & MPT_FLG_EXIST) && t->gate;
        // A new note while the gate is already high: drop the gate for 3 ms so an envelope
        // downstream retriggers. Without this, legato notes would be one long gate.
        if (t->notes != s_followNotes) {
            s_followNotes = t->notes;
            if (s_outGate) { s_retrig = 1; s_retrigUntil = now + 49; }
        }
        if (s_retrig && (int32_t)(now - s_retrigUntil) >= 0) s_retrig = 0;
        if (s_retrig) gate = 0;
        if ((uint8_t)key != s_outKey || gate != s_outGate) {
            s_outKey = (uint8_t)key;
            s_outGate = gate;
            link_post_note(s_outKey, gate ? 100 : 0);
        }
    }

    link_post_pulse(g_plBeats, g_plLoops);
}
