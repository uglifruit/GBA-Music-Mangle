// mod.c — the modulator: inputs + buttons + base values -> g_mods, plus songs, the follower and
// the button sampling. See mod.h for the model and the factory settings.

#include "mod.h"
#include "player.h"
#include "clock.h"
#include "scan.h"
#include "rom.h"
#include "link.h"
#include "mixer.h"
#include "gba.h"

#define REG_KEYINPUT R16(0x0130)

#define KEY_A      (1u << 0)
#define KEY_B      (1u << 1)
#define KEY_RIGHT  (1u << 4)
#define KEY_LEFT   (1u << 5)
#define KEY_UP     (1u << 6)
#define KEY_DOWN   (1u << 7)
#define KEY_R      (1u << 8)
#define KEY_L      (1u << 9)

const char *src_name[SRC_COUNT]   = { "CV1", "CV2", "AU1", "AU2", "PU2", "MAIN", "X", "Y" };
const char *dest_name[DEST_COUNT] = {
    "---", "CLOCK", "RESET", "NEXT SONG", "SONG", "VOICE SHIFT", "TABLE OFFSET", "START / END",
    "ROM SCRUB", "LAYERS", "SOLO / MUTE", "TRANSPOSE", "FINE PITCH", "TEMPO", "REVERSE/DROP", "CRUSH",
    "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "NOTE LENGTH", "REVERB", "VIBRATO/TRILL", "TREMOLO/GATE",
    "LFO SPEED", "CLEAR MANGLES", "CHAOS", "REROLL CHAOS", "RESTART LOOP", "RESTART BAR",
};
const char *dest_short[DEST_COUNT] = {
    "---", "CLK", "RST", "NXT", "SNG", "VSH", "TBL", "SST", "SCR", "LAY", "SOL", "TRN", "PIT",
    "TMP", "REV", "CRU", "ATK", "DCY", "SUS", "REL", "LEN", "RVB", "VIB", "TRM", "LFO", "CLR",
    "CHS", "RRL", "RLP", "RBR",
};
const char *btn_mode_name[BTN_MODES] = { "HOLD", "LATCH", "STEP" };
const char *btn_name[BTN_SLOTS] = { "A", "B", "L", "R", "UP", "DOWN", "LEFT", "RIGHT" };
const uint16_t btn_key[BTN_SLOTS] = { KEY_A, KEY_B, KEY_L, KEY_R, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
const char *swu_name[SWU_COUNT] = { "NOTHING", "BYPASS", "PAUSE", "REPEAT BAR" };
const char *swd_name[SWD_COUNT] = { "NOTHING", "RESTART", "NEXT SONG" };
const uint32_t scrub_len[SCRUB_LEN_COUNT] = { 1024, 2048, 4096, 8192, 16384, 32768, 65536 };
const uint16_t bar_len[BAR_LEN_COUNT] = { 24, 48, 96, 192, 384 };
const char *bar_len_name[BAR_LEN_COUNT] = { "1 BEAT", "2 BEATS", "1 BAR", "2 BARS", "4 BARS" };

#define KIND_EDGE 0
#define KIND_POS  1
#define KIND_BIP  2
// Every value destination is BIP since 2026-10-27: each one-sided mangle got a meaning for its
// negative half (START/END, LAYERS from the bottom, SOLO/MUTE, REVERSE/DROP, rate/bit CRUSH,
// TRILL, GATE, free CHAOS). KIND_POS stays for any future destination that has no other side.
static const uint8_t s_kind[DEST_COUNT] = {
    KIND_BIP,  KIND_EDGE, KIND_EDGE, KIND_EDGE, KIND_BIP, KIND_BIP, KIND_BIP, KIND_BIP,
    KIND_BIP,  KIND_BIP,  KIND_BIP,  KIND_BIP,  KIND_BIP, KIND_BIP, KIND_BIP, KIND_BIP,
    KIND_BIP,  KIND_BIP,  KIND_BIP,  KIND_BIP,  KIND_BIP, KIND_BIP, KIND_BIP, KIND_BIP,
    KIND_BIP,  KIND_EDGE, KIND_BIP,  KIND_EDGE, KIND_EDGE, KIND_EDGE,
};

// BASE ranges, in each destination's own units (see the BASE page and mod_update):
//   VSHIFT programs, TABLE bytes, START /128 of the sample, SCRUB a virtual knob, LAYERS tracks
//   stripped, SOLO track (0 = none), TRANS semitones, PITCH cents, TEMPO a virtual knob, REVERSE
//   /128 chance, CRUSH steps; ATTACK DECAY SUSTAIN RELEASE LENGTH and LFO SPEED virtual knobs,
//   REVERB level added to the song's, VIBRATO and TREMOLO depth, CHAOS amount (x2 = 0..254).
static const int8_t s_baseMin[DEST_COUNT] = { 0, 0, 0, 0, 0, -64, -127, -127, -127, -15, -16, -48, -100, -127, -127, -15,
                                              -127, -127, -127, -127, -127, -127, -63, -63, -127, 0, -127, 0, 0, 0 };
static const int8_t s_baseMax[DEST_COUNT] = { 0, 0, 0, 0, 0,  63,  127, 127,  127, 15, 16,  48,  100,  127, 127, 15,
                                               127,  127,  127,  127,  127,  127, 63, 63,  127, 0, 127, 0, 0, 0 };

int dest_is_edge(int d)  { return d > 0 && d < DEST_COUNT && s_kind[d] == KIND_EDGE; }
int dest_has_base(int d) { return d > 0 && d < DEST_COUNT && s_baseMax[d] != s_baseMin[d]; }
int base_min(int d)      { return (d >= 0 && d < DEST_COUNT) ? s_baseMin[d] : 0; }
int base_max(int d)      { return (d >= 0 && d < DEST_COUNT) ? s_baseMax[d] : 0; }

Patch    g_patch;
// The whole Patch rides in one 256-byte flash slot (gba_proto.h). Appending is how it grows, so
// this is the backstop that says when it no longer can.
_Static_assert(sizeof(Patch) <= GBA_PATCH_SLOT_BYTES, "Patch no longer fits a flash slot");
// The host reads the cart tag at a fixed offset (GBA_OP_SLOT_TAG); it must never move.
_Static_assert(__builtin_offsetof(Patch, cartTag) == GBA_PATCH_TAG_OFFSET, "cartTag moved");
_Static_assert(PATCH_TABLES == SCAN_MAX_TABLES, "the saved table list must hold every table");
_Static_assert(DEST_COUNT <= DEST_MAX, "more destinations than Patch.base[] holds: append a new array");
int      g_song    = -1;
uint16_t g_uiMute  = 0;
uint8_t  g_uiSolo  = 0xFF;
int32_t  g_destVal[DEST_COUNT];
uint8_t  g_bypass  = 0;
uint8_t  g_srcHigh = 0;
uint8_t  g_perform = 0;
uint8_t  g_btnLatch = 0;
uint16_t g_btn     = 0;

static uint16_t s_presses;
static uint16_t s_perfDown;               // presses not yet seen by the BTN map
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

// Dead band around 0 for the value sources, in input counts (about 70 mV, or half a percent of a
// knob's travel). An unpatched jack or a knob left at noon reads a few counts either side of 0, and
// that must mangle nothing: a voice shift of -1 from a reading of -1 swaps every instrument.
// Readings outside it are not offset, so 1V/oct transpose stays exact.
#define DEAD_BAND 24

// The knobs get a much wider detent at noon, about 15 degrees either way, and the rest of the
// travel is stretched so the ends still reach full scale. With TEMPO and ROM SCRUB on X and Y, a
// knob left roughly at noon must give the song's own tempo and clean samples: an octave of tempo
// per quarter turn made "about noon" audibly slow. Knobs carry no 1V/oct, so the stretch is free.
#define KNOB_DETENT  200
#define KNOB_STRETCH 1135                 // 2048 / (2048 - KNOB_DETENT), Q10

// Auto-repeat for held d-pad and shoulder buttons, in Timer 2 counts. Not in PERFORM mode, where a
// held button is a HOLD and a repeat would be a stream of STEPs.
#define REPEAT_DELAY (16384u * 2 / 5)       // 400 ms
#define REPEAT_RATE  (16384u / 12)          // ~83 ms
#define REPEAT_KEYS  (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R)

static inline __attribute__((always_inline)) int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---- patch ---------------------------------------------------------------------------------------

static void default_buttons(Patch *p)
{
    static const BtnSlot factory[BTN_SLOTS] = {
        { DEST_RBAR,     0, BTN_HOLD },     // A      tap to stutter
        { DEST_REROLL,   0, BTN_HOLD },     // B      a new chaos pattern
        { DEST_REVERSE, 63, BTN_HOLD },     // L
        { DEST_CRUSH,   63, BTN_HOLD },     // R
        { DEST_TRANS,    1, BTN_STEP },     // UP
        { DEST_TRANS,   -1, BTN_STEP },     // DOWN
        { DEST_CLEAR,    0, BTN_HOLD },     // LEFT   back to clean (tempo and pitch stay)
        { DEST_CHAOS,   16, BTN_STEP },     // RIGHT  chaos up, 32 a press
    };
    for (int i = 0; i < BTN_SLOTS; i++) {
        p->btn[i].dest   = factory[i].dest;
        p->btn[i].amount = factory[i].amount;
        p->btn[i].mode   = factory[i].mode;
    }
}

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
    g_patch.swUp     = SWU_REPEAT;
    g_patch.swDown   = SWD_RESET;
    g_patch.cartSafe = 0;
    g_patch.barLenIx = BAR_LEN_DEFAULT;

    static const uint8_t factory[SRC_COUNT] = {
        DEST_VSHIFT, DEST_TRANS, DEST_RESET, DEST_RBAR, DEST_CLOCK, DEST_CHAOS, DEST_TEMPO, DEST_SCRUB,
    };
    for (int s = 0; s < SRC_COUNT; s++) {
        g_patch.mod[s].dest  = factory[s];
        g_patch.mod[s].depth = 63;
    }
    default_buttons(&g_patch);
}

int mod_patch_accept(Patch *p)
{
    if (p->magic != PATCH_MAGIC || p->version < 1 || p->version > PATCH_VERSION) return 0;
    for (int s = 0; s < SRC_COUNT; s++) {
        if (p->mod[s].dest >= DEST_COUNT || p->mod[s].depth < -64 || p->mod[s].depth > 63) return 0;
    }
    if (!(p->clkMode <= CLK_EXT && p->ppqnIx < CLK_PPQN_COUNT && p->maxChans >= 1
          && p->maxChans <= PL_DS_MAX && p->follow < PL_TRACKS && p->scrubLenIx < SCRUB_LEN_COUNT
          && p->swUp < SWU_COUNT && p->swDown < SWD_COUNT))
        return 0;

    // v1 -> v2: everything appended arrived as zeros (the whole slot is sent, zero-padded), which
    // is already right for table, list and BASE; an all-zero button map would be dead buttons, so
    // it gets the factory one.
    if (p->version == 1) {
        default_buttons(p);
        p->version = 2;
    }
    // BASE has lived in three places: baseV2 (v2), baseV3 (v3, v4) and base (v5). Bring an older
    // patch's values into base[]; destinations it did not know start at 0. The cart fields of a
    // v3 arrived as zeros, which already means "no tables saved".
    if (p->version == 2 || p->version == 3 || p->version == 4) {
        int n = p->version == 2 ? DEST_COUNT_V2 : DEST_COUNT_V3;
        for (int d = 0; d < DEST_MAX; d++) p->base[d] = d < n ? (p->version == 2 ? p->baseV2[d] : p->baseV3[d]) : 0;
        for (int d = 0; d < DEST_COUNT_V2; d++) p->baseV2[d] = 0;
        for (int d = 0; d < DEST_COUNT_V3; d++) p->baseV3[d] = 0;
        p->barLenIx = BAR_LEN_DEFAULT;                    // was padding before v5
        p->restartQ = 0;
        p->version = 5;
    }
    if (p->barLenIx >= BAR_LEN_COUNT) p->barLenIx = BAR_LEN_DEFAULT;
    if (p->restartQ > 1) p->restartQ = 0;
    if (p->tableCount > PATCH_TABLES) p->tableCount = 0;

    for (int i = 0; i < BTN_SLOTS; i++) {
        if (p->btn[i].dest >= DEST_COUNT || p->btn[i].mode >= BTN_MODES) return 0;
        if (p->btn[i].amount < -64 || p->btn[i].amount > 63) return 0;
    }
    for (int d = 0; d < DEST_COUNT; d++) p->base[d] = (int8_t)clampi(p->base[d], s_baseMin[d], s_baseMax[d]);
    if (p->list >= LIST_COUNT) p->list = LIST_ALL;
    return 1;
}

void mod_patch_applied(void)
{
    g_clkMode     = g_patch.clkMode;
    g_clkPpqnIx   = g_patch.ppqnIx;
    g_plMaxChans  = g_patch.maxChans;
    g_plRepeat    = g_patch.repeat;
    g_mixGain     = g_patch.gain;
    g_mods.scrubLen = scrub_len[g_patch.scrubLenIx];
    g_plBarTicks  = bar_len[g_patch.barLenIx < BAR_LEN_COUNT ? g_patch.barLenIx : BAR_LEN_DEFAULT];
    rom_set_speed(g_patch.cartSafe ? ROM_SPEED_SAFE : ROM_SPEED_FAST);
}

void mod_patch_prepare_save(void)
{
    for (int i = 0; i < 4; i++) g_patch.game[i] = g_romCode[i];
    g_patch.song  = (uint16_t)(g_song < 0 ? 0 : g_song);
    g_patch.mute  = g_uiMute;
    g_patch.table = g_tableIx;
    g_patch.list  = g_listMode;
    // The cart and where its music is, so the next boot on this cart can skip the scan.
    g_patch.cartTag    = rom_cart_tag();
    g_patch.romVersion = g_romVersion;
    g_patch.romCheck   = g_romCheck;
    int n = (g_scan.method == SCAN_NONE) ? 0 : g_tableCount;
    g_patch.tableCount = (uint8_t)n;
    for (int i = 0; i < PATCH_TABLES; i++) {
        g_patch.tableAddr[i] = i < n ? g_tables[i].songTable : 0;
        g_patch.tableRate[i] = i < n ? g_tables[i].gameRate : 0;
    }
}

int mod_patch_is_this_cart(const Patch *p)
{
    if (!g_romSize || p->cartTag != rom_cart_tag()) return 0;
    if (p->romVersion != g_romVersion || p->romCheck != g_romCheck) return 0;
    for (int i = 0; i < 4; i++) if (p->game[i] != g_romCode[i]) return 0;
    return 1;
}

void mod_patch_loaded(void)
{
    mod_patch_applied();
    int same = 1;
    for (int i = 0; i < 4; i++) if (g_patch.game[i] != g_romCode[i]) same = 0;
    if (!same) return;
    if (g_patch.table < g_tableCount && (g_patch.table != g_tableIx || g_patch.list != g_listMode)) {
        scan_select(g_patch.table, g_patch.list);
        g_plGameRate = g_scan.gameRate;          // the table's own mixing rate, as mod_set_list
    }
    if (g_patch.song < g_songCount) {
        mod_song_select(g_patch.song);
        g_uiMute = g_patch.mute;
    }
}

// CLEAR MANGLES: every BASE value back to 0 and every latched button off - except where the song
// sits rather than how it is mangled: TEMPO, TRANSPOSE and FINE PITCH. A CV or a knob cannot be
// cleared from here; they are wherever they are.
static int kept_on_clear(int d) { return d == DEST_TEMPO || d == DEST_TRANS || d == DEST_PITCH; }
static void clear_mangles(void)
{
    for (int d = 0; d < DEST_COUNT; d++) if (!kept_on_clear(d)) g_patch.base[d] = 0;
    for (int i = 0; i < BTN_SLOTS; i++)
        if (!kept_on_clear(g_patch.btn[i].dest)) g_btnLatch &= (uint8_t)~(1u << i);
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

void mod_set_list(int table, int listMode)
{
    scan_select(table, listMode);
    g_patch.table = g_tableIx;
    g_patch.list  = g_listMode;
    g_plGameRate  = g_scan.gameRate;
    mod_song_select(0);
}

// ---- buttons -------------------------------------------------------------------------------------

uint16_t mod_take_presses(void)
{
    uint16_t p = s_presses;
    s_presses = 0;
    return p;
}

static inline __attribute__((always_inline)) void read_buttons(uint32_t now)
{
    uint16_t held = (uint16_t)((~REG_KEYINPUT) & 0x03FFu);
    uint16_t down = held & (uint16_t)~g_btn;
    s_presses  |= down;
    s_perfDown |= down;
    if (!g_perform && (held & REPEAT_KEYS)) {
        if (down & REPEAT_KEYS) s_nextRepeat = now + REPEAT_DELAY;
        else if ((int32_t)(now - s_nextRepeat) >= 0) {
            s_presses |= held & REPEAT_KEYS;
            s_nextRepeat = now + REPEAT_RATE;
        }
    }
    g_btn = held;
    link_set_buttons(held);
}

// ---- curves ----------------------------------------------------------------------------------------

// 2^(v/1024) x 256 for v in -2048..2047: x0.25 .. x4, the integer part a shift and the fraction a
// straight line (within 6 % of the curve, and monotonic, which is what a tempo knob needs).
static inline __attribute__((always_inline)) uint32_t tempo_scale(int32_t v)
{
    int32_t e = v >> 10;                                // -2 .. 1 (arithmetic shift)
    uint32_t m = 256u + (((uint32_t)(v & 1023) * 256u) >> 10);
    return e >= 0 ? (m << e) : (m >> -e);
}

// The ROM SCRUB curve: |v| 0..2047 -> 0 .. 16 MB, exponential - one octave of offset per 64 counts -
// so the start of a knob's travel moves by bytes (a sample slips against itself), the middle by
// kilobytes (into neighbouring samples) and the end by megabytes (another part of the cart).
static inline __attribute__((always_inline)) int32_t scrub_offset(int32_t v)
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

// ---- the update ------------------------------------------------------------------------------------

// A knob, centred on 0, through the noon detent.
static inline __attribute__((always_inline)) int32_t knob_value(uint16_t raw)
{
    int32_t v = (int32_t)raw - 2048;
    int32_t a = v < 0 ? -v : v;
    if (a <= KNOB_DETENT) return 0;
    a = ((a - KNOB_DETENT) * KNOB_STRETCH) >> 10;
    if (a > 2047) a = 2047;
    return v < 0 ? -a : a;
}

// IWRAM: it runs every millisecond (main.c, gfx_idle). Its per-call helpers above are forced inline
// so none of them runs from EWRAM; the rare calls (song changes, the link posts) stay out there.
IWRAM_CODE void mod_update(void)
{
    uint32_t now = clock_now();
    read_buttons(now);

    // ---- sources: values, centred on 0, and edges ----
    int32_t v[SRC_COUNT];
    for (int i = 0; i < 4; i++) v[i] = (int32_t)g_in[i] - 2048;
    v[SRC_PU2]  = g_gate ? 2047 : 0;
    v[SRC_MAIN] = knob_value(g_knob[0]);
    v[SRC_X]    = knob_value(g_knob[1]);
    v[SRC_Y]    = knob_value(g_knob[2]);
    for (int s = 0; s < SRC_COUNT; s++)
        if (s != SRC_PU2 && v[s] > -DEAD_BAND && v[s] < DEAD_BAND) v[s] = 0;

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

    // ---- destinations: sum the MAP, in v x depth units ----
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

    // ---- the GBA's buttons, in PERFORM mode: one more source per slot ----
    uint16_t down = s_perfDown;
    s_perfDown = 0;
    if (!g_perform) { down = 0; g_btnLatch = 0; }
    for (int i = 0; g_perform && i < BTN_SLOTS; i++) {
        const BtnSlot *b = &g_patch.btn[i];
        uint8_t d = b->dest;
        if (d == DEST_NONE || d >= DEST_COUNT) continue;
        int pressed = (down & btn_key[i]) != 0;
        if (s_kind[d] == KIND_EDGE) {
            if (pressed) edgeN[d]++;
            continue;
        }
        if (b->mode == BTN_STEP) {
            if (pressed) g_patch.base[d] = (int8_t)clampi(g_patch.base[d] + b->amount, s_baseMin[d], s_baseMax[d]);
            continue;
        }
        if (b->mode == BTN_LATCH && pressed) g_btnLatch ^= (uint8_t)(1u << i);
        int on = (b->mode == BTN_HOLD) ? (g_btn & btn_key[i]) != 0 : (g_btnLatch >> i) & 1;
        if (on) { sum[d] += 2047 * b->amount; mapped[d] = 1; }
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
    g_plHoldBar = (uint8_t)(sw == 2 && g_patch.swUp == SWU_REPEAT);
    if (sw == 0 && s_lastSwitch != 0) {
        if (g_patch.swDown == SWD_RESET) edgeN[DEST_RESET]++;
        else if (g_patch.swDown == SWD_NEXT) edgeN[DEST_NEXT]++;
    }
    s_lastSwitch = sw;

    // ---- edges: song changes and resets first, then the clock, so a reset and the pulse it was
    //      patched alongside land as "restart, then tick 0" ----
    if (g_song >= 0) {
        // Each restart is followed by clock_reset(), as RESET's is: the restarted spot's first tick
        // plays at once (INT), or on the pulse it arrived with (EXT).
        // AT BAR END queues them instead: the player does it on the next section line, on the grid.
        int q = g_patch.restartQ;
        if (edgeN[DEST_NEXT]) mod_song_step(1);
        else if (edgeN[DEST_RESET]) { if (q) player_queue_restart(PL_RESTART_SONG); else mod_restart(); }
        else if (edgeN[DEST_RLOOP]) {
            if (q) player_queue_restart(PL_RESTART_LOOP); else { player_restart_loop(); clock_reset(); }
        } else if (edgeN[DEST_RBAR]) {
            if (q) player_queue_restart(PL_RESTART_BAR); else { player_restart_bar(); clock_reset(); }
        }
        if (mapped[DEST_SONG]) {
            int off = (int)(g_destVal[DEST_SONG] / 128);         // +-16 songs
            if (off != s_songCvOff) {
                s_songCvOff = off;
                play_song(s_songBase + off);
            }
        }
    }
    for (uint8_t n = edgeN[DEST_CLOCK]; n; n--) clock_pulse();
    if (edgeN[DEST_CLEAR]) clear_mangles();
    if (edgeN[DEST_REROLL]) player_chaos_reroll();

    // ---- the mangles: CV + buttons (dv) plus the BASE value, each in its own units ----
    int bypass = g_bypass;
    int32_t *dv = g_destVal;
    const int8_t *bs = g_patch.base;
    if (bypass) {
        g_mods.voiceShift = 0; g_mods.tableOffset = 0; g_mods.sampleStart = 0;
        g_mods.reverseChance = 0; g_mods.romScrub = 0; g_mods.pitch = 0; g_mods.tempoU = 0x100;
        g_crushHold = 1; g_crushMask = 0xFF;
        g_mods.attack = 0; g_mods.decay = 0; g_mods.sustain = 0; g_mods.release = 0; g_mods.gate = 0;
        g_mods.reverb = 0; g_mods.vibrato = 0; g_mods.tremolo = 0; g_mods.lfoSpeed = 22;
        g_mods.chaos = 0; g_mods.chaosFree = 0; g_mods.sampleEnd = 0; g_mods.dropChance = 0;
    } else {
        // Divides, not shifts: a shift floors, and -1 >> 5 is a whole program down.
        g_mods.voiceShift    = (int16_t)clampi(dv[DEST_VSHIFT] / 32 + bs[DEST_VSHIFT], -127, 127);
        g_mods.tableOffset   = (int16_t)(dv[DEST_TABLE] + bs[DEST_TABLE]);
        // Each of these two has a second meaning below zero: START cuts the END off instead, and
        // REVERSE becomes a chance to DROP the note.
        int32_t st = clampi(dv[DEST_START] / 8 + bs[DEST_START] * 2, -255, 255);
        g_mods.sampleStart   = (uint8_t)(st > 0 ? st : 0);
        g_mods.sampleEnd     = (uint8_t)(st < 0 ? -st : 0);
        int32_t rv = clampi(dv[DEST_REVERSE] / 8 + bs[DEST_REVERSE] * 2, -255, 255);
        g_mods.reverseChance = (uint8_t)(rv > 0 ? rv : 0);
        g_mods.dropChance    = (uint8_t)(rv < 0 ? -rv : 0);
        g_mods.romScrub      = scrub_offset(clampi(dv[DEST_SCRUB] + bs[DEST_SCRUB] * 16, -2047, 2047));

        // Transpose: 1V/oct at depth 63. One volt is 341.33 counts, so a semitone at depth 63 is
        // 341.33 x 63 / 12 = 1792 in v x depth units - a constant divisor, rounded to nearest.
        // Buttons add 2047 x amount to the same sum, so a HOLD of 63 is +6 V: 72 semitones.
        int32_t tr = sum[DEST_TRANS];
        int32_t semis = (tr >= 0 ? tr + 896 : tr - 896) / 1792 + bs[DEST_TRANS];
        int32_t pitch = semis * 256 + (dv[DEST_PITCH] >> 3) + (bs[DEST_PITCH] * 256) / 100;
        g_mods.pitch  = (int16_t)clampi(pitch, -32767, 32767);

        g_mods.tempoU = (uint16_t)tempo_scale(clampi(dv[DEST_TEMPO] + bs[DEST_TEMPO] * 16, -2048, 2047));

        // The sound: envelopes and note length as virtual knobs (BASE x 16, as SCRUB and TEMPO).
        g_mods.attack  = (int16_t)clampi(dv[DEST_ATTACK]  + bs[DEST_ATTACK]  * 16, -2047, 2047);
        g_mods.decay   = (int16_t)clampi(dv[DEST_DECAY]   + bs[DEST_DECAY]   * 16, -2047, 2047);
        g_mods.sustain = (int16_t)clampi(dv[DEST_SUSTAIN] + bs[DEST_SUSTAIN] * 16, -2047, 2047);
        g_mods.release = (int16_t)clampi(dv[DEST_RELEASE] + bs[DEST_RELEASE] * 16, -2047, 2047);
        g_mods.gate    = (int16_t)clampi(dv[DEST_LENGTH]  + bs[DEST_LENGTH]  * 16, -2047, 2047);
        g_mods.reverb  = (int8_t)clampi(dv[DEST_REVERB] / 16 + bs[DEST_REVERB], -127, 127);
        g_mods.vibrato = (int8_t)clampi(dv[DEST_VIBRATO] / 32 + bs[DEST_VIBRATO], -63, 63);   // - = trill
        g_mods.tremolo = (int8_t)clampi(dv[DEST_TREMOLO] / 32 + bs[DEST_TREMOLO], -63, 63);   // - = gate
        // LFO SPEED: m4a's default 22 per frame (about 5 Hz), x1/4 .. x4 on TEMPO's curve.
        g_mods.lfoSpeed = (uint8_t)clampi((int32_t)((22u * tempo_scale(clampi(dv[DEST_LFO] + bs[DEST_LFO] * 16,
                                                                             -2048, 2047))) >> 8), 1, 255);

        // CHAOS: the amount either way; below zero it is FREE (never reseeded, so never repeats).
        int32_t cz = clampi(dv[DEST_CHAOS] / 8 + bs[DEST_CHAOS] * 2, -255, 255);
        g_mods.chaos     = (uint8_t)(cz < 0 ? -cz : cz);
        g_mods.chaosFree = (uint8_t)(cz < 0);

        // CRUSH: above zero the sample rate (sample-and-hold, up to 12 samples: about 1.4 kHz),
        // below zero the bits (down to one). Each side is one effect, so they can be told apart.
        int32_t c = clampi(dv[DEST_CRUSH] / 128 + bs[DEST_CRUSH], -15, 15);
        g_crushHold = (uint8_t)(c > 0 ? 1 + ((c * 3) >> 2) : 1);
        g_crushMask = (uint8_t)(c < 0 ? 0xFFu << ((-c) >> 1) : 0xFFu);
    }

    // ---- mute: the PLAY page, then LAYERS and SOLO ----
    int tc = g_plTrackCount ? g_plTrackCount : 1;
    uint16_t all  = (uint16_t)((1u << tc) - 1u);
    uint16_t mute = g_uiMute;
    if (g_uiSolo < PL_TRACKS) mute |= (uint16_t)(all & ~(1u << g_uiSolo));
    if (!bypass) {
        // LAYERS: above zero tracks are stripped from the top (track 1 always stays), below zero
        // from the bottom (the last track always stays). Full scale strips all but that one.
        int strip = bs[DEST_LAYERS];
        if (mapped[DEST_LAYERS]) strip += (int)(dv[DEST_LAYERS] * tc / 2048);
        if (strip > 0) {
            int keep = tc - strip;
            if (keep < 1) keep = 1;
            if (keep < tc) mute |= (uint16_t)(all & ~((1u << keep) - 1u));
        } else if (strip < 0) {
            int cut = -strip;
            if (cut > tc - 1) cut = tc - 1;
            mute |= (uint16_t)((1u << cut) - 1u);
        }
        // SOLO: +n solos track n, -n MUTES track n. The BASE value picks it outright; otherwise
        // the CV level does, either way from zero.
        int k = bs[DEST_SOLO];
        if (!k && mapped[DEST_SOLO] && dv[DEST_SOLO]) {
            int32_t a = dv[DEST_SOLO] < 0 ? -dv[DEST_SOLO] : dv[DEST_SOLO];
            int n = (int)((a * tc) >> 11) + 1;
            k = dv[DEST_SOLO] > 0 ? n : -n;
        }
        if (k > tc) k = tc;
        if (k < -tc) k = -tc;
        if (k > 0) mute |= (uint16_t)(all & ~(1u << (k - 1)));
        else if (k < 0) mute |= (uint16_t)(1u << (-k - 1));
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
