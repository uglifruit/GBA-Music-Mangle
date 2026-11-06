// Independent bounded decoder for GAX 3.05-style song data. No cartridge code is executed.
// Format reference: loveemu's Gaxtapper and beanie_axolotl's MIT shinen-gax-python.
#include "gax.h"
static void zero(void *v,unsigned n){volatile uint8_t *p=v;while(n--)*p++=0;}
static int clamp(int x,int l,int h){return x<l?l:x>h?h:x;}
static int ok(const GxRom *r,uint32_t a,uint32_t n){return r->data&&a>=r->base&&a-r->base<=r->size&&n<=r->size-(a-r->base);}
static uint8_t b(const GxRom *r,uint32_t a){return r->data[a-r->base];}
static uint16_t h(const GxRom *r,uint32_t a){return b(r,a)|((uint16_t)b(r,a+1)<<8);}
static uint32_t w(const GxRom *r,uint32_t a){return h(r,a)|((uint32_t)h(r,a+2)<<16);}
static uint32_t divide(uint32_t n,uint32_t d){uint32_t q=0,r=0;if(!d)return 0;for(int i=31;i>=0;--i){r=(r<<1)|((n>>i)&1);if(r>=d){r-=d;q|=1u<<i;}}return q;}
static void event(GxPlayer *p,unsigned e,uint32_t v){if(p->hooks.event)p->hooks.event(p->hooks.user,e,v);}
static void unsupported(GxPlayer *p,unsigned id){if(id<96)p->unsupported[id>>5]|=1u<<(id&31);event(p,GX_EVENT_UNSUPPORTED,id);}
static void off(GxPlayer *p,unsigned c){p->tracks[c].active=p->tracks[c].gate=p->tracks[c].has_wave=p->tracks[c].mod_on=0;p->tracks[c].mod_shift=0;if(p->hooks.stop)p->hooks.stop(p->hooks.user,c);}
void gx_stop(GxPlayer *p){for(unsigned n=0;n<GX_CHANNELS;++n)off(p,n);p->running=0;event(p,GX_EVENT_STOP,0);}
static int fail(GxPlayer *p,uint32_t a){p->error=1;gx_stop(p);event(p,GX_EVENT_BAD_DATA,a);return 0;}
void gx_init(GxPlayer *p,GxRom r,const GxHooks *hooks){
    zero(p,sizeof(*p));p->rom.data=r.data;p->rom.base=r.base;p->rom.size=r.size;
    if(!r.data||r.size>0x2000000||r.base>UINT32_MAX-r.size)p->rom.size=0;
    if(hooks){p->hooks.user=hooks->user;p->hooks.start=hooks->start;p->hooks.update=hooks->update;p->hooks.stop=hooks->stop;
        p->hooks.done=hooks->done;p->hooks.note=hooks->note;p->hooks.event=hooks->event;}
    p->mods.tempo=256;p->repeat=1;p->rng=0x47415833;
}
// A song header, by gaxtapper's v3 rules (loveemu): the pointers and the reserved +0x1E field, plus
// the bounds this decoder needs (1..32 channels, 1..256 rows, 1..1024 orders). Returns GX_HDR_SONG,
// GX_HDR_FX for a valid zero-channel header (shared samples / an effects bank), or the GX_REJECT_*
// stage a candidate failed at (0 = not a header at all).
int gx_header_kind(const GxRom *r,uint32_t a){
    if((a&3)||!ok(r,a,32))return 0;
    unsigned ch=h(r,a),rows=h(r,a+2),orders=h(r,a+4);
    if(ch>32||h(r,a+30))return 0;
    uint32_t notes=w(r,a+12),ins=w(r,a+16),samples=w(r,a+20);
    if(!ch){                                         // gaxtapper: FX-only headers zero these
        if(rows||orders||w(r,a+24)||w(r,a+28))return 0;
        if((ins|samples)&3||!ok(r,ins,4)||!ok(r,samples,8)||!ok(r,w(r,ins),1)||!ok(r,w(r,samples),1)||w(r,samples+4))return 0;
        return GX_HDR_FX;
    }
    if(!rows||rows>256||!orders||orders>1024||!ok(r,a+32,ch*4))return GX_REJECT_SHAPE;
    if((notes&3)||!ok(r,notes,1))return GX_REJECT_NOTES;
    if((ins&3)||!ok(r,ins,4)||!ok(r,w(r,ins),1))return GX_REJECT_INSTRUMENTS;
    if((samples&3)||!ok(r,samples,8)||!ok(r,w(r,samples),1)||w(r,samples+4))return GX_REJECT_SAMPLES;
    for(unsigned c=0;c<ch;++c){
        uint32_t seq=w(r,a+32+4*c);
        if((seq&3)||!ok(r,seq,orders*4))return GX_REJECT_CHANNELS;
    }
    return GX_HDR_SONG;
}
int gx_song_valid(const GxRom *r,uint32_t a){return gx_header_kind(r,a)==GX_HDR_SONG;}
// What the stricter first validator (before 2026-11-01) also demanded of a song: 0 if it would have
// kept this one, else the first GX_STRICT_* it fails. Diagnostics only: tells a hardware report
// whether relaxing the scan is what found a song.
int gx_strict_reason(const GxRom *r,uint32_t a){
    if(!gx_song_valid(r,a))return GX_STRICT_PATTERN;
    unsigned ch=h(r,a),orders=h(r,a+4),rate=h(r,a+24);
    if(rate<4000||rate>48000)return GX_STRICT_RATE;
    if(h(r,a+8)>256)return GX_STRICT_VOLUME;
    if(h(r,a+6)>=orders)return GX_STRICT_LOOP;
    uint32_t notes=w(r,a+12);
    if(!ok(r,w(r,w(r,a+16)),24))return GX_STRICT_PATTERN;
    for(unsigned c=0;c<ch;++c){
        uint32_t seq=w(r,a+32+4*c);
        for(unsigned order=0;order<orders;++order){
            uint32_t pattern=notes+h(r,seq+order*4);
            if(!ok(r,pattern,1)||b(r,pattern)>1||b(r,seq+order*4+3))return GX_STRICT_PATTERN;
        }
    }
    return 0;
}
unsigned gx_scan(const GxRom *r,uint32_t begin,uint32_t end,uint32_t *out,unsigned cap){
    return gx_scan_stats(r,begin,end,out,cap,0);
}
unsigned gx_scan_stats(const GxRom *r,uint32_t begin,uint32_t end,uint32_t *out,unsigned cap,GxScanStats *st){
    unsigned n=0;if(!out||end<begin||!ok(r,begin,end-begin))return 0;
    for(uint32_t a=begin;a<end&&end-a>=4&&n<cap;a+=4){
        int k=gx_header_kind(r,a);
        if(k==GX_HDR_SONG)out[n++]=a;
        if(st&&k>0&&k<GX_REJECT_COUNT)++st->count[k];
    }
    return n;
}
static int get(GxPlayer *p,GxTrack *t,uint8_t *v){if(!ok(&p->rom,t->stream,1))return fail(p,t->stream);*v=b(&p->rom,t->stream++);return 1;}
// Each byte command represents at least one row: RLE zero/overrun is rejected.
static int decode(GxPlayer *p,GxTrack *t,uint8_t *note,uint8_t *ins,uint8_t *fx,uint8_t *param,unsigned remaining){
    *note=*ins=*fx=*param=0;
    if(t->pattern_empty)return 1;
    if(t->rest){--t->rest;return 1;}
    uint8_t cmd;if(!get(p,t,&cmd))return 0;
    if(cmd==0x80)return 1;
    if(cmd==0xFF){uint8_t run;if(!get(p,t,&run))return 0;if(!run||run>remaining)return fail(p,t->stream-1);t->rest=run-1;return 1;}
    if(cmd==0xFA)return get(p,t,fx)&&get(p,t,param);
    *note=cmd&127;
    if(!get(p,t,ins))return 0;
    if(!(cmd&128))return get(p,t,fx)&&get(p,t,param);
    return 1;
}
static int position(GxPlayer *p,unsigned order,unsigned row){
    if(order>=p->orders||row>=p->rows)return 0;
    p->order=order;p->row=row;p->wait=0;p->hold_rows=0;p->break_pattern=0;
    for(unsigned c=0;c<p->channels;++c){
        GxTrack *t=&p->tracks[c];uint32_t seq=p->chan[c];
        uint32_t a=p->patterns+h(&p->rom,seq+order*4);
        if(!ok(&p->rom,a,1)||b(&p->rom,a)>1)return fail(p,a);
        t->transpose=(int8_t)b(&p->rom,seq+order*4+2);t->pattern_empty=b(&p->rom,a);t->stream=a+1;t->rest=0;
        for(unsigned i=0;i<row;++i){uint8_t note,ins,fx,param;if(!decode(p,t,&note,&ins,&fx,&param,p->rows-i))return 0;}
    }
    return 1;
}
int gx_seek(GxPlayer *p,unsigned order,unsigned row){
    if(!p->running)return 0;
    if(!position(p,order,row))return fail(p,p->header);
    for(unsigned c=0;c<GX_CHANNELS;++c)off(p,c);
    return 1;
}
static int start_common(GxPlayer *p,uint32_t header){
    gx_stop(p);zero(p->tracks,sizeof(p->tracks));zero(p->unsupported,sizeof(p->unsupported));
    p->header=header;p->channels=h(&p->rom,header);p->rows=h(&p->rom,header+2);p->orders=h(&p->rom,header+4);
    p->restart=h(&p->rom,header+6);p->volume=h(&p->rom,header+8);p->mixing_rate=h(&p->rom,header+24);
    if(p->restart>=p->orders)p->restart=0;          // no longer a scan gate: a bad loop point loops to the top
    p->patterns=w(&p->rom,header+12);p->instruments=w(&p->rom,header+16);p->samples=w(&p->rom,header+20);
    p->speed[0]=p->speed[1]=6;p->phase=0;p->frames=0;p->error=0;p->running=1;
    if(!position(p,0,0))return fail(p,header);
    return 1;
}
int gx_start(GxPlayer *p,uint32_t header){
    if(!gx_song_valid(&p->rom,header))return 0;
    unsigned ch=h(&p->rom,header);
    for(unsigned c=0;c<ch&&c<GX_CHANNELS;++c)p->chan[c]=w(&p->rom,header+32+c*4);
    p->v2=0;return start_common(p,header);
}
// ---- GAX 2 ----
static int v2_handler(const GxRom *r,uint32_t a,uint32_t *data){
    if((a&3)||!ok(r,a,28))return 0;
    for(unsigned k=0;k<3;++k){uint32_t f=w(r,a+4*k);if(!(f&1)||!ok(r,f&~1u,2))return 0;}
    uint32_t cnt=w(r,a+12),rel=w(r,a+16),f14=w(r,a+20),d=w(r,a+24);
    if(cnt>=64||(rel&&!ok(r,rel,1))||f14>=0x10000||(d&3)||!ok(r,d,4))return 0;
    *data=d;return 1;
}
// A V2 song header with channels: the V3 bounds, the sample table's entry 0 may be empty.
static int v2_header(const GxRom *r,uint32_t a){
    if((a&3)||!ok(r,a,28))return 0;
    unsigned ch=h(r,a),rows=h(r,a+2),orders=h(r,a+4);
    uint32_t notes=w(r,a+12),ins=w(r,a+16),samples=w(r,a+20);
    return ch&&ch<=GX_CHANNELS&&rows&&rows<=256&&orders&&orders<=1024&&ok(r,notes,1)&&
        !(ins&3)&&ok(r,ins,4)&&ok(r,w(r,ins),1)&&!(samples&3)&&ok(r,samples,8);
}
unsigned gx_v2_entry(const GxRom *r,uint32_t a,uint32_t *header,uint32_t *chans,unsigned cap){
    if((a&3)||!ok(r,a,4))return 0;
    uint32_t n=w(r,a);if(n<4||n>40||!ok(r,a+4,n*4))return 0;
    unsigned handlers=0,ch=0,got=0;uint32_t hdr=0;
    for(unsigned i=0;i<n;++i){
        uint32_t d;if(!v2_handler(r,w(r,a+4+4*i),&d))continue;
        ++handlers;
        if(!hdr){if(v2_header(r,d)){hdr=d;ch=h(r,d);}continue;}
        if(got<ch&&got<cap){
            if(!ok(r,d,(uint32_t)h(r,hdr+4)*4))return 0;   // a pattern table: orders entries
            chans[got++]=d;
        }
    }
    if(handlers<4||handlers+1<n||!hdr||got<ch)return 0;
    *header=hdr;return ch;
}
int gx_start_v2(GxPlayer *p,uint32_t header,const uint32_t *chans,unsigned n){
    if(!v2_header(&p->rom,header)||n!=h(&p->rom,header)||n>GX_CHANNELS)return 0;
    for(unsigned c=0;c<n;++c){if((chans[c]&3)||!ok(&p->rom,chans[c],(uint32_t)h(&p->rom,header+4)*4))return 0;p->chan[c]=chans[c];}
    p->v2=1;return start_common(p,header);
}
int gx_restart(GxPlayer *p){
    if(!p->v2)return gx_start(p,p->header);
    uint32_t chans[GX_CHANNELS];for(unsigned c=0;c<p->channels;++c)chans[c]=p->chan[c];
    return gx_start_v2(p,p->header,chans,p->channels);
}
uint32_t gx_frame_hz_q16(const GxPlayer *p){return ((uint32_t)p->mods.tempo*15290u);}// 59.7275 Hz * 65536 / 256
static unsigned random8(GxPlayer *p){p->rng^=p->rng<<13;p->rng^=p->rng>>17;p->rng^=p->rng<<5;return p->rng&255;}
// ---- playback ----
// Read from the GAX 2.01a replayer in Tang Tang (2026-11-06: its channel handlers, effect and perf-list
// jump tables, envelope, vibrato and mixer), with shinen-gax-python's replayer (MIT, tuned against GBA
// playback) for GAX 3. Where they agree both versions share the code.
// - Pitch is in 1/32 semitones in the driver (Q8 here): perf note + vibrato, plus the step's note and
//   the order's transpose unless the perf row is "fixed", plus the wave's fine tune. GAX 2's table puts
//   pitch 48 at 8372 Hz, GAX 3's (shinen-gax-python) at 8363.
// - A note without an instrument only moves the pitch (legato); an instrument restarts the envelope,
//   the perf list, the vibrato and the volume. Tone portamento (3xx) glides to the note over xx frames.
// - The level: envelope x perf volume x step volume x song volume, each / 256 (GAX 2: 255 is unity);
//   GAX 2's mixer adds each voice's sample x level >> 8, then scales the sum by (channels + 8) / 16
//   (unity for one voice) and clips to 8 bits. Here the scale is applied per voice, clamped to 255.
// - GAX 2 wave modulation is a loop window moving through the wave, the play position with it
//   (mod_shift: the caller tells the mixer). Echo and the output filter are not reproduced.
static const int8_t kSine[64]={0,12,24,37,48,60,70,80,90,98,106,112,117,122,125,126,127,126,125,122,117,112,106,98,
    90,80,70,60,48,37,24,12,0,-12,-24,-37,-48,-60,-70,-80,-90,-98,-106,-112,-117,-122,-125,-126,-127,-126,-125,-122,
    -117,-112,-106,-98,-90,-80,-70,-60,-48,-37,-24,-12};   // the vibrato sine, the same in GAX 2.01a and 3.05
static uint32_t step(int pitch,unsigned base){
    static const uint32_t ratio[13]={65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715,131072};
    pitch=clamp(pitch,0,127*256);unsigned key=(unsigned)pitch>>8,oct=key/12,semi=key%12;
    uint32_t r=ratio[semi]+(((ratio[semi+1]-ratio[semi])*(pitch&255))>>8);
    uint32_t value=(uint32_t)(((uint64_t)base*r)>>18);
    if(oct>=4)value<<=oct-4;else value>>=4-oct;
    return value<1?1:value>0x200000?0x200000:value;
}
static int valid_voice(GxPlayer *p,const GxVoice *v){return v->size&&v->size<=GX_MAX_SAMPLE&&ok(&p->rom,v->data,v->size+1)&&
    v->start<v->size&&(v->loop==GX_NOLOOP||v->loop<v->size)&&v->step&&v->step<=0x200000&&v->left<=255&&v->right<=255;}
static void levels(GxPlayer *p,unsigned c){
    GxTrack *t=&p->tracks[c];unsigned vol;
    if(p->v2){                         // the driver's chain: 255 is unity at each stage, the envelope's too
        vol=t->env_volume==255?256u:t->env_volume;
        if(t->perf_volume!=255)vol=(vol*t->perf_volume)>>8;
        if(t->volume!=255)vol=(vol*t->volume)>>8;
        vol=(vol*p->volume)>>8;
        if(p->channels>1)vol=(vol*(p->channels+8u))>>4;
    } else {vol=(t->volume*t->perf_volume)>>8;vol=(vol*t->env_volume)>>8;vol=(vol*p->volume)>>8;}
    if(p->mods.mute&(1u<<c))vol=0;
    t->voice.left=t->voice.right=(uint16_t)clamp((int)vol,0,255);
    int pitch=t->perf_pitch+t->vib+(t->fixed?0:t->pitch+t->transpose*256)+p->mods.pitch+t->fine_tune;
    t->voice.step=step(pitch,p->v2?8372:8363);
}
// The volume envelope, a frame: {count, sustain, loop start, loop end, then points of 8 bytes: u16 time
// at +4, s16 slope (x 256) at +6, u8 volume at +8}. Held at the sustain point and looped (loop end ->
// loop start) until note-off; past the last point it stays; a last point of 0 with no loop ending there
// silences the voice (*silent). The value between points is the stored slope's, as the driver's.
static unsigned envelope(GxPlayer *p,GxTrack *t,int *silent){
    const GxRom *r=&p->rom;uint32_t a=t->env;unsigned count=b(r,a),sus=b(r,a+1),start=b(r,a+2),end=b(r,a+3),time=t->env_time;
    *silent=0;t->env_time=(uint16_t)(time+1);
    if(sus!=255&&sus<count&&!t->released&&time==h(r,a+4+sus*8))t->env_time=(uint16_t)time;
    unsigned last=count-1;
    if(time>=h(r,a+4+last*8)){
        if(!b(r,a+8+last*8)&&(end==255||end<last))*silent=1;
        t->env_time=(uint16_t)time;
    }
    if(!t->released&&start!=255&&end!=255&&start<count&&end<count&&time==h(r,a+4+end*8))t->env_time=h(r,a+4+start*8);
    unsigned i=0;while(i<last&&h(r,a+4+i*8)<time)++i;
    if(h(r,a+4+i*8)==time||!i)return b(r,a+8+i*8);
    unsigned prev=h(r,a+4+(i-1)*8),offset=time-prev;int v0=b(r,a+8+(i-1)*8);
    if(!p->v2){                                            // GAX 3: interpolated (shinen-gax-python ignores the slope)
        int delta=(int)b(r,a+8+i*8)-v0;unsigned span=h(r,a+4+i*8)-prev;
        return (unsigned)(v0+(delta<0?-(int)divide((unsigned)-delta*offset,span):(int)divide((unsigned)delta*offset,span)));
    }
    int slope=(int16_t)h(r,a+4+i*8+2);
    return (unsigned)(v0+(((int)offset*slope)>>8))&255;
}
// An instrument (the driver's load): envelope, perf list, vibrato and volume start again; the pitch is
// the note's (not touched here). 0 if it is not one.
static int load_instrument(GxPlayer *p,unsigned c,unsigned id){
    GxTrack *t=&p->tracks[c];const GxRom *r=&p->rom;
    t->instrument_id=(uint16_t)id;
    int selected=(int)id+p->mods.voice_shift;
    if(selected<=0||selected>255){off(p,c);t->instrument=0;return 0;}
    uint32_t entry=p->instruments+4u*(unsigned)selected+(int32_t)p->mods.table_offset;
    if(!ok(r,entry,4)){off(p,c);t->instrument=0;return 0;}
    // V3: envelope +12, performance speed / count +16 / +17, rows +20, waves from +24. V2: wave blocks of
    // 28 bytes from +12, envelope +124, speed / count +132 / +133, rows +136 (a second envelope at +128 is
    // not used). Both: vibrato wait, depth, speed at +8 +9 +10.
    uint32_t a=w(r,entry);if(!ok(r,a,p->v2?140:24)||b(r,a)){off(p,c);t->instrument=0;return 0;}
    uint32_t env=w(r,a+(p->v2?124:12)),perf=w(r,a+(p->v2?136:20));unsigned count=b(r,a+(p->v2?133:17));
    if(!count||count>128||!ok(r,perf,count*8)||!ok(r,env,4)){off(p,c);t->instrument=0;return 0;}
    unsigned points=b(r,env);if(!points||points>64||!ok(r,env+4,points*8)){off(p,c);t->instrument=0;return 0;}
    for(unsigned i=1;i<points;++i)if(h(r,env+4+i*8)<=h(r,env+4+(i-1)*8)){off(p,c);t->instrument=0;return 0;}
    // GAX 2 leaves the sounding wave, the perf pitch and the perf volume alone: the perf list's next
    // trigger replaces them. GAX 3 (as this player always did) starts from silence.
    if(!p->v2){off(p,c);t->perf_pitch=0;t->fixed=0;t->fine_tune=0;t->perf_volume=255;t->has_wave=0;t->slot=255;}
    t->instrument=a;t->env=env;t->perf=perf;t->perf_count=count;t->perf_index=0;t->perf_loop=0;
    t->perf_speed=b(r,a+(p->v2?132:16));t->perf_wait=0;t->env_time=0;t->released=0;t->volume=255;
    t->env_volume=b(r,env+8);t->perf_slide=t->perf_vol_slide=0;t->porta_rate=0;t->active=t->gate=1;
    t->vib_wait=b(r,a+8);t->vib_phase=0;t->vib=0;
    return 1;
}
// A step's note: pitch (legato without an instrument), the instrument if there is one.
static void note_on(GxPlayer *p,unsigned c,unsigned note,unsigned ins,unsigned porta){
    GxTrack *t=&p->tracks[c];
    t->note=(uint8_t)clamp((int)note+11+t->transpose,0,127);++t->notes;t->activity=255;t->gate=1;
    if(random8(p)<p->mods.drop_chance)return;
    if(!porta){t->pitch=((int)note-2)*256;t->released=0;}
    if(ins)load_instrument(p,c,ins);
    else if(!t->instrument)off(p,c);
}
static int wave(GxPlayer *p,unsigned c,unsigned slot){
    GxTrack *t=&p->tracks[c];const GxRom *r=&p->rom;
    if(slot>=4){off(p,c);return 0;}
    unsigned id=b(r,t->instrument+1+slot);if(!id){off(p,c);return 0;}
    // A wave's settings: V3 24 bytes from +24 per used slot {fine s16, modulation, ping-pong, start,
    // loop, end}; V2 28 bytes from +12 per slot {modulation, -, -, -, start, loop start, loop end,
    // modulation size, step, speed u16, fine s16}: it loops [loop start, loop end) when start < end,
    // else plays to the sample's end; modulated, it loops the window [start, start + size) and moves it.
    uint32_t param=t->instrument+24,start_at=param+4,loop_at=param+8,end_at=param+12;
    if(p->v2){param=t->instrument+12+slot*28u;start_at=param+4;loop_at=param+8;end_at=param+12;}
    else for(unsigned i=0;i<slot;++i)if(b(r,t->instrument+1+i)){param+=24;start_at+=24;loop_at+=24;end_at+=24;}
    uint32_t rec=p->samples+id*8;
    if(!ok(r,param,28)||!ok(r,rec,8)){off(p,c);return 0;}
    uint32_t data=w(r,rec),size=w(r,rec+4);
    if(!size||size>GX_MAX_SAMPLE||!ok(r,data,size+1)){off(p,c);return 0;}
    if(!p->v2&&b(r,param+2))unsupported(p,GX_FEATURE_MODULATION);
    if(!p->v2&&b(r,param+3))unsupported(p,GX_FEATURE_PINGPONG);
    unsigned end=w(r,end_at),loop=w(r,loop_at),mod=0;
    t->mod_on=0;t->mod_shift=0;
    if(p->v2){
        int32_t lo=(int32_t)loop,hi=(int32_t)end,msize=(int32_t)w(r,param+16),mstep=(int32_t)w(r,param+20),s0=(int32_t)w(r,start_at);
        if(b(r,param)&&lo<hi&&msize>0&&mstep>0&&h(r,param+24)&&s0>=0&&lo>=0&&(uint32_t)hi<=size&&s0+msize<=hi){
            mod=1;t->mod_pos=(uint32_t)s0;t->mod_dir=1;t->mod_wait=(uint8_t)h(r,param+24);
            size=(uint32_t)hi;t->voice.loop=(uint32_t)s0;  // validated to the window's reach, then the window
        } else {t->voice.loop=GX_NOLOOP;if(loop<end&&end<=size){size=end;t->voice.loop=loop;}}
    }
    else{if(end){if(end>size||loop>=end){off(p,c);return 0;}size=end;}t->voice.loop=end?loop:GX_NOLOOP;}
    if(!mod){unsigned chop=(size*p->mods.sample_end)>>8;size-=chop;}
    if(!size){off(p,c);return 0;}if(t->voice.loop>=size)t->voice.loop=GX_NOLOOP;
    data+=(uint32_t)p->mods.scrub;if(!ok(r,data,size+1)){off(p,c);return 0;}
    t->voice.data=data;t->voice.size=mod?t->mod_pos+w(r,param+16):size;
    uint32_t start=w(r,start_at),extra=mod?0:(size*p->mods.sample_start)>>8;
    if(start>=size||extra>=size-start)start=size-1;else start+=extra;
    t->voice.start=start;t->voice.reverse=random8(p)<p->mods.reverse_chance;
    t->fine_tune=(int16_t)h(r,p->v2?param+26:param)*8;t->slot=slot;t->has_wave=1;
    levels(p,c);
    GxVoice before=t->voice;
    if(p->hooks.note)p->hooks.note(p->hooks.user,c,&t->voice);
    if(!valid_voice(p,&t->voice)){off(p,c);return 0;}
    // The window moves only on the wave it was set up for, forwards (CHAOS may have swapped it).
    if(mod&&!t->voice.reverse&&t->voice.data==before.data&&t->voice.size==before.size&&t->voice.loop==before.loop){
        t->mod_on=1;t->mod_size=w(r,param+16);t->mod_step=w(r,param+20);t->mod_lo=loop;t->mod_hi=end;t->mod_speed=(uint8_t)h(r,param+24);
    } else if(mod)unsupported(p,GX_FEATURE_MODULATION);
    if(p->hooks.start)p->hooks.start(p->hooks.user,c,&t->voice);
    return 1;
}
// The instrument's performance list, a row: {note (0 none), fixed, wave slot + 1, -, two effects
// {param, fx}}. A wave trigger (V2; V3 on any note) resets the perf volume. Effects: 1/2 slide, 5 jump
// (counted down by 6's loop count), 10/11 volume slide, 12 volume, 15 speed.
static void performance(GxPlayer *p,unsigned c){
    GxTrack *t=&p->tracks[c];const GxRom *r=&p->rom;
    if(p->v2&&!t->perf_speed)return;                       // GAX 2: speed 0 never steps
    if(t->perf_wait){--t->perf_wait;return;}
    if(t->perf_index>=t->perf_count)return;
    uint32_t a=t->perf+t->perf_index++*8;unsigned note=b(r,a),slot=b(r,a+2);
    t->perf_slide=t->perf_vol_slide=0;
    if(note){t->perf_pitch=((int)note-2)*256;t->fixed=b(r,a+1)!=0;if(!p->v2||slot)t->perf_volume=255;}
    for(unsigned i=0;i<2;++i){unsigned param=b(r,a+4+i*2),fx=b(r,a+5+i*2);
        switch(fx){
        case 0:break;
        case 1:t->perf_slide=(int16_t)(param*8);break;
        case 2:t->perf_slide=-(int16_t)(param*8);break;
        case 5:if(t->perf_loop&&!--t->perf_loop)break;
            if(param<t->perf_count)t->perf_index=(uint16_t)param;else unsupported(p,GX_PERF_EFFECT+fx);break;
        case 6:if(!t->perf_loop)t->perf_loop=(uint8_t)(param?param+1:0);break;
        case 10:t->perf_vol_slide=param;break;
        case 11:t->perf_vol_slide=-(int16_t)param;break;
        case 12:t->perf_volume=param;break;
        case 15:t->perf_speed=param;break;
        default:unsupported(p,GX_PERF_EFFECT+fx);break;
        }
    }
    if(note&&slot)wave(p,c,slot-1);
    if(!t->perf_speed)t->perf_index=t->perf_count;
    else t->perf_wait=t->perf_speed-1;
}
static void step_effect(GxPlayer *p,unsigned c,unsigned note,unsigned fx,unsigned param){
    GxTrack *t=&p->tracks[c];
    switch(fx){
    case 0:break;
    case 1:t->step_slide=(int16_t)(param*8);break;
    case 2:t->step_slide=-(int16_t)(param*8);break;
    case 3:if(param&&note>1){                               // tone portamento over param frames
        t->porta_target=((int)note-2)*256;int32_t d=(t->porta_target-t->pitch)>>3;   // in the driver's 1/32 units
        t->porta_rate=(int16_t)(8*(d<0?-(int32_t)divide((uint32_t)-d,param):(int32_t)divide((uint32_t)d,param)));}
        break;
    case 7:p->speed[0]=param>>4;p->speed[1]=param&15;p->phase=0;break;   // this row the high nibble, then the low
    case 10:t->vol_slide=param;break;
    case 11:t->vol_slide=-(int16_t)param;break;
    case 12:t->volume=param;break;
    case 13:p->break_pattern=1;break;
    case 14:if((param>>4)==0xD)break;unsupported(p,fx);break; // EDx is a note delay (read_row)
    case 15:p->speed[0]=p->speed[1]=param;break;
    default:unsupported(p,fx);break;
    }
}
static int read_row(GxPlayer *p){
    for(unsigned c=0;c<p->channels;++c){
        GxTrack *t=&p->tracks[c];uint8_t note,ins,fx,param;
        t->step_slide=t->vol_slide=0;t->delay=0;
        if(!decode(p,t,&note,&ins,&fx,&param,p->rows-p->row))return 0;
        if(fx==14&&(param>>4)==0xD&&(param&15)&&note){t->delay=param&15;t->delay_note=note;t->delay_ins=ins;continue;}
        if(note>1)note_on(p,c,note,ins,fx==3);
        if(note==1){t->released=1;t->gate=0;if(t->active&&b(&p->rom,t->env+1)==255)off(p,c);}
        step_effect(p,c,note,fx,param);
    }
    event(p,GX_EVENT_ROW,((uint32_t)p->order<<16)|p->row);return 1;
}
void gx_update(GxPlayer *p){
    for(unsigned c=0;c<p->channels;++c){GxTrack *t=&p->tracks[c];if(!t->active||!t->has_wave)continue;
        if(p->hooks.done&&p->hooks.done(p->hooks.user,c)){off(p,c);continue;}
        levels(p,c);if(p->hooks.update)p->hooks.update(p->hooks.user,c,&t->voice);
    }
}
void gx_frame(GxPlayer *p){
    if(!p->running)return;
    const GxRom *r=&p->rom;
    for(unsigned c=0;c<p->channels;++c){GxTrack *t=&p->tracks[c];   // EDx: x frames after its row, before a new row
        if(t->delay&&!--t->delay){unsigned n=t->delay_note;
            if(n>1)note_on(p,c,n,t->delay_ins,0);
            if(n==1){t->released=1;t->gate=0;if(t->active&&b(r,t->env+1)==255)off(p,c);}}}
    if(!p->wait&&!p->hold_rows){if(!read_row(p))return;unsigned speed=p->speed[p->phase];p->phase^=1;if(!speed)p->hold_rows=1;else p->wait=speed;}
    for(unsigned c=0;c<p->channels;++c){GxTrack *t=&p->tracks[c];
        if(t->active&&t->instrument){int silent;t->env_volume=(uint8_t)envelope(p,t,&silent);
            unsigned depth=b(r,t->instrument+9);                   // vibrato: wait, then a sine at speed
            if(!depth)t->vib=0;
            else{if(t->vib_wait)--t->vib_wait;else t->vib_phase=(uint8_t)((t->vib_phase+b(r,t->instrument+10))&63);
                t->vib=(int16_t)(((kSine[t->vib_phase]*(int)depth)>>8)*8);}
            performance(p,c);
            if(t->mod_on&&t->has_wave&&!--t->mod_wait){            // GAX 2 wave modulation: move the window
                t->mod_wait=t->mod_speed;uint32_t was=t->mod_pos;
                if(t->mod_dir>0){t->mod_pos+=t->mod_step;if(t->mod_pos+t->mod_size>t->mod_hi){t->mod_pos-=2*t->mod_step;t->mod_dir=-1;}}
                else{t->mod_pos-=t->mod_step;if((int32_t)t->mod_pos<(int32_t)t->mod_lo){t->mod_pos+=2*t->mod_step;t->mod_dir=1;}}
                if((int32_t)t->mod_pos<0||t->mod_pos+t->mod_size>t->mod_hi)t->mod_on=0,t->mod_pos=was;   // a step wider than its range
                else{t->mod_shift+=(int32_t)(t->mod_pos-was);t->voice.loop=t->mod_pos;t->voice.size=t->mod_pos+t->mod_size;}
            }
            t->volume=(uint8_t)clamp(t->volume+t->vol_slide,0,255);
            t->perf_volume=(uint8_t)clamp(t->perf_volume+t->perf_vol_slide,0,255);
            t->pitch=clamp(t->pitch+t->step_slide,-32768,32767);
            t->perf_pitch=clamp(t->perf_pitch+t->perf_slide,-32768,32767);
            if(t->porta_rate){int32_t before=t->porta_target-t->pitch;t->pitch+=t->porta_rate;   // stops crossing the note
                if((before<0)!=(t->porta_target-t->pitch<0)||t->pitch==t->porta_target){t->pitch=t->porta_target;t->porta_rate=0;}}
            if(silent||(!t->env_volume&&t->released))off(p,c);
        } else if(!t->active&&t->porta_rate){t->pitch=t->porta_target;t->porta_rate=0;}
        if(t->activity)t->activity-=((t->activity>>3)+1);
    }
    gx_update(p);++p->frames;
    if(p->hold_rows||!p->wait||--p->wait)return;
    if(++p->row<p->rows&&!p->break_pattern)return;
    unsigned order=p->order+1;
    if(order>=p->orders){if(!p->repeat){gx_stop(p);return;}order=p->restart;++p->loops;event(p,GX_EVENT_LOOP,p->loops);}
    if(!position(p,order,0))fail(p,p->header);
}
