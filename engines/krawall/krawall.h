// Bounded, allocation-free Krawall ROM player. All addresses are GBA bus addresses.
#ifndef KR_PLAYER_H
#define KR_PLAYER_H
#include <stdint.h>
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

typedef struct {
    const uint8_t *data;
    uint32_t base, size;
} KrRom;
typedef struct {
    uint32_t module, samples, instruments;
    uint16_t sample_count, instrument_count;
    uint8_t format; // KR_2003: byte row count, no extended instrument byte
} KrSong;
typedef struct {
    uint32_t data, size, loop, start, step; // Q12 step at 16384 Hz
    uint16_t left, right;                 // 0..255, post envelope/mute
    uint8_t reverse;
} KrVoice;
typedef struct {
    // Callback pointers are copied at init; user/context and ROM must outlive the player.
    void *user;
    void (*start)(void *, unsigned, const KrVoice *);
    void (*update)(void *, unsigned, const KrVoice *);
    void (*stop)(void *, unsigned);
    int (*done)(void *, unsigned);
    // Mutate a proposed voice at note-on (chaos, alternate samples, custom routing).
    // The engine revalidates the complete sample range after this callback.
    void (*note)(void *, unsigned, KrVoice *);
    void (*event)(void *, unsigned, uint32_t);
} KrHooks;
typedef struct {
    uint32_t mute; // all 32 logical tracks, independent of hardware voice cap
    int16_t pitch, voice_shift, table_offset; // Q8 semitones; table offset in bytes
    int32_t scrub;
    uint16_t tempo; // Q8 multiplier, 256 = original; zero pauses internal scheduling
    uint8_t sample_start, sample_end, reverse_chance, drop_chance;
} KrMods;
typedef struct {
    KrVoice voice;
    uint32_t instrument_addr, sample_addr, notes;
    uint16_t instrument, env_tick, pan_tick, fade;
    int32_t pitch; // Q8 semitones, MIDI numbering (packed note 49 = MIDI 60)
    uint32_t offset;
    uint8_t active, released, note, gate, activity, volume, channel_volume;
    int16_t pan;
    uint8_t effect, param, memory[51];
} KrTrack;
typedef struct {
    KrRom rom;
    KrSong song;
    KrHooks hooks;
    KrMods mods;
    KrTrack tracks[KR_CHANNELS];
    uint32_t stream, pattern, ticks, beats, loops, rng;
    uint32_t unsupported[2]; // bit n identifies an encountered unimplemented effect n
    uint16_t order, row, rows, tick, bpm, speed, pending_order, pending_row;
    uint8_t channels, orders, restart, global_volume, instrument_based;
    uint8_t running, repeat, error, format;
} KrPlayer;

void kr_init(KrPlayer *, KrRom, const KrHooks *);
int kr_module_valid(const KrRom *, uint32_t module, uint8_t format);
// Incremental scanner: bounded address range, returns number stored (up to capacity).
// Candidates are structural, not proof of an associated sample bank. Supply banks explicitly.
unsigned kr_scan(const KrRom *, uint32_t begin, uint32_t end, uint8_t format,
                 uint32_t *modules, unsigned capacity);
int kr_start(KrPlayer *, const KrSong *);
void kr_stop(KrPlayer *);
void kr_tick(KrPlayer *);   // exactly ONE tracker tick (normally 24 ticks/beat)
void kr_update(KrPlayer *); // live mute/pitch/gain; no time advances
uint32_t kr_tick_hz_q16(const KrPlayer *); // BPM * 0.4 * tempo multiplier, Q16
int kr_seek(KrPlayer *, unsigned order, unsigned row); // bounded seek, releases old voices
int kr_restart(KrPlayer *); // restart module; repeat/controls preserved
#ifdef __cplusplus
}
#endif
#endif
