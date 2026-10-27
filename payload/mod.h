// mod.h — the modulator: what the Workshop's inputs, and the GBA's own buttons, do to the player.
//
// The FM card's division of labour, unchanged: the Workshop streams its raw inputs, and every
// decision about what they MEAN lives here, in tables in GBA RAM. Three ways in, all summed:
//
//   MAP   one mapping per Workshop source (CV, audio, pulse, knobs): a destination and a depth.
//   BTN   in PERFORM mode (SELECT on the PLAY page) the GBA's buttons are sources too: each one
//         HOLDs a destination up while pressed, LATCHes it on and off, STEPs its BASE value, or -
//         for the edge destinations - fires a clock, a reset, or the next song.
//   BASE  every destination's resting value, set on the GBA in that destination's own units
//         (programs, semitones, bytes, tracks...). With nothing patched, this is how the GBA alone
//         mangles a song.
//
// Destinations come in three kinds:
//   EDGE  the source's rising edges do something (clock, reset, next song)
//   POS   only the positive half acts; at or below 0 V (or a knob at or left of noon) there is no
//         effect, so an unpatched input or a centred knob never mangles anything
//   BIP   both directions act around zero
//
// The factory map:
//   PU2  -> CLOCK          AU1 -> RESET           AU2 -> SAMPLE START
//   CV1  -> VOICE SHIFT    CV2 -> TRANSPOSE (1V/oct)
//   MAIN -> LAYERS         X   -> TEMPO           Y   -> ROM SCRUB
// (The sound destinations and CLEAR MANGLES are not in the factory map: assign them on MAP/BTN.)
// The factory buttons (PERFORM mode):
//   A hold REVERSE   B hold CRUSH   L/R step VOICE SHIFT   UP/DOWN step TRANSPOSE
//   LEFT/RIGHT step TABLE OFFSET
// Switch: UP bypasses every mangle, DOWN (momentary) restarts the song.
#pragma once

#include <stdint.h>

// ---- sources, in MAP-page order (the first four are g_in[] in LINK_IN_* order) ----
#define SRC_CV1   0
#define SRC_CV2   1
#define SRC_AU1   2
#define SRC_AU2   3
#define SRC_PU2   4
#define SRC_MAIN  5
#define SRC_X     6
#define SRC_Y     7
#define SRC_COUNT 8
extern const char *src_name[SRC_COUNT];

// ---- destinations ----
#define DEST_NONE     0
#define DEST_CLOCK    1    // EDGE: one clock pulse (PPQN on the SET page)
#define DEST_RESET    2    // EDGE: restart the song, aligned to the clock
#define DEST_NEXT     3    // EDGE: next song
#define DEST_SONG     4    // BIP:  pick a song up to 16 either side of the selected one
#define DEST_VSHIFT   5    // BIP:  voice shift, +-64 programs
#define DEST_TABLE    6    // BIP:  voice-table pointer offset, +-2048 bytes
#define DEST_START    7    // POS:  sample start point, 0..255/256 of the sample
#define DEST_SCRUB    8    // BIP:  ROM scrub, exponential, bytes .. 16 MB either way
#define DEST_LAYERS   9    // POS:  strip tracks from the top down until only track 1 is left
#define DEST_SOLO    10    // POS:  solo one track, chosen by the level
#define DEST_TRANS   11    // BIP:  transpose, 1V/oct at depth 63, quantised to semitones
#define DEST_PITCH   12    // BIP:  fine pitch, +-1 semitone, continuous
#define DEST_TEMPO   13    // BIP:  internal tempo x0.25 .. x4
#define DEST_REVERSE 14    // POS:  chance each note plays its sample backwards
#define DEST_CRUSH   15    // POS:  sample-and-hold and bit reduction on the output
// ---- added 2026-10-27 (patch v3): the sound itself, m4a's own controls turned from outside ----
#define DEST_ATTACK  16    // BIP:  envelope attack, + slower (swells) / - snappier, live
#define DEST_DECAY   17    // BIP:  envelope decay, + longer / - shorter, live
#define DEST_SUSTAIN 18    // BIP:  envelope sustain level, + higher / - lower, live
#define DEST_RELEASE 19    // BIP:  envelope release, + longer tails / - cut short, live
#define DEST_LENGTH  20    // BIP:  note length x1/4 .. x4 (staccato / legato), at note-on
#define DEST_REVERB  21    // BIP:  the engine's reverb, added to the song's own (- removes it)
#define DEST_VIBRATO 22    // POS:  the engine's LFO on pitch, depth
#define DEST_TREMOLO 23    // POS:  the same LFO on volume, depth
#define DEST_LFO     24    // BIP:  that LFO's speed, x1/4 .. x4 around m4a's default
#define DEST_CLEAR   25    // EDGE: CLEAR MANGLES - every BASE value to 0 and every latch off,
                           //       except TEMPO, TRANSPOSE and FINE PITCH
#define DEST_COUNT   26
#define DEST_COUNT_V2 16   // the destinations a v1/v2 patch knew
extern const char *dest_name[DEST_COUNT];
extern const char *dest_short[DEST_COUNT];   // three letters, for the PERFORM legend
int  dest_is_edge(int d);
int  dest_has_base(int d);                    // has a BASE value (not EDGE, not SONG, not NONE)
int  base_min(int d);
int  base_max(int d);

typedef struct {
    uint8_t dest;
    int8_t  depth;         // -64..+63
} ModSlot;

// ---- GBA buttons in PERFORM mode (BTN page) ----
#define BTN_HOLD  0        // adds `amount` at full scale while held
#define BTN_LATCH 1        // each press toggles the same on or off
#define BTN_STEP  2        // each press adds `amount` to the destination's BASE value
#define BTN_MODES 3
extern const char *btn_mode_name[BTN_MODES];

#define BTN_SLOTS 8        // A, B, L, R, UP, DOWN, LEFT, RIGHT
extern const char *btn_name[BTN_SLOTS];
extern const uint16_t btn_key[BTN_SLOTS];     // the GbaKey bit for each slot

typedef struct {
    uint8_t dest;
    int8_t  amount;        // HOLD/LATCH: -64..+63 like a depth; STEP: base units per press
    uint8_t mode;          // BTN_*
} BtnSlot;

// Switch actions (SET page).
#define SWU_NONE   0
#define SWU_BYPASS 1       // UP: every mangle off
#define SWU_PAUSE  2       // UP: transport paused (internal clock)
#define SWU_COUNT  3
#define SWD_NONE   0
#define SWD_RESET  1       // DOWN: restart the song
#define SWD_NEXT   2       // DOWN: next song
#define SWD_COUNT  3
extern const char *swu_name[SWU_COUNT];
extern const char *swd_name[SWD_COUNT];

#define SCRUB_LEN_COUNT 7
extern const uint32_t scrub_len[SCRUB_LEN_COUNT];

// ---- the patch (16 shared flash slots on the Workshop) ----
// 'MP', not the FM card's 'FM' or 108's 'GB': a slot written by another GBA program is rejected
// ("BAD DATA") rather than misread. BUMP PATCH_VERSION whenever a stored field changes its layout
// or its meaning, and prefer appending + migrating to rejecting (see the FM card's CLAUDE.md).
//   v1  the map, settings, scene.
//   v2  table + list mode, BASE values, BTN map - APPENDED. A v1 slot arrives zero-padded past its
//       end; mod_patch_accept() gives it the factory buttons and migrates it.
//   v3  ten more destinations. BASE could not grow in place (BTN follows it), so the v2 array is
//       left as baseV2 and a full-size base[] is APPENDED; mod_patch_accept() copies a v2 slot's
//       values across.
//   v4  the cart the patch was saved on (tag + version + checksum, beside game[4]) and where its
//       song tables are - APPENDED. At boot, a slot saved on the inserted cart is loaded and the
//       scan skipped (main.c). A v3 slot arrives with these zeroed: no tag, no tables, no skip.
#define PATCH_MAGIC   0x4D50u
#define PATCH_VERSION 4
#define PATCH_TABLES  8            // = SCAN_MAX_TABLES (mod.c asserts it)

typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  clkMode;          // CLK_*
    ModSlot  mod[SRC_COUNT];
    uint8_t  ppqnIx;           // index into clk_ppqn
    uint8_t  maxChans;         // 1..12
    uint8_t  gain;             // mixer gain, 16 = unity
    uint8_t  follow;           // track the CV outputs follow, 0..15
    uint8_t  repeat;           // restart a song that ends
    uint8_t  scrubLenIx;       // index into scrub_len
    uint8_t  swUp, swDown;     // SWU_*, SWD_*
    uint8_t  cartSafe;         // 1 = 4/2 wait states
    uint8_t  pad[3];
    // The scene: restored only on the same game (matching code).
    char     game[4];
    uint16_t song;             // playable-song index
    uint16_t mute;             // the PLAY page's mutes
    // ---- v2 ---- (keep appending; nothing above may move)
    uint8_t  table;            // scene: song table index
    uint8_t  list;             // scene: LIST_*
    int8_t   baseV2[DEST_COUNT_V2]; // v2's BASE values; migrated into base[] and unused since v3
    BtnSlot  btn[BTN_SLOTS];
    // ---- v3 ----
    int8_t   base[DEST_COUNT]; // BASE values, in each destination's own units
    // ---- v4 ---- the cart, and its music's whereabouts (written on every save)
    uint16_t cartTag;          // rom_cart_tag(); AT GBA_PATCH_TAG_OFFSET, the host announces it
    uint8_t  romVersion;       // header 0xBC
    uint8_t  romCheck;         // header 0xBD
    uint8_t  tableCount;       // song tables below, 0 = none (no MP2K found when saved)
    uint8_t  pad4[3];
    uint32_t tableAddr[PATCH_TABLES];
    uint16_t tableRate[PATCH_TABLES];
} Patch;

extern Patch g_patch;

// ---- live state, for the UI ----
extern int      g_song;                 // selected playable song, -1 if none
extern uint16_t g_uiMute;               // PLAY page mutes (bit per track)
extern uint8_t  g_uiSolo;               // PLAY page solo, 0xFF = none
extern int32_t  g_destVal[DEST_COUNT];  // each destination's CV + button value, -2048..2047
extern uint8_t  g_bypass;               // switch UP is bypassing the mangles
extern uint8_t  g_srcHigh;              // bit per source: its Schmitt trigger is high
extern uint8_t  g_perform;              // PERFORM mode: the buttons drive the BTN map
extern uint8_t  g_btnLatch;             // bit per BTN slot: latched on

void mod_init(void);
void mod_update(void);                  // every main-loop iteration
void mod_song_select(int n);            // wraps; restarts the clock alignment
void mod_song_step(int delta);
void mod_restart(void);                 // restart the current song (RESET)
void mod_set_list(int table, int listMode);   // switch song table / list, start its first song

void mod_default_patch(void);
int  mod_patch_accept(Patch *p);        // validate, migrate older versions, clamp; 0 = reject
void mod_patch_applied(void);           // push the patch's settings into the engine
void mod_patch_prepare_save(void);      // record the scene into g_patch before a save
void mod_patch_loaded(void);            // after a load: settings, and the scene if it is this game
int  mod_patch_is_this_cart(const Patch *p);  // saved on the inserted cart (tag, code, version, sum)

// ---- buttons (sampled in mod_update; the UI drains the latch at frame rate) ----
extern uint16_t g_btn;                  // held now (GbaKey layout)
uint16_t mod_take_presses(void);        // presses + auto-repeats since the last call
