// Allocation-free GBA MusyX PCM experiment. Every address is a logical GBA ROM address.
#ifndef MS_PLAYER_H
#define MS_PLAYER_H
#include <stdint.h>
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

typedef struct {const uint8_t *data;uint32_t base,size;} MsRom;
typedef struct {uint32_t root,song,group;uint16_t index;} MsSong;
typedef struct {uint16_t songs,samples,macros;} MsCatalogue;
typedef struct {uint32_t data,size,loop,start,step;uint16_t left,right;uint8_t reverse;} MsPcm;
typedef struct {
    void *user;
    void (*start)(void *,unsigned,const MsPcm *);
    void (*update)(void *,unsigned,const MsPcm *);
    void (*stop)(void *,unsigned);
    int (*done)(void *,unsigned);
    void (*note)(void *,unsigned,MsPcm *);
    void (*event)(void *,unsigned,uint32_t);
} MsHooks;
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
    uint8_t owner,note,velocity,volume,active,released,has_sample,tail,chains;
} MsVoice;
typedef struct {
    MsRom rom;MsHooks hooks;MsMods mods;MsSong song;MsTrack tracks[MS_TRACKS];MsVoice voices[MS_VOICES];
    uint32_t macros,samples,song_table,patterns,ticks,loops,rng,age;
    uint32_t unsupported[2],warnings,rejected,last_opcode;
    uint16_t macro_count,sample_count,song_count,bpm;
    uint8_t running,error,repeat,preview;
} MsPlayer;
void ms_init(MsPlayer *,MsRom,const MsHooks *);
int ms_root_valid(const MsRom *,uint32_t root);
int ms_catalogue(const MsRom *,uint32_t root,MsCatalogue *);
unsigned ms_scan(const MsRom *,uint32_t begin,uint32_t end,uint32_t *roots,unsigned cap);
int ms_song_info(const MsRom *,uint32_t root,unsigned index,MsSong *);
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
