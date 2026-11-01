// Synthetic native Krawall data; no cartridge dumps required.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../mangle_adapter.h"
#include "../discovery.h"
#include "../pcm_mixer.h"
#define BASE 0x08000000u
#define SIZE 65536u
static uint8_t rom[SIZE];
_Alignas(4) uint8_t g_hostIo[0x400];
static KrPlayer p;
static unsigned bad,unsupported;
static void w16(unsigned a,unsigned v){rom[a]=v;rom[a+1]=v>>8;}
static void w32(unsigned a,uint32_t v){w16(a,v);w16(a+2,v>>16);}
static void event(void *ctx,unsigned e,uint32_t v){(void)ctx;(void)v;bad+=e==KR_EVENT_BAD_DATA;unsupported+=e==KR_EVENT_UNSUPPORTED;}
static void fixture(int old) {
    memset(rom,0,sizeof(rom));kr_pcm_init();bad=unsupported=0;
    w32(0x800,BASE+0x3000);w32(0x804,BASE+0x4000);
    w32(0x3000,256);w32(0x3004,BASE+0x3012+256);w32(0x3008,8363);
    rom[0x300E]=64;rom[0x3010]=1;
    for(unsigned i=0;i<=256;++i)rom[0x3012+i]=(uint8_t)((i%32)*7+16);
    rom[0x1000]=1;rom[0x1001]=1;rom[0x1000+355]=128;
    rom[0x1000+356]=6;rom[0x1000+357]=125;w32(0x1000+364,BASE+0x2000);
    w16(0x2020,8);
    unsigned data=old?0x2021:0x2022;
    rom[data]=0x60;rom[data+1]=old?98:49;rom[data+2]=1;rom[data+3]=0x50;
    KrHooks h;kr_mixer_hooks(&h,&p);h.event=event;
    KrRom r={rom,BASE,SIZE};kr_init(&p,r,&h);
    KrSong s={BASE+0x1000,BASE+0x800,BASE+0x804,1,1,(uint8_t)old};
    assert(kr_start(&p,&s));
}
static void render(void){int8_t l[16],r[16];kr_pcm_render(l,r);}
static void playback(void) {
    for(int old=0;old<=1;++old){
        fixture(old);uint32_t modules[4];assert(kr_scan(&p.rom,BASE,BASE+SIZE,old,modules,4)>=1);
        assert(modules[0]==BASE+0x1000);assert(kr_tick_hz_q16(&p)==50*65536);
        kr_tick(&p);assert(p.tracks[0].notes==1&&p.tracks[0].gate&&g_kr_mix[0].active);
        assert(g_kr_mix[0].step>2080&&g_kr_mix[0].step<2100);
        int energy=0;for(int k=0;k<5;++k){int8_t l[16],r[16];kr_pcm_render(l,r);for(int i=0;i<16;i++)energy+=l[i]*l[i]+r[i]*r[i];}
        assert(energy>0);uint32_t gen=g_kr_mix[0].gen;int32_t pos=g_kr_mix[0].pos;
        p.mods.mute=1;kr_update(&p);assert(!g_kr_mix[0].volL&&!g_kr_mix[0].volR);
        render();assert(g_kr_mix[0].pos!=pos&&g_kr_mix[0].gen==gen);
        p.mods.mute=0;p.mods.pitch=12*256;kr_update(&p);assert(g_kr_mix[0].step>4170&&g_kr_mix[0].volL);
        assert(kr_seek(&p,0,0));kr_tick(&p);assert(p.tracks[0].notes==2);
        p.repeat=0;for(int i=0;i<50;i++)kr_tick(&p);assert(!p.running&&!g_kr_mix[0].active);
        assert(kr_restart(&p));assert(p.mods.pitch==12*256);
    }
    puts("PASS modern/2003 data, scan, PCM, clock, mute, pitch, seek, stop/restart");
}
static void effects(void) {
    fixture(0);uint8_t row[]={0xE0,49,1,0x40,1,3,0,0x80,22,0x47,0,0x80,18,0,0};
    memcpy(rom+0x2022,row,sizeof(row));kr_tick(&p);assert(p.speed==3&&p.tracks[0].volume==48);
    kr_tick(&p);kr_tick(&p);kr_tick(&p);uint32_t s=g_kr_mix[0].step;
    kr_tick(&p);assert(g_kr_mix[0].step>s);kr_tick(&p);kr_tick(&p);assert(p.tracks[0].volume==0);
    fixture(0);uint8_t uns[]={0x80,19,7,0};memcpy(rom+0x2022,uns,4);kr_tick(&p);
    assert(unsupported==1&&(p.unsupported[0]&(1u<<19)));
    fixture(0);uint8_t jump[]={0x80,4,0,0};memcpy(rom+0x2022,jump,4);
    for(int i=0;i<7;++i)kr_tick(&p);
    assert(p.loops==1);
    puts("PASS speed, arpeggio, volume zero, jump, unsupported-effect reporting");
}
static void malicious_note(void *ctx,unsigned ch,KrVoice *v){(void)ctx;(void)ch;v->data=UINT32_MAX-3;}
static void safety(void) {
    fixture(0);p.hooks.note=malicious_note;kr_tick(&p);assert(!g_kr_mix[0].active);
    fixture(0);w32(0x3004,BASE+SIZE+10);kr_tick(&p);assert(!g_kr_mix[0].active);
    fixture(0);memset(rom+0x2022,0x80,2000);kr_tick(&p);assert(p.error&&!p.running&&bad);
    fixture(0);p.stream=BASE+SIZE-1;rom[SIZE-1]=0xE0;kr_tick(&p);assert(p.error);
    fixture(0);rom[0x2022]=0x3F;kr_tick(&p);assert(p.error);
    fixture(0);KrSong s=p.song;s.sample_count=UINT16_MAX;assert(!kr_start(&p,&s));
    fixture(0);p.mods.scrub=INT32_MIN;kr_tick(&p);assert(!g_kr_mix[0].active);
    puts("PASS bad ranges, truncation, channel/bank validation, budget, callback validation");
}
static void instruments(void) {
    fixture(0);rom[0x1000+358]=1;assert(kr_restart(&p));
    unsigned e=0x4000+192;w16(e,64<<9);w16(e+2,(unsigned)(-16*256));
    w16(e+4,4);rom[e+48]=1;rom[e+51]=1;w16(0x4000+296,8192);
    kr_tick(&p);assert(p.tracks[0].instrument_addr==BASE+0x4000);unsigned v=g_kr_mix[0].volL;
    kr_tick(&p);assert(g_kr_mix[0].volL<v);
    rom[0x2027]=0x20;rom[0x2028]=127;rom[0x2029]=0;
    for(int i=0;i<12;++i)kr_tick(&p);
    assert(!p.tracks[0].active);
    puts("PASS instrument map, packed volume envelope, release/fadeout");
}
static void unsigned_and_bridge(void) {
    fixture(0);memset(rom+0x3012,128,257);kr_tick(&p);
    for(int block=0;block<4;++block){int8_t l[16],r[16];kr_pcm_render(l,r);for(int i=0;i<16;++i)assert(l[i]==0&&r[i]==0);}
    PlayerMods mods={0};Track followers[PL_TRACKS];mods.tempoU=256;mods.scrubLen=4096;
    assert(!kr_mangle_sync(&p,&mods,followers));assert(followers[0].notes==1&&followers[0].lastKey==60);
    mods.attack=1;mods.gate=1;mods.chaos=1;mods.vibrato=1;
    assert(kr_mangle_sync(&p,&mods,followers)==(KR_ADAPTER_UNSUPPORTED_ENVELOPE|KR_ADAPTER_UNSUPPORTED_GATE|KR_ADAPTER_UNSUPPORTED_CHAOS|KR_ADAPTER_UNSUPPORTED_LFO));
    fixture(0);rom[0x1000+358]=1;
    for(unsigned i=0;i<257;++i)w32(0x5000+4*i,BASE+0x4000);
    KrSong song=p.song;song.instruments=BASE+0x5000;song.instrument_count=257;
    assert(kr_start(&p,&song));
    const uint8_t extended[]={0x20,49|128,1,1,0};memcpy(rom+0x2022,extended,sizeof(extended));
    kr_tick(&p);assert(p.tracks[0].instrument==257&&p.tracks[0].active);
    puts("PASS unsigned midpoint silence, bridge/followers/unsupported controls and extended instrument IDs");
}
static void discovery(void){
    fixture(0);
    for(unsigned i=0;i<4;++i){w32(0x800+4*i,BASE+0x3000);w32(0x810+4*i,BASE+0x4000);}
    KrDiscovery d={0};for(unsigned i=0;i<SIZE;i+=0x4000)kr_discover_slice(&d,&p.rom,BASE+i,BASE+i+0x4000);
    assert(d.module_count>=1&&d.sample_count>=1&&d.instrument_count>=1);
    assert(d.samples[0].address==BASE+0x800&&d.samples[0].count==4);
    assert(d.instruments[0].address==BASE+0x810&&d.instruments[0].count==4);
    puts("PASS incremental trial discovery of module/sample/instrument candidates");
}
static uint32_t seed=12345;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void fuzz(void){
    for(unsigned run=0;run<2000;++run){
        fixture(run&1);
        for(unsigned i=0x2021;i<0x2800;++i)rom[i]=rnd();
        for(unsigned i=0;i<sizeof(p.mods);++i)((uint8_t*)&p.mods)[i]=rnd();
        for(unsigned i=0;i<64;++i){kr_tick(&p);kr_update(&p);render();}
        assert(!p.running||p.ticks==64);
    }
    puts("PASS 2,000 randomized pattern/control cases");
}
int main(void){playback();effects();safety();instruments();unsigned_and_bridge();discovery();fuzz();return 0;}
