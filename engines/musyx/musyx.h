// Allocation-free GBA MusyX PCM experiment. Every address is a logical GBA ROM address.
#ifndef MS_PLAYER_H
#define MS_PLAYER_H
#include <stdint.h>
#include "../common/eng.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MS_TRACKS 17
#define MS_VOICES 12
#define MS_NOLOOP UINT32_MAX
#define MS_MAX_SAMPLE 0x7ffffu
#define MS_EVENT_BAD_DATA 1
#define MS_EVENT_UNSUPPORTED 2
#define MS_EVENT_NOTE 3
#define MS_EVENT_LOOP 4
#define MS_EVENT_STOP 5
#define MS_WARN_LOOP_TIME 1u
#define MS_WARN_PREVIEW_TAIL 2u
#define MS_WARN_PREVIEW_CHAIN 4u
#define MS_WARN_VOICE_LIMIT 8u
#define MS_WARN_REJECTED_VOICE 16u
#define MS_WARN_KEYMAP_RANGE 32u     // a note outside its keymap played the nearest entry
// Why voices were rejected (MsPlayer.why, a mask): the program's sound-list entry allows no voices,
// names a macro past the table, a sample failed its checks, a macro ran off its range/budget.
#define MS_WHY_NO_VOICES 1u
#define MS_WHY_PROGRAM 2u
#define MS_WHY_SAMPLE 4u
#define MS_WHY_MACRO 8u

// The Game Boy channels (PSG): 0, 1 squares, 2 wave, 3 noise. The driver gives every note on tracks
// 0-3 a channel (the track's, or the macro header's when that is 0-3), and every note on 4-15 a PCM
// voice. The player keeps their state here; the caller writes the registers (payload/eng_musyx.c) or
// emulates them (pcplay).
#define MS_PSG 4
#define MS_ALL (MS_VOICES+MS_PSG) // the voice array: the PCM voices, then one per Game Boy channel
typedef struct {
    int32_t pitch;        // Q8 semitones (MIDI key x 256), mods included
    uint32_t wave;        // wave channel: the waveform to load (a bus address), wave_len bytes
    uint8_t wave_len;     // 16 (32 4-bit steps, one bank) or 32 (64 steps, both banks)
    uint8_t on,trigger;   // trigger: restart the channel (a key-on, or a volume / envelope change)
    uint8_t duty,env,noise,volume; // duty 0..3; env NRx2 low nibble (0 = none); noise NR43 or 255 (by pitch); volume 0..127
    uint8_t track,pan;    // pan 0..127: 0-41 left, 42-85 both, 86+ right (the hardware's three places)
} MsPsg;

typedef EngRom MsRom;
typedef struct {uint32_t root,song,group;uint16_t index;} MsSong;
typedef struct {uint16_t songs,samples,macros;} MsCatalogue;
typedef EngVoice MsPcm;
typedef EngHooks MsHooks;
typedef struct {
    uint16_t mute,tempo;
    int16_t pitch,voice_shift,table_offset;
    int32_t scrub;
    uint8_t sample_start,sample_end,reverse_chance,drop_chance;
} MsMods;
typedef struct {
    uint32_t entries,seq,base_time,entry_time,message,message_time;
    uint32_t notes;uint8_t program,note,gate,activity,ended,pattern_active;
} MsTrack;
// A voice, as the cart's own driver keeps one (its macros, envelopes and pitch modulators run once a
// video frame, ms_frame(); notes start and end on the sequencer's tick, ms_tick()).
typedef struct {
    MsPcm pcm;
    uint32_t macro_begin,macro_end,ip,keyoff,gate_left,age,sample_rate,wave,adsr;
    int32_t pitch,fade;   // the pitch sounding (Q8 semitones, all parts summed); op 21's level step a frame
    int16_t offset;       // SETNOTE / ADDNOTE / RNDNOTE / keymap transpose: Q8 semitones from the note
    int16_t sweep[2][3];  // op 11's two sweeps: sum, step a frame, limit (the sum wraps by the limit)
    int16_t vib[2];       // op 10's vibrato: sum, step a frame (negated every vib_period + 1 frames)
    int16_t slide[3];     // op 12's slide: sum, step a frame, target
    int16_t autopan[2];   // op 45's pan sweep: position (pan x 256), step a frame; bounds autopan_lo/hi x 256
    uint16_t level;       // the envelope, 0..0xFF00 (full); scales the volume
    uint16_t wait,repeat; // WAIT's frames left; op 6's passes left
    uint16_t macro_id,root_note;
    uint8_t env,phase,hold,rel; // env 0 none, 1 ADSR (phase 0..3), 2 fade; hold: key-off leaves the ADSR; rel: release asked
    uint8_t hw,duty,henv,noise,wave_len,pan,prio; // header voice; PSG settings; pan 0..127; sound-list priority
    uint8_t vib_count,vib_period,autopan_lo,autopan_hi,autopan_bounce;
    uint8_t owner,note,velocity,cur,volume; // velocity as played; cur, the volume SCALEVOLUME scales; volume, what sounds
    uint8_t active,keyed,has_sample,ended,fixed,sounding,chains,fresh; // keyed: key-off happened; ended: END ran; fixed: unpitched; fresh: first step to run
} MsVoice;
typedef struct {
    MsRom rom;MsHooks hooks;MsMods mods;MsSong song;MsTrack tracks[MS_TRACKS];MsVoice voices[MS_ALL];
    uint32_t macros,samples,song_table,patterns,ticks,loops,rng,age,tail;
    uint32_t unsupported[2],warnings,rejected,last_opcode;
    uint16_t macro_count,sample_count,song_count,bpm;
    MsPsg psg[MS_PSG];
    uint32_t macro_lo,macro_hi;                    // the region all macros' code lies in (GOTO may cross)
    uint8_t running,error,repeat,preview,why,toggle,lead; // toggle: the next square for an alternating macro; lead: the track loops are counted on
} MsPlayer;
void ms_init(MsPlayer *,MsRom,const MsHooks *);
int ms_root_valid(const MsRom *,uint32_t root);
int ms_catalogue(const MsRom *,uint32_t root,MsCatalogue *);
unsigned ms_scan(const MsRom *,uint32_t begin,uint32_t end,uint32_t *roots,unsigned cap);
int ms_song_info(const MsRom *,uint32_t root,unsigned index,MsSong *);
int ms_song_shape(const MsRom *,uint32_t root,unsigned index,uint32_t *ticks96,int *loops);
int ms_start(MsPlayer *,const MsSong *);
void ms_stop(MsPlayer *);
void ms_tick(MsPlayer *); // ONE musical tick; source parser uses 96 PPQN
void ms_frame(MsPlayer *); // one video frame (59.73 Hz): macros, envelopes, pitch modulators
void ms_update(MsPlayer *); // live mute/pitch, no time advance
uint32_t ms_tick_hz_q16(const MsPlayer *);
int ms_audition_sample(MsPlayer *,uint32_t root,unsigned sample); // no sequencer/macro execution
#ifdef __cplusplus
}
#endif
#endif
