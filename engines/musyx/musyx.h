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

// The Game Boy channels (PSG): 0, 1 squares, 2 wave, 3 noise. MusyX on the GBA drives them from
// macros (VOICE_ON / SETNOISE / HARDENVELOPE, a wave sample started on voice 2). The player keeps their
// state here; the caller writes the registers (payload/eng_musyx.c) or emulates them (pcplay).
#define MS_PSG 4
typedef struct {
    int32_t pitch;        // Q8 semitones (MIDI key x 256), mods included
    uint32_t wave;        // wave channel: the waveform to load (a bus address), wave_len bytes
    uint8_t wave_len;     // 16 (32 4-bit steps, one bank) or 32 (64 steps, both banks)
    uint8_t on,trigger;   // trigger: restart the channel (a new note, or a volume / envelope change)
    uint8_t duty,env,noise,volume; // duty 0..3; env NRx2 low nibble (0 = none); noise NR43 or 255 (by pitch); volume 0..127
    uint8_t track;
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
typedef struct {
    MsPcm pcm;
    uint32_t macro_begin,macro_end,ip,keyoff,wait,gate_left,age,sample_rate;
    int32_t pitch;uint16_t macro_id,root_note;
    int16_t key_add;   // PLAYMACRO key offsets so far: a keymap reached through them is indexed by note + key_add
    uint32_t loop_at,wave; // the counted loop (op 6) in progress; a wave sample's data
    uint8_t hw,psg,duty,env,noise,loop_left,wave_len; // header voice (255 any); PSG channel + 1 held; its settings
    uint8_t owner,note,velocity,volume,active,released,has_sample,tail,chains;
} MsVoice;
typedef struct {
    MsRom rom;MsHooks hooks;MsMods mods;MsSong song;MsTrack tracks[MS_TRACKS];MsVoice voices[MS_VOICES];
    uint32_t macros,samples,song_table,patterns,ticks,loops,rng,age;
    uint32_t unsupported[2],warnings,rejected,last_opcode;
    uint16_t macro_count,sample_count,song_count,bpm;
    MsPsg psg[MS_PSG];uint8_t psg_owner[MS_PSG];   // owner: voice + 1, 0 none
    uint32_t macro_lo,macro_hi;                    // the region all macros' code lies in (GOTO may cross)
    uint8_t running,error,repeat,preview,why;
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
void ms_update(MsPlayer *); // live mute/pitch, no time advance
uint32_t ms_tick_hz_q16(const MsPlayer *);
int ms_audition_sample(MsPlayer *,uint32_t root,unsigned sample); // no sequencer/macro execution
#ifdef __cplusplus
}
#endif
#endif
