// Logik State "GBAModPlay" (LS_Play) module player: bounded, allocation-free. All addresses are GBA
// bus addresses. Written from a reading of the driver in Super Puzzle Fighter II Turbo (2026-11-06;
// docs/ENGINES.md "GBAModPlay"); no cartridge code is executed.
//
// THE DATA. A module ("GBAMOD30") holds the song: channels, order list, patterns, speed and BPM. Its
// samples are in a separate BANK the game hands the driver: 256 instruments {length, finetune,
// volume, loop start, loop length, relative note}, then every sample's bytes back to back (signed
// 8-bit). Two versions share the magic with different layouts (see lsplay.c): 2002's (Puzzle Fighter)
// and 2003's (Driv3r, "LS_Play"). A pattern is one block per channel, each three run-length streams of {rows, value}:
// note and instrument (u16, big-endian: note = v >> 7, 0x1FF none; instrument = v & 127, 0 none), the
// volume column (u8, 0 none, else volume + 1) and the effect (u16 big-endian: effect, parameter).
#ifndef LS_PLAYER_H
#define LS_PLAYER_H
#include <stdint.h>
#include "../common/eng.h"
#ifdef __cplusplus
extern "C" {
#endif
#define LS_CHANNELS 16
#define LS_NOLOOP UINT32_MAX
#define LS_MAX_SAMPLE 0xFFFFu
#define LS_EVENT_ROW 1
#define LS_EVENT_LOOP 2
#define LS_EVENT_STOP 3
#define LS_EVENT_BAD_DATA 4
#define LS_EVENT_UNSUPPORTED 5
#define LS_BANK_SIZE 0xC00u          // the bank's instrument table: 256 x 12 bytes

typedef EngRom LsRom;
typedef EngVoice LsVoice;
typedef EngHooks LsHooks;
typedef struct {
    uint32_t mute;
    int16_t pitch, voice_shift, table_offset; // Q8 semitones; instruments; bytes into the bank's table
    int32_t scrub;
    uint16_t tempo;                           // Q8 multiplier (kr_tick_hz_q16's sense)
    uint8_t sample_start, sample_end, reverse_chance, drop_chance;
} LsMods;
typedef struct { uint32_t p; uint16_t left, value; } LsStream; // a run-length stream: next pair, rows left, value
typedef struct {
    LsVoice voice;
    LsStream s[3];                  // note+instrument, volume column, effect
    uint32_t notes;
    int32_t lin;                    // linear mode's portamento offset (period units, 64 a semitone)
    int16_t amiga, vib;             // Amiga mode's portamento and vibrato offsets (period units)
    int32_t vib_lin;                // version 2's linear vibrato offset (period units)
    int16_t fine, rel;              // the instrument's finetune and relative note
    int16_t vol;                    // 0..64 (C can set more)
    uint16_t note, ins;             // the note playing; the instrument (0-based)
    uint16_t cell_note, cell_ins;   // this row's cell
    uint8_t cell_vc, cell_fx, cell_param;
    int8_t kind;                    // the effect carried on through the row's ticks, -1 none
    uint8_t vsl_up, vsl_down, vib_speed, vib_depth, vib_pos, loop_row, loop_count;
    uint16_t porta_mem;             // 1xx / 2xx's parameter (linear mode keeps it)
    int16_t tp_speed, tp_step, tp_target; // tone portamento
    uint8_t active, gate, key, activity;  // key: the MIDI key the lanes show
} LsTrack;
typedef struct {
    LsRom rom; LsHooks hooks; LsMods mods; LsTrack tracks[LS_CHANNELS];
    uint32_t module, bank, patterns, loops, rng, unsupported, frames;
    uint16_t channels, orders, pattern_count, rows, order, row, speed, bpm, tick, counter;
    uint16_t music_volume;          // the game's music volume (Puzzle Fighter: 90, its default; Driv3r 64)
    uint8_t version;                // the module layout and driver: 1 (2002) or 2 (2003), see lsplay.c
    uint16_t seek_order, seek_row;  // a jump into a pattern (break, loop), taken after the row's cells
    uint16_t pattern_notes;         // notes this pattern so far: a silent one jumping to itself is the end
    uint8_t linear, running, repeat, error, broke, seek, ended;
} LsPlayer;

void ls_init(LsPlayer *, LsRom, const LsHooks *);
int ls_module_valid(const LsRom *, uint32_t module);   // 0, or its version: 1 (2002) or 2 (2003)
uint32_t ls_module_end(const LsRom *, uint32_t module); // its last pattern's last byte + 1
// A bank at a: every instrument it has fits the rules, and the samples fit the cart. used: a mask of
// instruments (1-based, 128 bits) that must all be present; 0 skips that test. Returns the number of
// instruments with a sample, 0 if it is not a bank.
unsigned ls_bank_valid(const LsRom *, uint32_t a, const uint32_t *used, int version);
// The instruments a module plays (bit n of the 128-bit mask = instrument n). 0 if it cannot be read.
int ls_module_instruments(const LsRom *, uint32_t module, uint32_t used[4]);
// Its length in ticks to its end or its loop (*loops: a position jump backwards).
int ls_module_shape(const LsRom *, uint32_t module, uint32_t *ticks, int *loops);
int ls_start(LsPlayer *, uint32_t module, uint32_t bank);
void ls_stop(LsPlayer *);
void ls_tick(LsPlayer *);           // ONE driver tick (a row at speed 1; 24 a beat at the usual tempo)
void ls_update(LsPlayer *);         // live mute / pitch / mods, no time
int ls_seek(LsPlayer *, unsigned order, unsigned row);
uint32_t ls_tick_hz_q16(const LsPlayer *); // the driver's ticks/s (BPM x 2 / 5, floored), tempo mangle in
#ifdef __cplusplus
}
#endif
#endif
