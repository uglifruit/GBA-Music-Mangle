// mod.h — the modulator: what each of the Workshop's inputs does to the player.
//
// The FM card's division of labour, unchanged: the Workshop streams its raw inputs, and every
// decision about what they MEAN lives here, in a table in GBA RAM (the MAP page). One mapping per
// source - a destination and a depth - and every destination sums whatever is mapped to it.
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
#define DEST_COUNT   16
extern const char *dest_name[DEST_COUNT];

typedef struct {
    uint8_t dest;
    int8_t  depth;         // -64..+63
} ModSlot;

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
#define PATCH_MAGIC   0x4D50u
#define PATCH_VERSION 1

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
} Patch;

extern Patch g_patch;

// ---- live state, for the UI ----
extern int      g_song;                 // selected playable song, -1 if none
extern uint16_t g_uiMute;               // PLAY page mutes (bit per track)
extern uint8_t  g_uiSolo;               // PLAY page solo, 0xFF = none
extern int32_t  g_destVal[DEST_COUNT];  // each destination's summed value, -2048..2047
extern uint8_t  g_bypass;               // switch UP is bypassing the mangles
extern uint8_t  g_srcHigh;              // bit per source: its Schmitt trigger is high

void mod_init(void);
void mod_update(void);                  // every main-loop iteration
void mod_song_select(int n);            // wraps; restarts the clock alignment
void mod_song_step(int delta);
void mod_restart(void);                 // restart the current song (RESET)

void mod_default_patch(void);
int  mod_patch_valid(const Patch *p);
void mod_patch_applied(void);           // push the patch's settings into the engine
void mod_patch_prepare_save(void);      // record the scene into g_patch before a save
void mod_patch_loaded(void);            // after a load: settings, and the scene if it is this game

// ---- buttons (sampled in mod_update; the UI drains the latch at frame rate) ----
extern uint16_t g_btn;                  // held now (GbaKey layout)
uint16_t mod_take_presses(void);        // presses + auto-repeats since the last call
