// Isolated GAX 3.05-style data player. Bus addresses remain ROM addresses on host and ARM.
#ifndef GX_PLAYER_H
#define GX_PLAYER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GX_CHANNELS 32
#define GX_NOLOOP UINT32_MAX
#define GX_MAX_SAMPLE 0x7ffffu
#define GX_EVENT_ROW 1
#define GX_EVENT_LOOP 2
#define GX_EVENT_UNSUPPORTED 3
#define GX_EVENT_BAD_DATA 4
#define GX_EVENT_STOP 5
#define GX_PERF_EFFECT 32u
#define GX_FEATURE_PINGPONG 64u
#define GX_FEATURE_MODULATION 65u
#define GX_FEATURE_VIBRATO 66u

typedef struct {const uint8_t *data;uint32_t base,size;} GxRom;
typedef struct {uint32_t data,size,loop,start,step;uint16_t left,right;uint8_t reverse;} GxVoice;
typedef struct {
    void *user;
    void (*start)(void *,unsigned,const GxVoice *);
    void (*update)(void *,unsigned,const GxVoice *);
    void (*stop)(void *,unsigned);
    int (*done)(void *,unsigned);
    void (*note)(void *,unsigned,GxVoice *);
    void (*event)(void *,unsigned,uint32_t);
} GxHooks;
typedef struct {
    uint32_t mute;
    int16_t pitch,voice_shift,table_offset;
    int32_t scrub;
    uint16_t tempo; // Q8 frame scheduling multiplier; does not affect a direct gx_frame call
    uint8_t sample_start,sample_end,reverse_chance,drop_chance;
} GxMods;
typedef struct {
    GxVoice voice;
    uint32_t stream,instrument,env,perf,notes;
    int32_t pitch,perf_pitch,fine_tune;
    uint16_t rest,env_time,instrument_id,perf_wait,perf_index,perf_count;
    int16_t transpose,step_slide,perf_slide,vol_slide,perf_vol_slide;
    uint8_t pattern_empty,active,gate,note,activity,released,volume,perf_volume,env_volume;
    uint8_t perf_speed,fixed,slot,has_wave,reverse;
} GxTrack;
typedef struct {
    GxRom rom;GxHooks hooks;GxMods mods;GxTrack tracks[GX_CHANNELS];
    uint32_t header,patterns,instruments,samples,frames,loops,rng;
    uint32_t unsupported[3]; // IDs 0..15 pattern, 32..47 perf, 64+ feature limitations
    uint16_t channels,rows,orders,restart,order,row,wait,volume,mixing_rate;
    uint8_t speed[2],phase,break_pattern,hold_rows,running,repeat,error;
} GxPlayer;
void gx_init(GxPlayer *,GxRom,const GxHooks *);
int gx_song_valid(const GxRom *,uint32_t);
unsigned gx_scan(const GxRom *,uint32_t begin,uint32_t end,uint32_t *out,unsigned cap);
int gx_start(GxPlayer *,uint32_t header);
void gx_stop(GxPlayer *);
void gx_frame(GxPlayer *); // ONE GAX control frame, nominally one GBA VBlank
void gx_update(GxPlayer *); // no time advance: apply live mute/pitch
int gx_seek(GxPlayer *,unsigned order,unsigned row); // silence voices; preserves effect history
int gx_restart(GxPlayer *);
uint32_t gx_frame_hz_q16(const GxPlayer *);
#ifdef __cplusplus
}
#endif
#endif
