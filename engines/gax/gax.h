// Isolated GAX 3.05-style data player. Bus addresses remain ROM addresses on host and ARM.
#ifndef GX_PLAYER_H
#define GX_PLAYER_H
#include <stdint.h>
#include "../common/eng.h"
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

typedef EngRom GxRom;
typedef EngVoice GxVoice;
typedef EngHooks GxHooks;
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
    // From the GAX 2.01a replayer (Tang Tang, 2026-11-06), and shinen-gax-python for GAX 3:
    int32_t porta_target;            // tone portamento (3xx): the pitch it glides to, Q8
    int16_t porta_rate,vib;          // its step a frame; the vibrato's pitch offset, Q8
    uint8_t delay,delay_note,delay_ins; // note delay (EDx): frames left, the note and instrument held
    uint8_t vib_wait,vib_phase,perf_loop; // vibrato wait and sine phase; the perf list's loop count
    // GAX 2 wave modulation: the loop is a window of the wave's modulation size at mod_pos, moved by
    // its step every speed frames between its loop start and end, the play position moving with it.
    // mod_shift is the move not yet passed to the mixer (the caller applies it and zeroes it).
    uint32_t mod_pos,mod_size,mod_step,mod_lo,mod_hi;int32_t mod_shift;
    uint8_t mod_on,mod_wait,mod_speed;int8_t mod_dir;
} GxTrack;
typedef struct {
    GxRom rom;GxHooks hooks;GxMods mods;GxTrack tracks[GX_CHANNELS];
    uint32_t header,patterns,instruments,samples,frames,loops,rng;
    uint32_t unsupported[3]; // IDs 0..15 pattern, 32..47 perf, 64+ feature limitations
    uint32_t chan[GX_CHANNELS]; // each channel's pattern table (V3: from the header; V2: its handler)
    uint16_t channels,rows,orders,restart,order,row,wait,volume,mixing_rate;
    uint8_t speed[2],phase,break_pattern,hold_rows,running,repeat,error,v2;
} GxPlayer;
void gx_init(GxPlayer *,GxRom,const GxHooks *);
// Header classification (gaxtapper's v3 rules). Stages a near miss can fail at, counted by
// gx_scan_stats for a hardware report: which check is turning a cart's songs away.
#define GX_HDR_SONG 1
#define GX_HDR_FX 2                // valid zero-channel header: shared samples / effects
#define GX_REJECT_SHAPE 3          // rows/orders out of this decoder's bounds
#define GX_REJECT_NOTES 4
#define GX_REJECT_INSTRUMENTS 5
#define GX_REJECT_SAMPLES 6
#define GX_REJECT_CHANNELS 7
#define GX_REJECT_COUNT 8
#define GX_STRICT_RATE 1           // what the first, stricter validator would also have refused
#define GX_STRICT_VOLUME 2
#define GX_STRICT_LOOP 3
#define GX_STRICT_PATTERN 4
typedef struct {uint32_t count[GX_REJECT_COUNT];} GxScanStats;
int gx_header_kind(const GxRom *,uint32_t);
int gx_song_valid(const GxRom *,uint32_t);
int gx_strict_reason(const GxRom *,uint32_t);
unsigned gx_scan(const GxRom *,uint32_t begin,uint32_t end,uint32_t *out,unsigned cap);
unsigned gx_scan_stats(const GxRom *,uint32_t begin,uint32_t end,uint32_t *out,unsigned cap,GxScanStats *);
int gx_start(GxPlayer *,uint32_t header);
// GAX 2 (2.01 .. 2.35; docs/ENGINES.md "GAX 2"). A song entry is a count n >= 4 and n pointers to
// 7-word handlers {three Thumb code pointers, a count, a pointer, a number, a data pointer}: one
// handler's data is the song header (V3's first 26 bytes, no channel pointers), the handlers after it
// are the channels, each one's data a pattern table. Sequence data is V3's; instruments are V3's fields
// at other offsets with one wave; samples are SIGNED (the caller sets the mixer). gx_v2_entry returns
// the channel count and fills header / chans (cap entries), or 0 if a is not a GAX 2 song entry.
unsigned gx_v2_entry(const GxRom *,uint32_t a,uint32_t *header,uint32_t *chans,unsigned cap);
int gx_start_v2(GxPlayer *,uint32_t header,const uint32_t *chans,unsigned n);
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
