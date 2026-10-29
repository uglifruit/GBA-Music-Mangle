// Independent Krawall format decoder; no heap, libc, libgcc division or hardware ownership.
#include "krawall.h"

static void zero(void *v, unsigned n) { volatile uint8_t *p=v; while(n--) *p++=0; }
static int clip(int x,int lo,int hi) { return x<lo?lo:x>hi?hi:x; }
static int range(const KrRom *r,uint32_t a,uint32_t n) {
    return a>=r->base && a-r->base<=r->size && n<=r->size-(a-r->base);
}
static uint8_t u8(const KrRom *r,uint32_t a) { return r->data[a-r->base]; }
static uint16_t u16(const KrRom *r,uint32_t a) { return u8(r,a)|((uint16_t)u8(r,a+1)<<8); }
static uint32_t u32(const KrRom *r,uint32_t a) {
    return u16(r,a)|((uint32_t)u16(r,a+2)<<16);
}
static uint32_t random8(KrPlayer *p) {
    p->rng^=p->rng<<13;p->rng^=p->rng>>17;p->rng^=p->rng<<5;return p->rng&255;
}
static void event(KrPlayer *p,unsigned e,uint32_t v) {
    if(p->hooks.event) p->hooks.event(p->hooks.user,e,v);
}
static void off(KrPlayer *p,unsigned c) {
    p->tracks[c].active=p->tracks[c].gate=0;
    if(p->hooks.stop) p->hooks.stop(p->hooks.user,c);
}
void kr_stop(KrPlayer *p) {
    for(unsigned c=0;c<KR_CHANNELS;++c) off(p,c);
    p->running=0;event(p,KR_EVENT_STOP,0);
}
static int fail(KrPlayer *p) {
    p->error=1;kr_stop(p);event(p,KR_EVENT_BAD_DATA,p->stream);return 0;
}
void kr_init(KrPlayer *p,KrRom r,const KrHooks *h) {
    zero(p,sizeof(*p));p->rom.data=r.data;p->rom.base=r.base;p->rom.size=r.size;
    if(!r.data||r.size>0x02000000u||r.base>UINT32_MAX-r.size)p->rom.size=0;
    if(h) {
        p->hooks.user=h->user;p->hooks.start=h->start;p->hooks.update=h->update;
        p->hooks.stop=h->stop;p->hooks.done=h->done;p->hooks.note=h->note;p->hooks.event=h->event;
    }
    p->mods.tempo=256;p->repeat=1;p->rng=0x4b524157;
}
static uint32_t pattern_addr(const KrRom *r,uint32_t m,unsigned order) {
    unsigned n=u8(r,m+3+order);
    if(n>=254||!range(r,m+364,4*n+4)) return 0;
    return u32(r,m+364+4*n);
}
static unsigned pattern_rows(const KrRom *r,uint32_t a,unsigned old) {
    if(!range(r,a,old?33:34)) return 0;
    return old?u8(r,a+32):u16(r,a+32);
}
int kr_module_valid(const KrRom *r,uint32_t m,uint8_t format) {
    if(!r->data||format>KR_2003||!range(r,m,368)) return 0;
    unsigned ch=u8(r,m),orders=u8(r,m+1);
    if(!ch||ch>32||!orders||u8(r,m+2)>=orders||!u8(r,m+356)||
       u8(r,m+356)>31||u8(r,m+357)<32||u8(r,m+355)>128||u8(r,m+363)) return 0;
    for(unsigned i=358;i<363;++i) if(u8(r,m+i)>1) return 0;
    unsigned real=0;
    for(unsigned i=0;i<orders;++i) {
        if(u8(r,m+3+i)>=254) continue;
        uint32_t a=pattern_addr(r,m,i);unsigned n=pattern_rows(r,a,format);
        if(!a||!n||n>256||u16(r,a)!=0) return 0;
        ++real;
    }
    return real!=0;
}
unsigned kr_scan(const KrRom *r,uint32_t begin,uint32_t end,uint8_t f,uint32_t *out,unsigned cap) {
    unsigned n=0;
    if(!out||!cap||end<begin) return 0;
    for(uint32_t a=begin;a<end&&end-a>=368&&n<cap;) {
        if(kr_module_valid(r,a,f)) out[n++]=a;
        if(a>UINT32_MAX-4) break;
        a+=4;
    }
    return n;
}
static int get(KrPlayer *p,uint8_t *v) {
    if(!range(&p->rom,p->stream,1)) return fail(p);
    *v=u8(&p->rom,p->stream++);return 1;
}
// Packed rows have a strict byte budget; malformed streams cannot block the audio service.
static int skip_row(KrPlayer *p) {
    for(unsigned i=0;i<256;++i) {
        uint8_t f,n,v;
        if(!get(p,&f)) return 0;
        if(!f) return 1;
        if(f&32) {
            if(!get(p,&n)||!get(p,&v)) return 0;
            if(!p->format&&(n&128)&&!get(p,&v)) return 0;
        }
        if((f&64)&&!get(p,&v)) return 0;
        if((f&128)&&(!get(p,&v)||!get(p,&v))) return 0;
    }
    return fail(p);
}
static int position(KrPlayer *p,unsigned order,unsigned row) {
    if(order>=p->orders) return 0;
    // 254 = skip order, 255 = end. Finite skip budget avoids cycles.
    for(unsigned i=0;i<p->orders;++i) {
        unsigned x=u8(&p->rom,p->song.module+3+order);
        if(x==255) return 0;
        if(x!=254) break;
        if(++order>=p->orders) return 0;
    }
    uint32_t a=pattern_addr(&p->rom,p->song.module,order);
    unsigned rows=pattern_rows(&p->rom,a,p->format);
    if(!a||!rows||rows>256||row>=rows) return 0;
    p->pattern=a;p->stream=a+(p->format?33:34);p->rows=rows;
    p->order=order;p->row=row;p->tick=0;
    // Do not trust the optional row index: decoding at most 255 rows is bounded.
    for(unsigned i=0;i<row;++i) if(!skip_row(p)) return 0;
    return 1;
}
int kr_seek(KrPlayer *p,unsigned order,unsigned row) {
    if(!p->running) return 0;
    if(!position(p,order,row)) return fail(p);
    for(unsigned i=0;i<KR_CHANNELS;++i) off(p,i);
    p->pending_order=p->pending_row=UINT16_MAX;
    return 1;
}
int kr_start(KrPlayer *p,const KrSong *s) {
    if(!kr_module_valid(&p->rom,s->module,s->format)||!s->sample_count||
       !range(&p->rom,s->samples,4u*s->sample_count)||
       (u8(&p->rom,s->module+358)&&(!s->instrument_count||!range(&p->rom,s->instruments,4u*s->instrument_count)))) return 0;
    kr_stop(p);zero(p->tracks,sizeof(p->tracks));zero(p->unsupported,sizeof(p->unsupported));
    p->song.module=s->module;p->song.samples=s->samples;p->song.instruments=s->instruments;
    p->song.sample_count=s->sample_count;p->song.instrument_count=s->instrument_count;p->song.format=s->format;
    p->format=s->format;p->channels=u8(&p->rom,s->module);p->orders=u8(&p->rom,s->module+1);
    p->restart=u8(&p->rom,s->module+2);p->speed=u8(&p->rom,s->module+356);
    p->bpm=u8(&p->rom,s->module+357);p->global_volume=u8(&p->rom,s->module+355);
    p->instrument_based=u8(&p->rom,s->module+358);p->error=0;p->ticks=0;
    p->pending_order=p->pending_row=UINT16_MAX;
    for(unsigned c=0;c<p->channels;++c) {
        p->tracks[c].pan=(int8_t)u8(&p->rom,s->module+259+c);p->tracks[c].channel_volume=64;
    }
    p->running=1;
    if(!position(p,0,0)) return fail(p);
    return 1;
}
int kr_restart(KrPlayer *p) { KrSong s; s.module=p->song.module;s.samples=p->song.samples;
    s.instruments=p->song.instruments;s.sample_count=p->song.sample_count;
    s.instrument_count=p->song.instrument_count;s.format=p->song.format;return kr_start(p,&s); }
uint32_t kr_tick_hz_q16(const KrPlayer *p) { return ((uint32_t)p->bpm*p->mods.tempo*512u)/5; }

// Equal-tempered pitch, C-4 (MIDI 60) = 8363 Hz. Tables generated mathematically,
// not copied from the reference implementation. Fine tune is 1/128 semitone.
static uint32_t step_for(int pitch) {
    static const uint32_t ratios[13]={65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715,131072};
    pitch=clip(pitch,0,127*256);
    unsigned key=(unsigned)pitch>>8,oct=key/12,semi=key%12,fine=pitch&255;
    uint32_t ratio=ratios[semi]+(((ratios[semi+1]-ratios[semi])*fine)>>8);
    uint32_t step=(uint32_t)(((uint64_t)8363*ratio)>>18); // 8363*4096/16384 * Q16
    if(oct>=5) step<<=oct-5;else step>>=5-oct;
    return step<1?1:step>0x200000?0x200000:step;
}
static int valid_voice(KrPlayer *p,const KrVoice *v) {
    return v->size&&v->size<=KR_MAX_SAMPLE&&range(&p->rom,v->data,v->size+1)&&
           v->start<v->size&&(v->loop==KR_NOLOOP||v->loop<v->size)&&
           v->step>=1&&v->step<=0x200000&&v->left<=255&&v->right<=255;
}
static int envelope(KrPlayer *p,uint32_t a,uint16_t *time,int release,int fallback) {
    if(!range(&p->rom,a,52)) return fallback;
    unsigned max=u8(&p->rom,a+48),sus=u8(&p->rom,a+49),loop=u8(&p->rom,a+50),flags=u8(&p->rom,a+51);
    if(!(flags&1)) return fallback;
    if(max>=12||sus>max||loop>max) return fallback;
    unsigned t=*time;
    if((flags&2)&&!release&&t>=(u16(&p->rom,a+4*sus)&511)) t=u16(&p->rom,a+4*sus)&511;
    unsigned last=u16(&p->rom,a+4*max)&511;
    if((flags&4)&&t>last) t=u16(&p->rom,a+4*loop)&511;
    int value=u16(&p->rom,a)>>9;
    for(unsigned n=0;n<=max;++n) {
        unsigned coord=u16(&p->rom,a+4*n),at=coord&511;
        if(t<at) break;
        value=coord>>9;
        if(n<max) {
            unsigned next=u16(&p->rom,a+4*(n+1))&511;
            if(next>at&&t<next) value+=((int16_t)u16(&p->rom,a+4*n+2)*(int)(t-at))>>8;
        }
    }
    *time=(uint16_t)(t<65534?t+1:t);return clip(value,0,64);
}
static void levels(KrPlayer *p,unsigned n) {
    KrTrack *t=&p->tracks[n];
    unsigned env=64;
    if(t->instrument_addr) {
        uint16_t time=t->env_tick;env=envelope(p,t->instrument_addr+192,&time,t->released,64);
    }
    unsigned volume=(t->volume*t->channel_volume*p->global_volume)>>13;
    volume=(volume*env)>>6;volume=(volume*t->fade)>>15;
    if(p->mods.mute&(1u<<n)) volume=0;
    int pan=t->pan;
    if(t->instrument_addr) {
        uint16_t time=t->pan_tick;pan+=(envelope(p,t->instrument_addr+244,&time,t->released,32)-32)*2;
    }
    pan=clip(pan,-64,64);
    t->voice.left=clip((volume*(64-pan))>>5,0,255);
    t->voice.right=clip((volume*(64+pan))>>5,0,255);
    t->voice.step=step_for(t->pitch+p->mods.pitch);
}
static void unsupported(KrPlayer *p,unsigned fx) {
    if(fx<64) p->unsupported[fx>>5]|=1u<<(fx&31);
    event(p,KR_EVENT_UNSUPPORTED,fx);
}
static int trigger(KrPlayer *p,unsigned n,unsigned note) {
    KrTrack *t=&p->tracks[n];
    t->note=note+11;t->gate=1;t->activity=64;++t->notes;

    int instrument=(int)t->instrument-1+p->mods.voice_shift;
    unsigned count=p->instrument_based?p->song.instrument_count:p->song.sample_count;
    if(instrument<0||instrument>=(int)count) return 0;
    uint32_t bank=p->instrument_based?p->song.instruments:p->song.samples;
    uint32_t entry=bank+4u*(unsigned)instrument+(int32_t)p->mods.table_offset;
    if(!range(&p->rom,entry,4)) return 0;
    unsigned sample=instrument;t->instrument_addr=0;
    if(p->instrument_based) {
        uint32_t a=u32(&p->rom,entry);
        if(note<1||note>96||!range(&p->rom,a,302)) return 0;
        sample=u16(&p->rom,a+2*(note-1));t->instrument_addr=a;
        if(sample>=p->song.sample_count) return 0;
        entry=p->song.samples+4*sample;
    }
    uint32_t a=u32(&p->rom,entry);
    if(!range(&p->rom,a,18)) return 0;
    uint32_t end=u32(&p->rom,a+4),data=a+18;
    if(end<=data||end-data>KR_MAX_SAMPLE||!range(&p->rom,data,end-data+1)) return 0;
    uint32_t size=end-data,loop=u32(&p->rom,a);
    t->sample_addr=a;t->voice.size=size;t->voice.data=data;
    t->voice.loop=u8(&p->rom,a+16)&&loop&&loop<=size?size-loop:KR_NOLOOP;
    t->volume=clip(u8(&p->rom,a+14),0,64);
    if(p->instrument_based)t->pan=(int8_t)u8(&p->rom,a+15);
    t->pitch=((int)note+11+(int8_t)u8(&p->rom,a+13))*256+2*(int8_t)u8(&p->rom,a+12);
    t->env_tick=t->pan_tick=0;t->fade=32768;t->released=0;
    unsigned chop=(size*p->mods.sample_end)>>8;
    t->voice.size-=chop;
    if(t->voice.loop>=t->voice.size)t->voice.loop=KR_NOLOOP;
    if(!t->voice.size) return 0;
    t->voice.data+=(uint32_t)p->mods.scrub;
    if(!range(&p->rom,t->voice.data,t->voice.size+1)) return 0;
    t->voice.start=t->offset+((t->voice.size*p->mods.sample_start)>>8);
    if(t->voice.start>=t->voice.size)t->voice.start=t->voice.size-1;
    t->voice.reverse=random8(p)<p->mods.reverse_chance;
    levels(p,n);
    if(!valid_voice(p,&t->voice))return 0;
    t->active=1;
    return 1;
}
static void release(KrPlayer *p,unsigned n) {
    KrTrack *t=&p->tracks[n];t->gate=0;t->released=1;
    if(!t->instrument_addr||!(u8(&p->rom,t->instrument_addr+243)&1)) off(p,n);
}
static void row_effect(KrPlayer *p,unsigned n,unsigned fx,unsigned v) {
    KrTrack *t=&p->tracks[n];
    if(fx==6||fx==7||fx==22||fx==26||fx==27||fx==28||fx==33) { if(v)t->memory[fx]=v;else v=t->memory[fx]; }
    t->effect=fx;t->param=v;
    switch(fx) {
    case 6: if((v>>4)==15||(v&15)==15)unsupported(p,fx);break;
    case 0:case 7:case 22:case 26:case 28:case 33:case 44:break;
    case 1:if(v)p->speed=(uint16_t)clip(v,1,31);break;
    case 2:if(v>=32)p->bpm=v;break;
    case 3:if(v<32){if(v)p->speed=v;}else p->bpm=v;break;
    case 4:p->pending_order=v;break;
    case 5:p->pending_row=v;break; // Krawerter stores a binary row, not BCD.
    case 8:t->volume=clip(t->volume-v,0,64);break;
    case 9:t->volume=clip(t->volume+v,0,64);break;
    case 18:t->volume=clip(v,0,64);break;
    case 25:t->channel_volume=clip(v,0,64);break;
    case 27:t->offset=v<<8;break;
    case 32:p->global_volume=clip(v,0,128);break;
    case 34:t->pan=clip((int)v-64,-64,64);break;
    case 36:event(p,KR_EVENT_MARK,v);break;
    default:unsupported(p,fx);break;
    }
}
static int read_row(KrPlayer *p) {
    for(unsigned c=0;c<p->channels;++c) {p->tracks[c].effect=0;p->tracks[c].offset=0;}
    for(unsigned budget=0;budget<256;++budget) {
        uint8_t f,note=0,ins=0,high=0,vol=0,fx=0,v=0;
        if(!get(p,&f)) return 0;
        if(!f) {event(p,KR_EVENT_ROW,((uint32_t)p->order<<16)|p->row);return 1;}
        unsigned c=f&31;
        if(c>=p->channels)return fail(p);
        if(f&32) {
            if(!get(p,&note)||!get(p,&ins))return 0;
            if(!p->format&&(note&128)&&!get(p,&high))return 0;
            if(p->format){high=(note&1);note>>=1;}else note&=127;
        }
        if((f&64)&&!get(p,&vol))return 0;
        if((f&128)&&(!get(p,&fx)||!get(p,&v)))return 0;
        KrTrack *t=&p->tracks[c];
        unsigned instrument=ins|((unsigned)high<<8);
        if(instrument)t->instrument=instrument;
        // Offset must precede the sample start; other effects/volume override defaults after it.
        if(fx==27)row_effect(p,c,fx,v);
        int started=0;
        if(note==127)release(p,c);
        else if(note) {
            // Keep the previous voice on probabilistic drop; invalid selections stop safely.
            if(p->mods.drop_chance&&random8(p)<p->mods.drop_chance) {
                t->note=note+11;t->gate=1;t->activity=64;++t->notes;
            } else { started=trigger(p,c,note);if(!started)off(p,c); }
        }
        if(vol) {
            if(vol>=0x10&&vol<=0x60)t->volume=clip(vol-0x10,0,64);
            else unsupported(p,64u+vol); // volume column commands explicitly reported
        }
        row_effect(p,c,fx,v);
        if(fx==44&&!v){off(p,c);started=0;}
        if(started) {
            levels(p,c);
            if(p->hooks.note)p->hooks.note(p->hooks.user,c,&t->voice);
            if(!valid_voice(p,&t->voice))off(p,c);
            else if(p->hooks.start)p->hooks.start(p->hooks.user,c,&t->voice);
        }
    }
    return fail(p);
}
void kr_update(KrPlayer *p) {
    for(unsigned n=0;n<p->channels;++n) {
        KrTrack *t=&p->tracks[n];if(!t->active)continue;
        if(p->hooks.done&&p->hooks.done(p->hooks.user,n)){off(p,n);continue;}
        levels(p,n);
        if(t->effect==22) {
            unsigned phase=p->tick%3, add=phase==1?t->param>>4:phase==2?t->param&15:0;
            t->voice.step=step_for(t->pitch+p->mods.pitch+(int)add*256);
        }
        if(p->hooks.update)p->hooks.update(p->hooks.user,n,&t->voice);
    }
}
static void tick_effect(KrPlayer *p,unsigned n) {
    KrTrack *t=&p->tracks[n];unsigned v=t->param,hi=v>>4,lo=v&15;
    int delta=(int)hi-(int)lo;
    switch(t->effect) {
    case 6:if(hi==15||lo==15)break; // fine S3M variants are reported, not approximated
        /* fall through */
    case 7:t->volume=clip(t->volume+delta,0,64);break;
    case 26:t->channel_volume=clip(t->channel_volume+delta,0,64);break;
    case 28:t->pan=clip(t->pan+delta,-64,64);break;
    case 33:p->global_volume=clip(p->global_volume+delta,0,128);break;
    case 44:if(p->tick==v)off(p,n);break;
    default:break;
    }
}
void kr_tick(KrPlayer *p) {
    if(!p->running)return;
    if(!p->tick&&!read_row(p))return;
    if((p->ticks%24)==0)++p->beats;
    for(unsigned n=0;n<p->channels;++n) {
        KrTrack *t=&p->tracks[n];
        if(p->tick)tick_effect(p,n);
        if(t->active&&t->instrument_addr) {
            envelope(p,t->instrument_addr+192,&t->env_tick,t->released,64);
            envelope(p,t->instrument_addr+244,&t->pan_tick,t->released,32);
            if(t->released) {
                unsigned fade=u16(&p->rom,t->instrument_addr+296);
                if(fade>=t->fade)off(p,n);else t->fade-=fade;
            }
        }
        if(t->activity)--t->activity;
    }
    kr_update(p);++p->ticks;
    if(++p->tick<p->speed)return;
    p->tick=0;
    unsigned order=p->order,row=p->row+1;
    if(p->pending_order!=UINT16_MAX||p->pending_row!=UINT16_MAX) {
        order=p->pending_order!=UINT16_MAX?p->pending_order:p->order+1;
        row=p->pending_row!=UINT16_MAX?p->pending_row:0;
    } else if(row>=p->rows){++order;row=0;}
    p->pending_order=p->pending_row=UINT16_MAX;
    while(order<p->orders&&u8(&p->rom,p->song.module+3+order)==254)++order;
    if(order>=p->orders||(order<p->orders&&u8(&p->rom,p->song.module+3+order)==255)) {
        if(!p->repeat){kr_stop(p);return;}order=p->restart;row=0;
    }
    if(order<p->order||(order==p->order&&row<=p->row)) {++p->loops;event(p,KR_EVENT_LOOP,p->loops);}
    if(order==p->order&&row==(unsigned)p->row+1&&row<p->rows)p->row=row;
    else if(!position(p,order,row))fail(p);
}
