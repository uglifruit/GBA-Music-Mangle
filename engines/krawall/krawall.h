// Bounded, allocation-free Krawall ROM player: Krawall's own player logic (lib/player.c and
// lib/player_effects.h, LGPL-2.1, see krawall.c and COPYING.LGPL) reading the cart through bounds-
// checked accessors and playing through the hooks. All addresses are GBA bus addresses.
#ifndef KR_PLAYER_H
#define KR_PLAYER_H
#include <stdint.h>
#include "../common/eng.h"
#ifdef __cplusplus
extern "C" {
#endif
#define KR_CHANNELS 32
#define KR_NOLOOP UINT32_MAX
#define KR_MAX_SAMPLE 0x7ffffu
#define KR_MODERN 0
#define KR_2003 1
#define KR_EVENT_ROW 1
#define KR_EVENT_LOOP 2
#define KR_EVENT_STOP 3
#define KR_EVENT_MARK 4
#define KR_EVENT_UNSUPPORTED 5
#define KR_EVENT_BAD_DATA 6

typedef EngRom KrRom;
typedef struct {
    uint32_t module, samples, instruments;
    uint16_t sample_count, instrument_count;
    uint8_t format; // KR_2003: byte row count, the note byte carries the instrument's 9th bit
    // A module can hold several songs, split by 254 ("+++") in its order list. song_mode plays the
    // one starting at start_order and loops it (or stops) at the next marker, as Krawall's own
    // KRAP_MODE_SONG does; 0 plays the whole order list and restarts at the module's restart order.
    uint8_t start_order, song_mode;
} KrSong;
typedef EngVoice KrVoice; // Q12 step at 16384 Hz; left/right 0..255 after envelope and mute
// Callback pointers are copied at init; user/context and ROM must outlive the player. `note` may
// mutate a proposed voice at note-on; the engine revalidates the complete sample range after it.
typedef EngHooks KrHooks;
typedef struct {
    uint32_t mute; // all 32 logical tracks, independent of hardware voice cap
    int16_t pitch, voice_shift, table_offset; // Q8 semitones; table offset in bytes
    int32_t scrub;
    uint16_t tempo; // Q8 multiplier, 256 = original; zero pauses internal scheduling
    uint8_t sample_start, sample_end, reverse_chance, drop_chance;
} KrMods;
// A channel: Krawall's MChannel (the names follow its fields), plus what the mixer is told (freq,
// mixvol, mixpan: its kramSetFreq/Vol/Pan) and what the lanes show.
typedef struct {
    KrVoice voice;
    uint32_t sample_addr, instrument_addr, notes; // the Sample and Instrument in use (0 = none)
    uint32_t freq;                    // Hz, as last set
    uint16_t instrument;              // the pattern's instrument number (1-based)
    int16_t knote, period, periodc, portainc, portadest, glissnote; // knote: Krawall's note (0 = C-0)
    int8_t volume, cvolume, panning, volumec, panningc, mixpan;
    uint8_t mixvol, pos, effect, leffect, effectop, effectVC;
    uint8_t vibrpos, vibrspeed, vibrdepth, vibrretrig, vibrwave;
    uint8_t trempos, tremspeed, tremdepth, tremretrig, tremwave;
    uint8_t panbpos, panbspeed, panbdepth, panbretrig, panbwave;
    uint8_t glissando, trmcnt, trmon, trmtot, volslide, volslide_fine, cvolslide, volslidefine;
    uint8_t panslide, panslidefine, arpeggio, arpeggiocnt, porta, porta_fine, porta_efine, retrig;
    uint8_t resetnote, playnote, touched;
    // instrument envelopes, fadeout and auto-vibrato
    uint8_t i_active, i_vol_status, i_vol_node, i_vol_tick, i_vol_ticktarget, i_vol_fadeactive;
    uint8_t i_pan_status, i_pan_node, i_pan_tick, i_pan_ticktarget, i_vibrpos;
    int8_t i_vol, i_pan;
    uint16_t i_vol_pos, i_pan_pos;
    int16_t i_vol_fade;
    // the lanes: note is the MIDI key (pattern note + 11), active a voice is sounding
    uint8_t active, note, gate, activity;
} KrTrack;
typedef struct {
    KrRom rom;
    KrSong song;
    KrHooks hooks;
    KrMods mods;
    KrTrack tracks[KR_CHANNELS];
    uint32_t stream, pattern, ticks, beats, loops, rng;
    uint32_t unsupported[2]; // bit n identifies an encountered unimplemented effect n
    uint16_t order, row, rows, tick, bpm, speed;
    int16_t goto_order, goto_row;     // Krawall's pGotoOrder / pGotoRow (-1 = none)
    uint8_t loop_row, loop_count, loop_active; // pattern loop (E6x)
    uint8_t channels, orders, restart, global_volume, volume, volslide, vibrofs;
    uint8_t instrument_based, fast_volslides, linear, song_mode;
    uint8_t running, repeat, error, format, loop_due;
} KrPlayer;

void kr_init(KrPlayer *, KrRom, const KrHooks *);
int kr_module_valid(const KrRom *, uint32_t module, uint8_t format);
// Incremental scanner: bounded address range, returns number stored (up to capacity).
// Candidates are structural, not proof of an associated sample bank. Supply banks explicitly.
unsigned kr_scan(const KrRom *, uint32_t begin, uint32_t end, uint8_t format,
                 uint32_t *modules, unsigned capacity);
uint32_t kr_module_ticks(const KrRom *, uint32_t module, uint8_t format);
// The songs in a module: each one's first order (0, then the order after each run of 254s), at
// most cap. ticks (may be 0) gets each one's length to its marker or the end, as kr_module_ticks.
unsigned kr_module_songs(const KrRom *, uint32_t module, uint8_t format, uint8_t *starts,
                         uint32_t *ticks, unsigned cap); // one pass, tracker ticks
int kr_start(KrPlayer *, const KrSong *);
void kr_stop(KrPlayer *);
void kr_tick(KrPlayer *);   // exactly ONE tracker tick (Krawall's timer routine; 24 a beat at speed 6)
void kr_update(KrPlayer *); // live mute/pitch/gain; no time advances
uint32_t kr_tick_hz_q16(const KrPlayer *); // BPM * 0.4 * tempo multiplier, Q16
int kr_seek(KrPlayer *, unsigned order, unsigned row); // bounded seek, releases old voices
int kr_restart(KrPlayer *); // restart module; repeat/controls preserved
#ifdef __cplusplus
}
#endif
#endif
