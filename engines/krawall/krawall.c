// krawall.c - a Krawall module player for GBA Music Mangle: Krawall's own player logic, ported.
//
// The row decoding, effects (all of player_effects.h, volume column included), envelopes, fadeout,
// auto-vibrato, period tables and song flow below are Sebastian Kienzl's lib/player.c and
// lib/player_effects.h, kept in their order and with their quirks (the games were tracked against
// them). Changed for this project, 2026-11-06:
// - every read of the cart goes through bounds-checked accessors (the data is not trusted), with
//   budgets on loops and clamps on table indices, instead of struct pointers;
// - the mixer calls (kramPlayExt, kramSetFreq/Vol/Pan/Pos, kramStop) set a voice that the caller's
//   hooks render (payload/mixer.c through engines/common/eng_mixer.c);
// - BIOS divides become shift-and-subtract (the payload links no division helpers);
// - Music Mangle's live controls: mute, transpose, voice shift, table offset, sample start/end,
//   scrub, reverse and drop chances, and seeking for the transport's sections;
// - the 2003 pattern format (note byte = note << 1 | instrument bit 8) and sub-songs (254 markers).
//
// Krawall, XM/S3M Modplayer Library
// Copyright (C) 2001-2005, 2013 Sebastian Kienzl
//
// This library is free software; you can redistribute it and/or modify it under the terms of the GNU
// Lesser General Public License as published by the Free Software Foundation; either version 2.1 of
// the License, or (at your option) any later version. This library is distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License in COPYING.LGPL.
#include "krawall.h"
#include "kr_tables.h"

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
// n / d by shift and subtract (Krawall: FastIntDivide, the BIOS's).
static uint32_t divide(uint32_t n,uint32_t d) {
    uint32_t q=0,rem=0;if(!d)return 0;
    for(int k=31;k>=0;--k){rem=(rem<<1)|((n>>k)&1u);if(rem>=d){rem-=d;q|=1u<<k;}}
    return q;
}
static void event(KrPlayer *p,unsigned e,uint32_t v) {
    if(p->hooks.event) p->hooks.event(p->hooks.user,e,v);
}
static void unsupported(KrPlayer *p,unsigned fx) {
    if(fx<64) p->unsupported[fx>>5]|=1u<<(fx&31);
    event(p,KR_EVENT_UNSUPPORTED,fx);
}
static void voice_off(KrPlayer *p,unsigned c) {
    KrTrack *t=&p->tracks[c];t->active=0;t->gate=0;
    if(p->hooks.stop) p->hooks.stop(p->hooks.user,c);
}
void kr_stop(KrPlayer *p) {
    for(unsigned c=0;c<KR_CHANNELS;++c) {voice_off(p,c);p->tracks[c].i_active=0;}
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

// ---- the module (structure checks, song lists) -----------------------------------------------------
// Module: channels, numOrders, songRestart, order[256] @3, channelPan[32] @259, songIndex[64] @291,
// volGlobal @355, initSpeed, initBPM, instrument-based, linear slides, S3M volume slides, volume
// optimisation, Amiga limits, padding, patterns[] @364. Pattern: index[16], rows (u8 in 2003), data.
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
    if(!ch||ch>32||!orders||!u8(r,m+356)||
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
// One pass of the order list in tracker ticks (rows x the starting speed; speed commands inside the
// patterns are not followed), for telling a short effect from a song. 0 if the module is invalid.
uint32_t kr_module_ticks(const KrRom *r,uint32_t m,uint8_t format) {
    if(!kr_module_valid(r,m,format)) return 0;
    unsigned orders=u8(r,m+1),speed=u8(r,m+356);uint32_t rows=0;
    for(unsigned i=0;i<orders;++i) {
        unsigned n=u8(r,m+3+i);
        if(n==255) break;
        if(n==254) continue;
        rows+=pattern_rows(r,pattern_addr(r,m,i),format);
    }
    return rows*speed;
}
unsigned kr_module_songs(const KrRom *r,uint32_t m,uint8_t format,uint8_t *starts,uint32_t *ticks,unsigned cap) {
    if(!cap||!kr_module_valid(r,m,format)) return 0;
    unsigned orders=u8(r,m+1),speed=u8(r,m+356),n=0,i=0;
    while(i<orders&&n<cap) {
        while(i<orders&&u8(r,m+3+i)==254) ++i;
        if(i>=orders||u8(r,m+3+i)==255) break;
        uint32_t rows=0;starts[n]=(uint8_t)i;
        for(;i<orders;++i) {
            unsigned x=u8(r,m+3+i);
            if(x>=254) break;
            rows+=pattern_rows(r,pattern_addr(r,m,i),format);
        }
        if(ticks) ticks[n]=rows*speed;
        ++n;
        if(i<orders&&u8(r,m+3+i)==255) break;
    }
    return n;
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

// ---- samples, instruments, periods ---------------------------------------------------------------------
// Sample: loopLength, end (pointer), c2Freq, fineTune s8 @12, relativeNote s8 @13, volDefault @14,
// panDefault s8 @15, loop @16, hq @17, data (unsigned 8-bit) @18. Instrument: samples[96] (u16),
// envVol @192 and envPan @244 (12 nodes {coord = y << 9 | x, slope}, then max, sus, loopStart,
// flags), volFade @296, vibType/Sweep/Depth/Rate @298.
#define SMP_FINE 12
#define SMP_REL 13
#define SMP_VOL 14
#define SMP_PAN 15
#define INS_ENV_VOL 192
#define INS_ENV_PAN 244
#define INS_FADE 296
#define INS_VIB_DEPTH 300
#define INS_VIB_RATE 301
static int sample_ok(const KrPlayer *p,uint32_t a) {
    if(!a||!range(&p->rom,a,18)) return 0;
    uint32_t end=u32(&p->rom,a+4),data=a+18;
    return end>data&&end-data<=KR_MAX_SAMPLE&&range(&p->rom,data,end-data+1);
}
static uint32_t sample_at(const KrPlayer *p,unsigned s,int32_t shift) {
    if(s>=p->song.sample_count) s&=255;           // a junk high byte in sample maps (seen on real carts)
    if(s>=p->song.sample_count) return 0;
    uint32_t entry=p->song.samples+4u*s+(uint32_t)shift;
    if(!range(&p->rom,entry,4)) return 0;
    uint32_t a=u32(&p->rom,entry);return sample_ok(p,a)?a:0;
}
// The pattern's instrument (0-based), with VOICE SHIFT and TABLE OFFSET.
static uint32_t instrument_at(const KrPlayer *p,unsigned i) {
    int k=(int)i+p->mods.voice_shift;
    if(k<0||k>=(int)p->song.instrument_count) return 0;
    uint32_t entry=p->song.instruments+4u*(unsigned)k+(uint32_t)(int32_t)p->mods.table_offset;
    if(!range(&p->rom,entry,4)) return 0;
    uint32_t a=u32(&p->rom,entry);return range(&p->rom,a,302)?a:0;
}
static uint32_t instrument_sample(const KrPlayer *p,uint32_t ins,int note) {
    return ins?sample_at(p,u16(&p->rom,ins+2u*(unsigned)clip(note,0,95)),0):0;
}
static int sfine(const KrPlayer *p,uint32_t s) { return s?(int8_t)u8(&p->rom,s+SMP_FINE):0; }
static int32_t calc_period_linear(int n,int fine) { return (n<<6)+2*768+(fine>>1); }
static int32_t calc_period(const KrPlayer *p,int n,uint32_t s) {
    n=clip(n,0,KR_TABLE_PERIODS-1);int fine=sfine(p,s);
    if(p->linear) return calc_period_linear(n,fine);
    return (int32_t)(((uint32_t)kr_table_periods[n]*kr_table_finetune[32+(fine>>2)])>>15);
}
static uint32_t freq_linear(int32_t per) {
    per=clip(per,0,768*24-1);
    unsigned okt=((unsigned)(per>>6)*0xaaabu)>>19,rem=(unsigned)per-okt*768;   // per / 768
    if(rem>767){++okt;rem-=768;}
    return kr_table_linear[rem]>>(23-okt);
}
static uint32_t freq_amiga(int32_t per) { return per>0?divide(8363u*1712u,(uint32_t)per):0; }
static uint32_t final_freq(const KrPlayer *p,int32_t per) { return p->linear?freq_linear(per):freq_amiga(per); }
static const int16_t *wave(unsigned w) { return w==1?kr_table_ramp:w==2?kr_table_square:w==3?kr_table_rand:kr_table_sine; }

// ---- the voice (what Krawall told its mixer) -----------------------------------------------------------
// TRANSPOSE (Q8 semitones) scales the frequency last; then Hz / 4 is the Q12 step at 16384 Hz.
static uint32_t transposed(uint32_t hz,int pitch) {
    static const uint32_t ratio[13]={65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715,131072};
    if(!pitch) return hz;
    int x=clip(pitch,-60*256,60*256)+120*256;unsigned key=(unsigned)x>>8,oct=key/12,semi=key%12;
    uint32_t r=ratio[semi]+(((ratio[semi+1]-ratio[semi])*((unsigned)x&255))>>8);
    uint64_t v=((uint64_t)hz*r)>>16;
    if(oct>=10) v<<=oct-10; else v>>=10-oct;
    return v>0xFFFFFFu?0xFFFFFFu:(uint32_t)v;
}
// Its mixer: lvol = vol x master x (64 - pan) >> 13, master = music volume (128) x the module's
// global volume x the song's (Gxx) >> 13. Krawall's output stage (32-channel setting, master 128)
// then gives exactly payload/mixer.c's sample x level >> 8 with level = lvol (volShift 0).
static void levels(KrPlayer *p,unsigned n) {
    KrTrack *t=&p->tracks[n];
    unsigned master=(128u*p->global_volume*p->volume)>>13;if(master>255)master=255;
    unsigned vol=(p->mods.mute&(1u<<n))?0:t->mixvol;int pan=clip(t->mixpan,-64,64);
    t->voice.left=(uint16_t)clip((int)((vol*master*(unsigned)(64-pan))>>13),0,255);
    t->voice.right=(uint16_t)clip((int)((vol*master*(unsigned)(64+pan))>>13),0,255);
    uint32_t step=transposed(t->freq,p->mods.pitch)>>2;
    t->voice.step=step<1?1:step>0x200000?0x200000:step;
}
static int valid_voice(KrPlayer *p,const KrVoice *v) {
    return v->size&&v->size<=KR_MAX_SAMPLE&&range(&p->rom,v->data,v->size+1)&&
           v->start<v->size&&(v->loop==KR_NOLOOP||v->loop<v->size)&&
           v->step>=1&&v->step<=0x200000&&v->left<=255&&v->right<=255;
}
static void set_freq(KrTrack *c,uint32_t hz) { c->freq=hz; }
static int set_vol(KrTrack *c,int v) { c->mixvol=(uint8_t)clip(v,0,64);return c->active; }   // 0: the sample has ended
static int set_pan(KrTrack *c,int x) { c->mixpan=(int8_t)clip(x,-64,64);return c->active; }
// kramSetPos: the voice restarts at a byte position (past the end: a loop's start, or it stops).
static void set_pos(KrPlayer *p,unsigned n,uint32_t pos) {
    KrTrack *t=&p->tracks[n];
    if(!t->active) return;
    if(pos>=t->voice.size){if(t->voice.loop==KR_NOLOOP){voice_off(p,n);return;}pos=t->voice.loop;}
    t->voice.start=pos;levels(p,n);
    if(p->hooks.start) p->hooks.start(p->hooks.user,n,&t->voice);
}

// ---- the channel calls of player.c ----------------------------------------------------------------------
#define CH(c) ((unsigned)((c)-p->tracks))
static void setChannelFreq(KrPlayer *p,KrTrack *c) { c->periodc=c->period;set_freq(c,final_freq(p,c->period)); }
static void setChannelFreqH(KrPlayer *p,KrTrack *c,int32_t per) { c->periodc=(int16_t)per;set_freq(c,final_freq(p,per)); }
static void setChannelFreqNote(KrPlayer *p,KrTrack *c,int n) {
    c->periodc=(int16_t)calc_period_linear(clip(n,0,KR_TABLE_PERIODS-1),sfine(p,c->sample_addr));set_freq(c,freq_linear(c->periodc));
}
static int setChannelVol(KrTrack *c) { c->volumec=c->volume;return set_vol(c,(c->volumec*c->cvolume*c->i_vol)>>12); }
static int setChannelVolH(KrTrack *c) { return set_vol(c,(c->volumec*c->cvolume*c->i_vol)>>12); }
static int setChannelPan(KrTrack *c) { c->panningc=c->panning;return set_pan(c,clip(c->panningc+c->i_pan,-64,64)); }
static int setChannelPanH(KrTrack *c) { return set_pan(c,clip(c->panningc+c->i_pan,-64,64)); }
static void stopChannel(KrPlayer *p,KrTrack *c) { c->i_active=0;voice_off(p,CH(c)); }
// The envelope table at a node; past the twelfth reads as 0 (Krawall would read the next fields).
static uint16_t env_coord(const KrPlayer *p,uint32_t env,unsigned node) { return node<12?u16(&p->rom,env+4*node):0; }
static int16_t env_inc(const KrPlayer *p,uint32_t env,unsigned node) { return node<12?(int16_t)u16(&p->rom,env+4*node+2):0; }
#define ENVFLAGS_ENABLED 1
#define ENVFLAGS_SUSTAIN 2
#define ENVFLAGS_LOOP 4
#define ENVSTATUS_DISABLED 0
#define ENVSTATUS_STOPPED 1
#define ENVSTATUS_SUSTAIN 2
#define ENVSTATUS_ACTIVE 3
#define ENVSTATUS_ACTIVE_NOSUSTAIN 4
static int env_enabled(const KrPlayer *p,uint32_t env) {
    return (u8(&p->rom,env+51)&ENVFLAGS_ENABLED)&&u8(&p->rom,env+48)<12;  // max past the nodes: off (bounded)
}
static int i_init(KrPlayer *p,KrTrack *c) {
    uint32_t ins=c->instrument_addr;int ret=0;
    c->i_active=0;c->i_vibrpos=0;
    if(!ins) {c->i_vol=64;c->i_vol_status=ENVSTATUS_DISABLED;c->i_pan=0;c->i_pan_status=ENVSTATUS_DISABLED;return 0;}
    if(env_enabled(p,ins+INS_ENV_VOL)) {
        c->i_vol_status=ENVSTATUS_ACTIVE;c->i_vol_node=c->i_vol_tick=255;c->i_vol_ticktarget=0;
        c->i_vol_fade=32767;c->i_vol_fadeactive=0;c->i_active=1;ret=1;
    } else {c->i_vol=64;c->i_vol_status=ENVSTATUS_DISABLED;}
    if(env_enabled(p,ins+INS_ENV_PAN)) {
        c->i_pan_status=ENVSTATUS_ACTIVE;c->i_pan_node=c->i_pan_tick=255;c->i_pan_ticktarget=0;c->i_active=1;
    } else {c->i_pan=0;c->i_pan_status=ENVSTATUS_DISABLED;}
    if(u8(&p->rom,ins+INS_VIB_RATE)) c->i_active=1;
    return ret;
}
// kramPlayExt: the channel's sample at its period, volume x channel volume, its panning; then the
// mangles (sample start/end, scrub, reverse) and the hooks' own change (CHAOS), revalidated.
static void playChannelNote(KrPlayer *p,KrTrack *c) {
    unsigned n=CH(c);
    if(p->instrument_based) i_init(p,c);
    c->resetnote=0;
    uint32_t s=c->sample_addr;
    if(!sample_ok(p,s)) {stopChannel(p,c);return;}
    uint32_t data=s+18,size=u32(&p->rom,s+4)-data,loop=u32(&p->rom,s);
    KrVoice *v=&c->voice;
    v->data=data+(uint32_t)p->mods.scrub;v->size=size-((size*p->mods.sample_end)>>8);
    v->loop=u8(&p->rom,s+16)&&loop&&loop<=size?size-loop:KR_NOLOOP;
    if(v->loop!=KR_NOLOOP&&v->loop>=v->size) v->loop=KR_NOLOOP;
    uint32_t start=(uint32_t)c->pos<<8;start+=(v->size*p->mods.sample_start)>>8;
    if(v->size&&start>=v->size) start=v->loop!=KR_NOLOOP?v->loop:v->size-1;
    v->start=start;v->reverse=random8(p)<p->mods.reverse_chance;
    set_freq(c,final_freq(p,c->period));c->mixvol=(uint8_t)clip((c->volume*c->cvolume)>>6,0,64);c->mixpan=(int8_t)clip(c->panning,-64,64);
    c->active=1;levels(p,n);
    if(p->hooks.note) p->hooks.note(p->hooks.user,n,v);
    if(!valid_voice(p,v)) {c->active=0;stopChannel(p,c);return;}
    if(p->hooks.start) p->hooks.start(p->hooks.user,n,v);
}
static void i_release(KrPlayer *p,KrTrack *c) {
    c->gate=0;
    if(c->i_vol_status) {
        c->i_vol_fadeactive=1;
        c->i_vol_status=c->i_vol_status==ENVSTATUS_SUSTAIN?ENVSTATUS_ACTIVE:ENVSTATUS_ACTIVE_NOSUSTAIN;
        // this is ok here, cause w/o a volume-envelope === will stop the sample
        if(c->i_pan_status) c->i_pan_status=c->i_pan_status==ENVSTATUS_SUSTAIN?ENVSTATUS_ACTIVE:ENVSTATUS_ACTIVE_NOSUSTAIN;
    } else stopChannel(p,c);
}

// ---- player_effects.h ------------------------------------------------------------------------------------
static void volUp(KrTrack *c,int v) { c->volume=(int8_t)clip(c->volume+v,-128,64);setChannelVol(c); }
static void volDown(KrTrack *c,int v) { c->volume=(int8_t)clip(c->volume-v,0,127);setChannelVol(c); }
static void volSet(KrTrack *c,int v) { c->volume=(int8_t)clip(v,-128,64);setChannelVol(c); }
static void panRight(KrTrack *c,int x) { c->panning=(int8_t)clip(c->panning+x,-128,64);setChannelPan(c); }
static void panLeft(KrTrack *c,int x) { c->panning=(int8_t)clip(c->panning-x,-64,127);setChannelPan(c); }
static unsigned quick_modulo(unsigned a,unsigned b) { if(!b)return 0;while(a>=b)a-=b;return a; } // BIOS: 0 by 0
static void eff_speed(KrPlayer *p,KrTrack *c) { if(c->effectop) p->speed=c->effectop; }
static void eff_bpm(KrPlayer *p,KrTrack *c) { if(c->effectop>=0x20) p->bpm=c->effectop; }
static void eff_speedbpm(KrPlayer *p,KrTrack *c) { if(c->effectop>=0x20) p->bpm=c->effectop; else p->speed=c->effectop; }
static void eff_volslide_s3m(KrPlayer *p,KrTrack *c,int onrow) {
    if(onrow) {
        if(c->effectop) c->volslide=c->effectop;
        unsigned h=c->volslide>>4,l=c->volslide&15;c->volslidefine=0;
        if(h&&l==15){c->volslidefine=1;volUp(c,h);}
        else if(l&&h==15){c->volslidefine=1;volDown(c,l);}
        if(c->effect==23&&c->resetnote&&c->leffect==20) c->resetnote=0;  // after vibrato: no reset
    }
    if(!onrow||p->fast_volslides) {
        if(c->volslidefine) return;
        unsigned h=c->volslide>>4,l=c->volslide&15;
        if(l) volDown(c,l); else if(h) volUp(c,h);
    }
}
static void eff_volslide_xm(KrTrack *c,int onrow) {
    if(onrow) {
        if(c->effectop) c->volslide=c->effectop;
        if(c->effect==49&&c->resetnote&&c->leffect==20) c->resetnote=0;
    } else {
        unsigned h=c->volslide>>4,l=c->volslide&15;
        if(l) volDown(c,l); else if(h) volUp(c,h);
    }
}
static void eff_volslide_df(KrTrack *c) { if(c->effectop) c->volslide_fine=c->effectop;volDown(c,c->volslide_fine); }
static void eff_volslide_uf(KrTrack *c) { if(c->effectop) c->volslide_fine=c->effectop;volUp(c,c->volslide_fine); }
static void eff_gvolume(KrPlayer *p,KrTrack *c) { p->volume=c->effectop>0x40?0x40:c->effectop; }
static void eff_gvolslide(KrPlayer *p,KrTrack *c,int onrow) {
    if(onrow) {if(c->effectop) p->volslide=c->effectop;}
    else {
        unsigned h=p->volslide>>4,l=p->volslide&15;
        if(l) p->volume=(uint8_t)clip((int)p->volume-(int)l,0,64);
        else if(h) p->volume=(uint8_t)clip(p->volume+h,0,0x40);
    }
}
static void eff_cvolume(KrTrack *c) { c->cvolume=(int8_t)(c->effectop>64?64:c->effectop);setChannelVolH(c); }
static void eff_cvolslide(KrTrack *c,int onrow) {
    if(onrow) {if(c->effectop) c->cvolslide=c->effectop;}
    else {
        unsigned h=c->cvolslide>>4,l=c->cvolslide&15;
        if(l){c->cvolume=(int8_t)clip(c->cvolume-(int)l,0,127);setChannelVolH(c);}
        else if(h){c->cvolume=(int8_t)clip(c->cvolume+(int)h,-128,0x40);setChannelVolH(c);}
    }
}
static void eff_panslide(KrTrack *c,int onrow) {
    if(onrow) {
        if(c->effectop) c->panslide=c->effectop;
        unsigned h=c->panslide>>4,l=c->panslide&15;c->panslidefine=0;
        if(h&&l==15){c->panslidefine=1;panLeft(c,(int)h<<1);}
        else if(l&&h==15){c->volslidefine=1;panRight(c,(int)l<<1);}   // (sic: Krawall sets volslidefine here)
    } else {
        if(c->panslidefine) return;
        unsigned h=c->panslide>>4,l=c->panslide&15;
        if(l) panRight(c,(int)l<<1); else if(h) panLeft(c,(int)h<<1);
    }
}
static void eff_portaup_s3m(KrPlayer *p,KrTrack *c,int onrow) {
    if(onrow) {
        if(c->effectop) c->porta=c->effectop;
        if(c->porta>=0xe0) {
            if((c->porta&0xf0)==0xf0) c->period-=(int16_t)((c->porta&15)<<2);
            if((c->porta&0xf0)==0xe0) c->period-=(int16_t)(c->porta&15);
            if(c->period<=0) stopChannel(p,c);
            setChannelFreq(p,c);
        }
    } else if(c->porta&&c->porta<0xe0) {
        c->period-=(int16_t)(c->porta<<2);
        if(c->period<=0) stopChannel(p,c); else setChannelFreq(p,c);
    }
}
// XM portas: the converter swaps up and down for Amiga slides, so up always adds here.
static void eff_portaup_xm(KrPlayer *p,KrTrack *c,int onrow) {
    if(onrow) {if(c->effectop) c->porta=c->effectop;}
    else {c->period+=(int16_t)(c->porta<<2);setChannelFreq(p,c);}
}
static void eff_portaup_f(KrPlayer *p,KrTrack *c) { if(c->effectop) c->porta_fine=c->effectop;c->period+=(int16_t)(c->porta_fine<<2);setChannelFreq(p,c); }
static void eff_portaup_ef(KrPlayer *p,KrTrack *c) { if(c->effectop) c->porta_efine=c->effectop;c->period+=c->porta_efine;setChannelFreq(p,c); }
static void eff_portadown_s3m(KrPlayer *p,KrTrack *c,int onrow) {
    if(onrow) {
        if(c->effectop) c->porta=c->effectop;
        if(c->porta>=0xe0) {
            if((c->porta&0xf0)==0xf0) c->period+=(int16_t)((c->porta&15)<<2);
            if((c->porta&0xf0)==0xe0) c->period+=(int16_t)(c->porta&15);
            setChannelFreq(p,c);
        }
    } else if(c->porta&&c->porta<0xe0) {c->period+=(int16_t)(c->porta<<2);setChannelFreq(p,c);}
}
static void eff_portadown_xm(KrPlayer *p,KrTrack *c,int onrow) {
    if(onrow) {if(c->effectop) c->porta=c->effectop;}
    else {c->period-=(int16_t)(c->porta<<2);if(c->period<=0) stopChannel(p,c); else setChannelFreq(p,c);}
}
static void eff_portadown_f(KrPlayer *p,KrTrack *c) {
    if(c->effectop) c->porta_fine=c->effectop;
    c->period-=(int16_t)(c->porta_fine<<2);if(c->period<=0) stopChannel(p,c); else setChannelFreq(p,c);
}
static void eff_portadown_ef(KrPlayer *p,KrTrack *c) {
    if(c->effectop) c->porta_efine=c->effectop;
    c->period-=c->porta_efine;if(c->period<=0) stopChannel(p,c); else setChannelFreq(p,c);
}
static int period_of_note(int n) { return kr_table_periods[clip(n,0,KR_TABLE_PERIODS-1)]; }
static void eff_portanote(KrPlayer *p,KrTrack *c,int onrow) {
    if(onrow) {if(c->effectop) c->portainc=c->effectop;return;}
    if(c->period>c->portadest) {
        c->period=(int16_t)clip(c->period-(c->portainc<<2),c->portadest,32767);
        if(c->glissando) {
            for(int k=0;k<KR_TABLE_PERIODS&&c->period<period_of_note(c->glissnote)&&c->glissnote<KR_TABLE_PERIODS-1;++k) c->glissnote++;
            if(c->period>period_of_note(c->glissnote)&&c->glissnote>0) c->glissnote--;
        }
    } else if(c->period<c->portadest) {
        c->period=(int16_t)clip(c->period+(c->portainc<<2),-32768,c->portadest);
        if(c->glissando) {
            for(int k=0;k<KR_TABLE_PERIODS&&c->period>period_of_note(c->glissnote)&&c->glissnote>0;++k) c->glissnote--;
            if(c->period<period_of_note(c->glissnote)&&c->glissnote<KR_TABLE_PERIODS-1) c->glissnote++;
        }
    } else return;
    if(!c->glissando) setChannelFreq(p,c);
    else setChannelFreqH(p,c,period_of_note(c->glissnote));   // the current halftone
}
static void eff_vibrato(KrPlayer *p,KrTrack *c,int onrow) {
    if(onrow) {
        if(c->playnote&&c->vibrretrig) c->vibrpos=p->vibrofs;
        if(c->effectop&15) c->vibrdepth=(uint8_t)((c->effectop&15)<<2);
        if(c->effectop&0xf0) c->vibrspeed=c->effectop>>4;
        if(c->resetnote&&c->leffect==23) c->resetnote=0;
    } else {
        int vperiod=c->period+((c->vibrdepth*wave(c->vibrwave)[c->vibrpos&(KR_TABLE_VIBRATO-1)])>>7);
        setChannelFreqH(p,c,(int16_t)vperiod);
        c->vibrpos=(uint8_t)((c->vibrpos+c->vibrspeed)&(KR_TABLE_VIBRATO-1));
        c->resetnote=1;
    }
}
static void eff_fvibrato(KrPlayer *p,KrTrack *c,int onrow) {
    if(onrow){eff_vibrato(p,c,1);if(c->effectop&15) c->vibrdepth>>=2;}
    else eff_vibrato(p,c,0);
}
static void eff_tremolo(KrTrack *c,int onrow) {
    if(onrow) {
        if(c->playnote&&c->tremretrig) c->trempos=0;
        if(c->effectop&15) c->tremdepth=c->effectop&15;
        if(c->effectop&0xf0) c->tremspeed=c->effectop>>4;
    } else {
        int8_t delta=(int8_t)((c->tremdepth*wave(c->tremwave)[c->trempos&(KR_TABLE_VIBRATO-1)])>>6);
        c->volumec=(int8_t)clip(c->volume+delta,0,64);setChannelVolH(c);
        c->trempos=(uint8_t)((c->trempos+c->tremspeed)&(KR_TABLE_VIBRATO-1));
        c->resetnote=1;
    }
}
static void eff_panbrello(KrTrack *c,int onrow) {
    if(onrow) {
        if(c->playnote&&c->panbretrig) c->panbpos=0;
        if(c->effectop&15) c->panbdepth=c->effectop&15;
        if(c->effectop&0xf0) c->panbspeed=c->effectop>>4;
    } else {
        int8_t delta=(int8_t)((c->panbdepth*wave(c->panbwave)[(c->panbpos>>2)&(KR_TABLE_VIBRATO-1)])>>6);
        c->panningc=(int8_t)(c->panning+delta);
        c->panbpos=(uint8_t)(c->panbpos+c->panbspeed);
        setChannelPanH(c);c->resetnote=1;
    }
}
static void eff_tremor(KrTrack *c,int onrow) {
    if(onrow&&c->effectop) {c->trmon=(uint8_t)((c->effectop>>4)+1);c->trmtot=(uint8_t)(c->trmon+(c->effectop&15)+1);}
    if(c->trmcnt==c->trmon) {c->volumec=0;setChannelVolH(c);c->resetnote=1;}
    else if(c->trmcnt==c->trmtot) {c->trmcnt=0;setChannelVol(c);c->resetnote=0;}
    c->trmcnt++;
}
static void eff_offset(KrPlayer *p,KrTrack *c) {
    if(c->effectop) {if(!c->playnote) set_pos(p,CH(c),(uint32_t)c->effectop<<8); else c->pos=c->effectop;}
}
static void eff_retrig(KrPlayer *p,KrTrack *c,int onrow) {
    if(onrow) {
        if(c->effectop) c->retrig=c->effectop;
        if(!c->playnote&&c->retrig) set_pos(p,CH(c),0);
    } else if(c->retrig&&!quick_modulo(p->tick,c->retrig&15)) {
        set_pos(p,CH(c),0);
        unsigned volchg=c->retrig>>4;
        if(volchg>=1&&volchg<=5) volDown(c,1<<(volchg-1));
        else if(volchg>=9&&volchg<=0xd) volUp(c,1<<(volchg-9));
        else switch(volchg) {
            case 7: volSet(c,c->volume>>1);break;
            case 0xf: volSet(c,c->volume<<1);break;
            case 0xe: volSet(c,(c->volume*3)>>1);break;
            case 6: volSet(c,(342*c->volume)>>9);break;   // x 2/3
            default: break;
        }
    }
}
static void eff_arpeggio(KrPlayer *p,KrTrack *c,int onrow) {
    if(onrow) {
        if(c->effectop) c->arpeggio=c->effectop;
        if(!c->playnote) setChannelFreq(p,c);   // the base tone
        c->arpeggiocnt=0;
    } else {
        if(!c->sample_addr) return;
        if(++c->arpeggiocnt>=3) c->arpeggiocnt=0;
        int note=c->arpeggiocnt==0?c->knote:c->arpeggiocnt==1?c->knote+(c->arpeggio>>4):c->knote+(c->arpeggio&15);
        setChannelFreqNote(p,c,note<KR_TABLE_PERIODS-1?note:KR_TABLE_PERIODS-1);
        c->resetnote=1;
    }
}
static void eff_pan(KrTrack *c) { c->panning=(int8_t)(c->effectop>0x80?0:(int)c->effectop-0x40);setChannelPan(c); }
static void eff_patternloop(KrPlayer *p,KrTrack *c) {
    if(c->effectop) {
        if(!p->loop_active){p->loop_count=c->effectop;p->loop_active=1;}
        if(!p->loop_count--) p->loop_active=0;
        else p->goto_row=p->loop_row;
    } else p->loop_row=(uint8_t)p->row;
}
static void eff_notecut(KrPlayer *p,KrTrack *c,int onrow) {
    if(onrow) {if(!c->effectop) c->playnote=0;}   // SC0: never play the note
    else if(c->effectop==p->tick) volSet(c,0);
}
static void eff_notedelay(KrPlayer *p,KrTrack *c,int onrow) {
    if(onrow) {if(c->effectop) c->playnote=0;}
    else if(c->effectop==p->tick) playChannelNote(p,c);
}
static void set_wave(KrTrack *c,uint8_t *w,uint8_t *retrig) { *w=c->effectop&3;*retrig=c->effectop<4; }
// The volume column ($60-$ff; Krawall's effectsVC). Its tone porta reads the effect's operand and,
// between rows, only sets the speed again (as Krawall's does).
static void volume_column(KrPlayer *p,KrTrack *c,int onrow) {
    unsigned x=c->effectVC&15;
    switch(c->effectVC>>4) {
    case 0: if(!onrow) volDown(c,x);break;
    case 1: if(!onrow) volUp(c,x);break;
    case 2: volDown(c,x);break;
    case 3: volUp(c,x);break;
    case 4: if(c->effectVC) c->vibrspeed=(uint8_t)x;break;
    case 5: if(onrow){if(c->effectVC) c->vibrdepth=(uint8_t)(x<<2);} else eff_vibrato(p,c,0);break;
    case 6: c->panning=(int8_t)(c->effectVC==7?0:(int)((4370*128*x)>>16)-64);setChannelPan(c);break;
    case 7: if(!onrow) panLeft(c,(int)x);break;
    case 8: if(!onrow) panRight(c,(int)x);break;
    case 9: if(onrow){if(c->effectop) c->portainc=(int16_t)(x<<4);} else eff_portanote(p,c,1);break;
    default: break;
    }
}
static int vc_between(unsigned vc) { unsigned g=vc>>4;return g==0||g==1||g==5||g==7||g==8||g==9; }
// Krawall's effect table: on the row func1(onrow), between rows func1 when inbet1, then func2.
static int eff_between(unsigned e) {
    switch(e){case 6:case 7:case 10:case 11:case 14:case 15:case 19:case 20:case 21:case 22:case 23:case 24:
              case 26:case 28:case 29:case 30:case 31:case 33:case 35:case 44:case 45:case 49:case 50:return 1;default:return 0;}
}
static void effect1(KrPlayer *p,KrTrack *c,unsigned e,int onrow) {
    switch(e) {
    case 1: eff_speed(p,c);break;
    case 2: eff_bpm(p,c);break;
    case 3: eff_speedbpm(p,c);break;
    case 4: p->goto_order=c->effectop;break;
    case 5: p->goto_row=(int16_t)(10*(c->effectop>>4)+(c->effectop&15));break;   // BCD, as XM/S3M store it
    case 6: case 23: case 24: eff_volslide_s3m(p,c,onrow);break;
    case 7: case 49: case 50: eff_volslide_xm(c,onrow);break;
    case 8: eff_volslide_df(c);break;
    case 9: eff_volslide_uf(c);break;
    case 10: eff_portadown_xm(p,c,onrow);break;
    case 11: eff_portadown_s3m(p,c,onrow);break;
    case 12: eff_portadown_f(p,c);break;
    case 13: eff_portadown_ef(p,c);break;
    case 14: eff_portaup_xm(p,c,onrow);break;
    case 15: eff_portaup_s3m(p,c,onrow);break;
    case 16: eff_portaup_f(p,c);break;
    case 17: eff_portaup_ef(p,c);break;
    case 18: c->volume=(int8_t)c->effectop;setChannelVol(c);break;
    case 19: eff_portanote(p,c,onrow);break;
    case 20: eff_vibrato(p,c,onrow);break;
    case 21: eff_tremor(c,onrow);break;
    case 22: eff_arpeggio(p,c,onrow);break;
    case 25: eff_cvolume(c);break;
    case 26: eff_cvolslide(c,onrow);break;
    case 27: eff_offset(p,c);break;
    case 28: eff_panslide(c,onrow);break;
    case 29: eff_retrig(p,c,onrow);break;
    case 30: eff_tremolo(c,onrow);break;
    case 31: eff_fvibrato(p,c,onrow);break;
    case 32: eff_gvolume(p,c);break;
    case 33: eff_gvolslide(p,c,onrow);break;
    case 34: eff_pan(c);break;
    case 35: eff_panbrello(c,onrow);break;
    case 36: event(p,KR_EVENT_MARK,c->effectop);break;
    case 37: c->glissando=c->effectop!=0;break;
    case 38: set_wave(c,&c->vibrwave,&c->vibrretrig);break;
    case 39: set_wave(c,&c->tremwave,&c->tremretrig);break;
    case 40: set_wave(c,&c->panbwave,&c->panbretrig);break;
    case 43: eff_patternloop(p,c);break;
    case 44: eff_notecut(p,c,onrow);break;
    case 45: eff_notedelay(p,c,onrow);break;
    default: if(onrow) unsupported(p,e);break;    // 41 42 46 47 48: empty in Krawall's table too
    }
}
static void effect2(KrPlayer *p,KrTrack *c,unsigned e) {
    if(e==23||e==49) eff_vibrato(p,c,0);
    else if(e==24||e==50) eff_portanote(p,c,0);
}

// ---- song flow -----------------------------------------------------------------------------------------
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
static unsigned order_at(const KrPlayer *p,unsigned i) { return i<p->orders?u8(&p->rom,p->song.module+3+i):255; }
// pDataSet: the pattern of the current order, from the current row (rows decoded from its start: the
// row index is not trusted).
static int set_pattern(KrPlayer *p) {
    uint32_t a=pattern_addr(&p->rom,p->song.module,p->order);
    unsigned rows=pattern_rows(&p->rom,a,p->format);
    if(!a||!rows||rows>256) return fail(p);
    p->pattern=a;p->stream=a+(p->format?33:34);p->rows=(uint16_t)rows;
    if(p->row>=rows) p->row=0;
    for(unsigned i=0;i<p->row;++i) if(!skip_row(p)) return 0;
    return 1;
}
// advanceRow. A loop (for the transport) is the order wrapping or a jump going back; it is counted
// when the row it lands on plays (process_row), not a row early when the jump is decided.
static void advance_row(KrPlayer *p) {
    unsigned old_order=p->order;int jumped=0,wrapped=0;
    if(p->goto_order>=0) {p->order=(uint16_t)(p->goto_order-1);p->row=8192;p->goto_order=-1;jumped=1;}
    if(++p->row>=p->rows||p->goto_row>=0) {
        int marker=0;
        for(unsigned k=0;k<256;++k) {             // over every +++ in the order list
            p->order=(uint16_t)(p->order+1);
            if(p->order>=p->orders||order_at(p,p->order)!=254) break;
            marker=1;
        }
        if(p->order>=p->orders||order_at(p,p->order)==255||(marker&&p->song_mode)) {
            p->order=p->restart;wrapped=1;
            if(!p->repeat){kr_stop(p);return;}
        }
        p->row=0;
        if(!set_pattern(p)) return;
    }
    if(p->goto_row>=0) {
        p->row=(uint16_t)(p->goto_row<p->rows?p->goto_row:0);p->goto_row=-1;
        if(!set_pattern(p)) return;
    }
    if(wrapped||p->order<old_order||(jumped&&p->order==old_order)) p->loop_due=1;
}
static void process_row(KrPlayer *p) {
    if(p->loop_due){p->loop_due=0;++p->loops;event(p,KR_EVENT_LOOP,p->loops);}
    for(unsigned budget=0;;++budget) {
        if(budget>=64) {fail(p);return;}
        uint8_t follow,note=0,ins=0,high=0,vol=0,eff=0,effop=0;
        if(!get(p,&follow)) return;
        if(!follow) break;
        unsigned n=follow&31;
        if(n>=p->channels) {fail(p);return;}
        KrTrack *c=&p->tracks[n];
        int setVol=0,sampleSet=0;
        c->touched=1;
        if(follow&32) {
            if(!get(p,&note)||!get(p,&ins)) return;
            if(p->format){high=note&1;note>>=1;}
            else if(note&128){if(!get(p,&high)) return;note&=127;}
        }
        if((follow&64)&&!get(p,&vol)) return;
        if((follow&128)&&(!get(p,&eff)||!get(p,&effop))) return;
        c->leffect=c->effect;c->effect=eff;
        unsigned instrument=ins|((unsigned)high<<8);
        if(instrument) {                                   // set volume & instrument/sample
            c->instrument=(uint16_t)instrument;instrument--;
            if(p->instrument_based) {
                if(note&&note!=127) {
                    c->instrument_addr=instrument_at(p,instrument);
                    c->sample_addr=instrument_sample(p,c->instrument_addr,note-1);
                    if(c->sample_addr){c->volume=c->volumec=(int8_t)u8(&p->rom,c->sample_addr+SMP_VOL);
                        c->panning=c->panningc=(int8_t)u8(&p->rom,c->sample_addr+SMP_PAN);}
                    setVol=sampleSet=1;
                }
            } else {
                int k=(int)instrument+p->mods.voice_shift;
                c->sample_addr=k>=0?sample_at(p,(unsigned)k,p->mods.table_offset):0;
                if(c->sample_addr) c->volume=c->volumec=(int8_t)u8(&p->rom,c->sample_addr+SMP_VOL);
                setVol=1;
            }
        }
        if(vol) {
            if(vol<=0x60){c->volume=(int8_t)clip((int)vol-0x10,-128,0x40);c->volumec=c->volume;c->effectVC=0;setVol=1;}
            else {c->effectVC=(uint8_t)(vol-0x60);volume_column(p,c,1);}
        }
        c->playnote=0;
        if(note&&c->sample_addr) {
            if(note==127) i_release(p,c);                 // note-off
            else {
                int kn=note-1+(int8_t)u8(&p->rom,c->sample_addr+SMP_REL);
                c->note=(uint8_t)(note+11);c->gate=1;c->activity=64;++c->notes;
                if(eff!=19&&eff!=24) {
                    if(p->instrument_based&&!sampleSet) {
                        uint32_t s=instrument_sample(p,c->instrument_addr,kn);
                        c->sample_addr=s;
                    }
                    if(c->sample_addr) {
                        c->knote=c->glissnote=(int16_t)kn;
                        c->period=c->periodc=c->portadest=(int16_t)calc_period(p,kn,c->sample_addr);
                        c->pos=0;c->trmcnt=0;c->playnote=1;
                        if(p->mods.drop_chance&&random8(p)<p->mods.drop_chance) c->playnote=0;   // DROP
                    }
                } else c->portadest=(int16_t)calc_period(p,kn,c->sample_addr);
            }
        }
        c->effectop=effop;
        if(eff) effect1(p,c,eff,1);
        if(!p->running) return;
        if(c->playnote) playChannelNote(p,c);
        else if(setVol) setChannelVol(c);
    }
    // The channels again (some were not touched: no pattern data)
    for(unsigned i=0;i<p->channels;++i) {
        KrTrack *c=&p->tracks[i];
        if(!c->touched){c->effectop=0;c->leffect=c->effect;c->effect=0;c->effectVC=0;}
        else c->touched=0;
        if(c->resetnote&&c->effect!=c->leffect){setChannelFreq(p,c);setChannelVol(c);setChannelPan(c);c->resetnote=0;}
    }
    event(p,KR_EVENT_ROW,((uint32_t)p->order<<16)|p->row);
    advance_row(p);
}
static void process_between(KrPlayer *p) {
    for(unsigned i=0;i<p->channels;++i) {
        KrTrack *c=&p->tracks[i];
        if(c->effectVC&&vc_between(c->effectVC)) volume_column(p,c,0);
        if(c->effect) {
            if(eff_between(c->effect)) effect1(p,c,c->effect,0);
            effect2(p,c,c->effect);
        }
    }
}
// processInstruments: volume envelope and fadeout, panning envelope, auto-vibrato, once a tick.
static void process_instruments(KrPlayer *p) {
    for(unsigned i=0;i<p->channels;++i) {
        KrTrack *c=&p->tracks[i];uint32_t ins=c->instrument_addr;
        if(!c->i_active||!ins) continue;
        if(c->i_vol_status) {
            int setVol=0;
            if(c->i_vol_fadeactive) {
                setVol=1;c->i_vol_fade=(int16_t)(c->i_vol_fade-(int)u16(&p->rom,ins+INS_FADE));
                if(c->i_vol_fade<=0){stopChannel(p,c);continue;}
            }
            if(c->i_vol_status>=ENVSTATUS_ACTIVE) {
                uint32_t env=ins+INS_ENV_VOL;unsigned max=u8(&p->rom,env+48),sus=u8(&p->rom,env+49),flags=u8(&p->rom,env+51);
                setVol=1;
                if(c->i_vol_node<12) c->i_vol_pos=(uint16_t)(c->i_vol_pos+env_inc(p,env,c->i_vol_node));
                if(++c->i_vol_tick==c->i_vol_ticktarget) {
                    if(c->i_vol_node==255||c->i_vol_node<11) c->i_vol_node++;
                    if((flags&ENVFLAGS_SUSTAIN)&&c->i_vol_node==sus&&c->i_vol_status!=ENVSTATUS_ACTIVE_NOSUSTAIN) c->i_vol_status=ENVSTATUS_SUSTAIN;
                    if(c->i_vol_node==max) {
                        if(flags&ENVFLAGS_LOOP) {
                            c->i_vol_node=(uint8_t)clip(u8(&p->rom,env+50),0,11);c->i_vol_tick=(uint8_t)(env_coord(p,env,c->i_vol_node)&0x1ff);
                        } else {
                            c->i_vol_status=ENVSTATUS_STOPPED;
                            if((env_coord(p,env,c->i_vol_node)>>9)==0){stopChannel(p,c);continue;}
                        }
                    }
                    c->i_vol_pos=(uint16_t)((env_coord(p,env,c->i_vol_node)>>9)<<8);
                    c->i_vol_ticktarget=(uint8_t)(1+(env_coord(p,env,c->i_vol_node+1u)&0x1ff));
                }
            }
            if(setVol) {
                c->i_vol=(int8_t)(((uint32_t)c->i_vol_pos*(uint32_t)(c->i_vol_fade>0?c->i_vol_fade:0))>>23);
                if(!setChannelVolH(c)){stopChannel(p,c);continue;}   // the sample is already done
            }
        }
        if(c->i_pan_status>=ENVSTATUS_ACTIVE) {
            uint32_t env=ins+INS_ENV_PAN;unsigned max=u8(&p->rom,env+48),sus=u8(&p->rom,env+49),flags=u8(&p->rom,env+51);
            if(c->i_pan_node<12) c->i_pan_pos=(uint16_t)(c->i_pan_pos+env_inc(p,env,c->i_pan_node));
            if(++c->i_pan_tick==c->i_pan_ticktarget) {
                if(c->i_pan_node==255||c->i_pan_node<11) c->i_pan_node++;
                if((flags&ENVFLAGS_SUSTAIN)&&c->i_pan_node==sus&&c->i_pan_status!=ENVSTATUS_ACTIVE_NOSUSTAIN) c->i_pan_status=ENVSTATUS_SUSTAIN;
                if(c->i_pan_node==max) {
                    if(flags&ENVFLAGS_LOOP) {
                        c->i_pan_node=(uint8_t)clip(u8(&p->rom,env+50),0,11);c->i_pan_tick=(uint8_t)(1+(env_coord(p,env,c->i_pan_node)&0x1ff));
                    } else c->i_pan_status=ENVSTATUS_STOPPED;
                }
                c->i_pan_pos=(uint16_t)((env_coord(p,env,c->i_pan_node)>>9)<<8);
                c->i_pan_ticktarget=(uint8_t)(env_coord(p,env,c->i_pan_node+1u)&0x1ff);
            }
            c->i_pan=(int8_t)(((c->i_pan_pos>>8)-32)<<1);
            if(!setChannelPanH(c)){stopChannel(p,c);continue;}
        }
        unsigned rate=u8(&p->rom,ins+INS_VIB_RATE);
        if(rate) {                                        // sweep and type are not implemented (Krawall)
            int vperiod=c->periodc+((u8(&p->rom,ins+INS_VIB_DEPTH)*wave(c->vibrwave)[(c->i_vibrpos>>2)&(KR_TABLE_VIBRATO-1)])>>8);
            set_freq(c,final_freq(p,(int16_t)vperiod));
            c->i_vibrpos=(uint8_t)(c->i_vibrpos+rate);
        }
    }
}
void kr_update(KrPlayer *p) {
    for(unsigned n=0;n<p->channels;++n) {
        KrTrack *t=&p->tracks[n];if(!t->active)continue;
        if(p->hooks.done&&p->hooks.done(p->hooks.user,n)){t->active=0;t->gate=0;continue;}
        levels(p,n);
        if(p->hooks.update)p->hooks.update(p->hooks.user,n,&t->voice);
    }
}
// timerRoutine: one tracker tick.
void kr_tick(KrPlayer *p) {
    if(!p->running)return;
    if(++p->tick>=p->speed) {p->tick=0;process_row(p);if(!p->running)return;}
    else process_between(p);
    if(p->instrument_based) process_instruments(p);
    if((p->ticks%24)==0)++p->beats;
    for(unsigned n=0;n<p->channels;++n) if(p->tracks[n].activity)--p->tracks[n].activity;
    ++p->ticks;
    kr_update(p);
}

// ---- start, seek ---------------------------------------------------------------------------------------
int kr_seek(KrPlayer *p,unsigned order,unsigned row) {
    if(!p->running||order>=p->orders) return 0;
    for(unsigned i=0;i<p->orders&&order_at(p,order)==254;++i) if(++order>=p->orders) return 0;
    if(order_at(p,order)>=254) return 0;
    for(unsigned i=0;i<KR_CHANNELS;++i){voice_off(p,i);p->tracks[i].i_active=0;}
    p->order=(uint16_t)order;p->row=(uint16_t)row;p->goto_order=p->goto_row=-1;p->loop_due=0;
    if(!set_pattern(p)) return 0;
    p->tick=p->speed;                                       // the next tick plays this row
    return 1;
}
// krapPlay.
int kr_start(KrPlayer *p,const KrSong *s) {
    if(!kr_module_valid(&p->rom,s->module,s->format)||!s->sample_count||
       !range(&p->rom,s->samples,4u*s->sample_count)||
       (u8(&p->rom,s->module+358)&&(!s->instrument_count||!range(&p->rom,s->instruments,4u*s->instrument_count)))) return 0;
    kr_stop(p);zero(p->tracks,sizeof(p->tracks));zero(p->unsupported,sizeof(p->unsupported));
    const KrRom *r=&p->rom;uint32_t m=s->module;
    p->song=*s;p->format=s->format;p->channels=u8(r,m);p->orders=u8(r,m+1);
    p->song_mode=s->song_mode&&s->start_order<p->orders;
    p->restart=p->song_mode?s->start_order:u8(r,m+2);if(p->restart>=p->orders)p->restart=0;
    p->song.start_order=p->song_mode?s->start_order:0;p->song.song_mode=p->song_mode;
    p->speed=p->tick=u8(r,m+356);p->bpm=u8(r,m+357);
    p->global_volume=u8(r,m+355);p->volume=64;p->volslide=0;
    p->instrument_based=u8(r,m+358);p->linear=u8(r,m+359);p->fast_volslides=u8(r,m+360);
    p->vibrofs=p->instrument_based?KR_TABLE_VIBRATO>>1:0;
    p->goto_order=p->goto_row=-1;p->loop_row=p->loop_count=p->loop_active=0;
    p->error=0;p->ticks=0;p->loop_due=0;
    for(unsigned c=0;c<p->channels;++c) {
        KrTrack *t=&p->tracks[c];
        t->vibrpos=p->vibrofs;t->vibrretrig=1;t->tremretrig=1;t->panbretrig=0;
        t->panning=(int8_t)u8(r,m+259+c);t->i_vol=64;t->i_pan=0;t->cvolume=64;
    }
    p->order=p->song.start_order;p->row=0;p->running=1;
    if(!set_pattern(p)) return 0;
    return 1;
}
int kr_restart(KrPlayer *p) { KrSong s=p->song;return kr_start(p,&s); }
uint32_t kr_tick_hz_q16(const KrPlayer *p) { return ((uint32_t)p->bpm*p->mods.tempo*512u)/5; }
