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
static void off(GxPlayer *p,unsigned c){p->tracks[c].active=p->tracks[c].gate=p->tracks[c].has_wave=0;if(p->hooks.stop)p->hooks.stop(p->hooks.user,c);}
void gx_stop(GxPlayer *p){for(unsigned n=0;n<GX_CHANNELS;++n)off(p,n);p->running=0;event(p,GX_EVENT_STOP,0);}
static int fail(GxPlayer *p,uint32_t a){p->error=1;gx_stop(p);event(p,GX_EVENT_BAD_DATA,a);return 0;}
void gx_init(GxPlayer *p,GxRom r,const GxHooks *hooks){
    zero(p,sizeof(*p));p->rom.data=r.data;p->rom.base=r.base;p->rom.size=r.size;
    if(!r.data||r.size>0x2000000||r.base>UINT32_MAX-r.size)p->rom.size=0;
    if(hooks){p->hooks.user=hooks->user;p->hooks.start=hooks->start;p->hooks.update=hooks->update;p->hooks.stop=hooks->stop;
        p->hooks.done=hooks->done;p->hooks.note=hooks->note;p->hooks.event=hooks->event;}
    p->mods.tempo=256;p->repeat=1;p->rng=0x47415833;
}
int gx_song_valid(const GxRom *r,uint32_t a){
    if((a&3)||!ok(r,a,32))return 0;
    unsigned ch=h(r,a),rows=h(r,a+2),orders=h(r,a+4);
    if(!ch||ch>32||!rows||rows>256||!orders||orders>1024||h(r,a+6)>=orders||h(r,a+8)>256||h(r,a+30)||!ok(r,a+32,ch*4))return 0;
    unsigned rate=h(r,a+24);if(rate<4000||rate>48000)return 0;
    uint32_t notes=w(r,a+12),ins=w(r,a+16),samples=w(r,a+20);
    if((notes|ins|samples)&3||!ok(r,notes,1)||!ok(r,ins,4)||!ok(r,samples,8))return 0;
    if(!ok(r,w(r,ins),24)||!ok(r,w(r,samples),1)||w(r,samples+4))return 0;
    for(unsigned c=0;c<ch;++c){
        uint32_t seq=w(r,a+32+4*c);
        if((seq&3)||!ok(r,seq,orders*4))return 0;
        for(unsigned order=0;order<orders;++order){
            uint32_t pattern=notes+h(r,seq+order*4);
            if(!ok(r,pattern,1)||b(r,pattern)>1||b(r,seq+order*4+3))return 0;
        }
    }
    return 1;
}
unsigned gx_scan(const GxRom *r,uint32_t begin,uint32_t end,uint32_t *out,unsigned cap){
    unsigned n=0;if(!out||end<begin||!ok(r,begin,end-begin))return 0;
    for(uint32_t a=begin;a<end&&end-a>=4&&n<cap;a+=4)if(gx_song_valid(r,a))out[n++]=a;
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
        GxTrack *t=&p->tracks[c];uint32_t seq=w(&p->rom,p->header+32+c*4);
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
int gx_start(GxPlayer *p,uint32_t header){
    if(!gx_song_valid(&p->rom,header))return 0;
    gx_stop(p);zero(p->tracks,sizeof(p->tracks));zero(p->unsupported,sizeof(p->unsupported));
    p->header=header;p->channels=h(&p->rom,header);p->rows=h(&p->rom,header+2);p->orders=h(&p->rom,header+4);
    p->restart=h(&p->rom,header+6);p->volume=h(&p->rom,header+8);p->mixing_rate=h(&p->rom,header+24);
    p->patterns=w(&p->rom,header+12);p->instruments=w(&p->rom,header+16);p->samples=w(&p->rom,header+20);
    p->speed[0]=p->speed[1]=6;p->phase=0;p->frames=0;p->error=0;p->running=1;
    if(!position(p,0,0))return fail(p,header);
    return 1;
}
int gx_restart(GxPlayer *p){return gx_start(p,p->header);}
uint32_t gx_frame_hz_q16(const GxPlayer *p){return ((uint32_t)p->mods.tempo*15290u);}// 59.7275 Hz * 65536 / 256
static unsigned random8(GxPlayer *p){p->rng^=p->rng<<13;p->rng^=p->rng>>17;p->rng^=p->rng<<5;return p->rng&255;}
// Equal temperament: GAX pitch 48 is 8363 Hz. Lookup values are mathematical Q16 ratios.
static uint32_t step(int pitch){
    static const uint32_t ratio[13]={65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715,131072};
    pitch=clamp(pitch,0,127*256);unsigned key=(unsigned)pitch>>8,oct=key/12,semi=key%12;
    uint32_t r=ratio[semi]+(((ratio[semi+1]-ratio[semi])*(pitch&255))>>8);
    uint32_t value=(uint32_t)(((uint64_t)8363*r)>>18);
    if(oct>=4)value<<=oct-4;else value>>=4-oct;
    return value<1?1:value>0x200000?0x200000:value;
}
static int valid_voice(GxPlayer *p,const GxVoice *v){return v->size&&v->size<=GX_MAX_SAMPLE&&ok(&p->rom,v->data,v->size+1)&&
    v->start<v->size&&(v->loop==GX_NOLOOP||v->loop<v->size)&&v->step&&v->step<=0x200000&&v->left<=255&&v->right<=255;}
static void levels(GxPlayer *p,unsigned c){
    GxTrack *t=&p->tracks[c];unsigned vol=(t->volume*t->perf_volume)>>8;vol=(vol*t->env_volume)>>8;vol=(vol*p->volume)>>8;
    if(p->mods.mute&(1u<<c))vol=0;
    t->voice.left=t->voice.right=(uint16_t)clamp(vol,0,255);
    int pitch=t->perf_pitch+(t->fixed?0:t->pitch+t->transpose*256)+p->mods.pitch+t->fine_tune;
    // Fine tuning is 1/32 of a semitone, already included in perf_pitch when resolving a slot.
    t->voice.step=step(pitch);
}
static unsigned envelope(GxPlayer *p,GxTrack *t){
    const GxRom *r=&p->rom;uint32_t a=t->env;unsigned count=b(r,a),sus=b(r,a+1),start=b(r,a+2),end=b(r,a+3),time=t->env_time;
    int held=!t->released&&sus<count&&time>=h(r,a+4+sus*8);
    if(held)time=h(r,a+4+sus*8);
    if(!t->released&&start<count&&end<count&&start<=end&&time>=h(r,a+4+end*8))time=h(r,a+4+start*8);
    unsigned last_time=h(r,a+4+(count-1)*8);
    if(time>=last_time){t->env_time=(uint16_t)last_time;return b(r,a+8+(count-1)*8);}
    unsigned value=b(r,a+8);
    for(unsigned i=1;i<count;++i){
        unsigned at=h(r,a+4+i*8),prev=h(r,a+4+(i-1)*8);
        if(time<=at){int v0=b(r,a+8+(i-1)*8),delta=(int)b(r,a+8+i*8)-v0;
            unsigned offset=time>prev?time-prev:0;
            value=(unsigned)(v0+(delta<0?-(int)divide((unsigned)-delta*offset,at-prev):(int)divide((unsigned)delta*offset,at-prev)));break;}
    }
    t->env_time=(uint16_t)(held?time:time+1);return value;
}
static int instrument(GxPlayer *p,unsigned c,unsigned id,unsigned note){
    GxTrack *t=&p->tracks[c];const GxRom *r=&p->rom;
    t->note=(uint8_t)clamp((int)note+11+t->transpose,0,127);++t->notes;t->activity=255;t->gate=1;
    if(random8(p)<p->mods.drop_chance)return 1;
    if(id)t->instrument_id=id;
    int selected=(int)t->instrument_id+p->mods.voice_shift;
    if(selected<=0||selected>255){off(p,c);return 1;}
    uint32_t entry=p->instruments+4u*(unsigned)selected+(int32_t)p->mods.table_offset;
    if(!ok(r,entry,4)){off(p,c);return 1;}
    uint32_t a=w(r,entry);if(!ok(r,a,24)||b(r,a)){off(p,c);return 1;}
    uint32_t env=w(r,a+12),perf=w(r,a+20);unsigned count=b(r,a+17);
    if(!count||count>128||!ok(r,perf,count*8)||!ok(r,env,4)){off(p,c);return 1;}
    unsigned points=b(r,env);if(!points||points>64||!ok(r,env+4,points*8)){off(p,c);return 1;}
    for(unsigned i=1;i<points;++i)if(h(r,env+4+i*8)<=h(r,env+4+(i-1)*8)){off(p,c);return 1;}
    off(p,c);t->instrument=a;t->env=env;t->perf=perf;t->perf_count=count;t->perf_index=0;
    t->perf_speed=b(r,a+16);t->perf_wait=0;t->env_time=0;t->released=0;t->volume=t->perf_volume=255;
    t->env_volume=b(r,env+8);t->pitch=((int)note-2)*256;t->perf_pitch=0;t->perf_slide=t->perf_vol_slide=0;
    t->fixed=t->has_wave=0;t->fine_tune=0;t->active=t->gate=1;t->slot=255;
    if(b(r,a+8)||b(r,a+9)||b(r,a+10))unsupported(p,GX_FEATURE_VIBRATO);
    return 1;
}
static int wave(GxPlayer *p,unsigned c,unsigned slot){
    GxTrack *t=&p->tracks[c];const GxRom *r=&p->rom;
    if(slot>=4){off(p,c);return 0;}
    unsigned id=b(r,t->instrument+1+slot);if(!id){off(p,c);return 0;}
    uint32_t param=t->instrument+24;
    for(unsigned i=0;i<slot;++i)if(b(r,t->instrument+1+i))param+=24;
    uint32_t rec=p->samples+id*8;
    if(!ok(r,param,24)||!ok(r,rec,8)){off(p,c);return 0;}
    uint32_t data=w(r,rec),size=w(r,rec+4);
    if(!size||size>GX_MAX_SAMPLE||!ok(r,data,size+1)){off(p,c);return 0;}
    if(b(r,param+2))unsupported(p,GX_FEATURE_MODULATION);
    if(b(r,param+3))unsupported(p,GX_FEATURE_PINGPONG);
    unsigned end=w(r,param+12),loop=w(r,param+8);
    if(end){if(end>size||loop>=end){off(p,c);return 0;}size=end;}
    t->voice.loop=end?loop:GX_NOLOOP;
    unsigned chop=(size*p->mods.sample_end)>>8;size-=chop;
    if(!size){off(p,c);return 0;}if(t->voice.loop>=size)t->voice.loop=GX_NOLOOP;
    data+=(uint32_t)p->mods.scrub;if(!ok(r,data,size+1)){off(p,c);return 0;}
    t->voice.data=data;t->voice.size=size;
    uint32_t start=w(r,param+4),extra=(size*p->mods.sample_start)>>8;
    if(start>=size||extra>=size-start)start=size-1;else start+=extra;
    t->voice.start=start;t->voice.reverse=random8(p)<p->mods.reverse_chance;
    t->fine_tune=(int16_t)h(r,param)*8;t->slot=slot;t->has_wave=1;
    levels(p,c);
    if(p->hooks.note)p->hooks.note(p->hooks.user,c,&t->voice);
    if(!valid_voice(p,&t->voice)){off(p,c);return 0;}
    if(p->hooks.start)p->hooks.start(p->hooks.user,c,&t->voice);
    return 1;
}
static void performance(GxPlayer *p,unsigned c){
    GxTrack *t=&p->tracks[c];const GxRom *r=&p->rom;
    if(t->perf_wait){--t->perf_wait;return;}
    if(t->perf_index>=t->perf_count)return;
    uint32_t a=t->perf+t->perf_index++*8;unsigned note=b(r,a),slot=b(r,a+2);
    t->perf_slide=t->perf_vol_slide=0;
    if(note){t->perf_pitch=((int)note-2)*256;t->fixed=b(r,a+1)!=0;t->perf_volume=255;}
    for(unsigned i=0;i<2;++i){unsigned param=b(r,a+4+i*2),fx=b(r,a+5+i*2);
        switch(fx){
        case 0:break;
        case 1:t->perf_slide=(int16_t)(param*8);break;
        case 2:t->perf_slide=-(int16_t)(param*8);break;
        case 5:if(param<t->perf_count)t->perf_index=param;else unsupported(p,GX_PERF_EFFECT+fx);break;
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
static int read_row(GxPlayer *p){
    for(unsigned c=0;c<p->channels;++c){
        GxTrack *t=&p->tracks[c];uint8_t note,ins,fx,param;
        t->step_slide=t->vol_slide=0;
        if(!decode(p,t,&note,&ins,&fx,&param,p->rows-p->row))return 0;
        if(note>1)instrument(p,c,ins,note);
        if(note==1){t->released=1;t->gate=0;if(t->active&&b(&p->rom,t->env+1)==255)off(p,c);}
        switch(fx){
        case 0:break;
        case 1:t->step_slide=(int16_t)(param*8);break;
        case 2:t->step_slide=-(int16_t)(param*8);break;
        case 7:p->speed[0]=param&15;p->speed[1]=param>>4;break;
        case 10:t->vol_slide=param;break;
        case 11:t->vol_slide=-(int16_t)param;break;
        case 12:t->volume=param;break;
        case 13:p->break_pattern=1;break;
        case 15:p->speed[0]=p->speed[1]=param;break;
        default:unsupported(p,fx);break;
        }
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
    if(!p->wait&&!p->hold_rows){if(!read_row(p))return;unsigned speed=p->speed[p->phase];p->phase^=1;if(!speed)p->hold_rows=1;else p->wait=speed;}
    for(unsigned c=0;c<p->channels;++c){GxTrack *t=&p->tracks[c];
        if(t->active){t->env_volume=(uint8_t)envelope(p,t);performance(p,c);
            t->pitch=clamp(t->pitch+t->step_slide,-32768,32767);
            t->perf_pitch=clamp(t->perf_pitch+t->perf_slide,-32768,32767);
            t->volume=(uint8_t)clamp(t->volume+t->vol_slide,0,255);
            t->perf_volume=(uint8_t)clamp(t->perf_volume+t->perf_vol_slide,0,255);
            if(!t->env_volume&&t->released)off(p,c);
        }
        if(t->activity)t->activity-=((t->activity>>3)+1);
    }
    gx_update(p);++p->frames;
    if(p->hold_rows||!p->wait||--p->wait)return;
    if(++p->row<p->rows&&!p->break_pattern)return;
    unsigned order=p->order+1;
    if(order>=p->orders){if(!p->repeat){gx_stop(p);return;}order=p->restart;++p->loops;event(p,GX_EVENT_LOOP,p->loops);}
    if(!position(p,order,0))fail(p,p->header);
}
