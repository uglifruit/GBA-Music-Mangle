// Host acceptance tests use only authored MusyX-format data, never commercial assets.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mixer_adapter.h"
#include "../demo_data.h"
#define BASE 0x08000000u
static uint8_t data[4096];
_Alignas(4) uint8_t g_hostIo[0x400];
static MsPlayer p;
static unsigned bad,unknown;
static void w16(unsigned a,unsigned v){data[a]=v;data[a+1]=v>>8;}
static void w32(unsigned a,uint32_t v){w16(a,v);w16(a+2,v>>16);}
static void event(void *u,unsigned kind,uint32_t v){(void)u;(void)v;bad+=kind==MS_EVENT_BAD_DATA;unknown+=kind==MS_EVENT_UNSUPPORTED;}
static void fixture(void){
    memcpy(data,ms_demo_rom,sizeof(data));bad=unknown=0;ms_pcm_init();MsRom r={data,BASE,sizeof(data)};
    MsHooks hooks;ms_mixer_hooks(&hooks,&p);hooks.event=event;ms_init(&p,r,&hooks);
    MsSong song;assert(ms_song_info(&p.rom,BASE+0x100,0,&song));assert(ms_start(&p,&song));p.preview=0;
}
static void render(void){int8_t l[16],r[16];ms_pcm_render(l,r);}
static unsigned active(void){unsigned n=0;for(unsigned i=0;i<12;++i)n+=p.voices[i].active;return n;}
static void playback(void){
    fixture();uint32_t roots[4];assert(ms_scan(&p.rom,BASE,BASE+4096,roots,4)==1&&roots[0]==BASE+0x100);
    assert(ms_tick_hz_q16(&p)==192*65536);ms_tick(&p);assert(p.tracks[0].notes==2&&active()==2&&p.tracks[0].gate);
    assert(g_ms_mix[0].step==4096&&g_ms_mix[1].step>5100);
    int energy=0;for(int block=0;block<4;++block){int8_t l[16],r[16];ms_pcm_render(l,r);for(int i=0;i<16;++i)energy+=l[i]*l[i]+r[i]*r[i];}assert(energy>0);
    uint32_t gen=g_ms_mix[0].gen;int32_t position=g_ms_mix[0].pos;
    p.mods.mute=1;ms_update(&p);assert(!g_ms_mix[0].volL&&!g_ms_mix[1].volR);render();assert(g_ms_mix[0].pos!=position&&g_ms_mix[0].gen==gen);
    p.mods.mute=0;p.mods.pitch=3072;ms_update(&p);assert(g_ms_mix[0].step==8192&&g_ms_mix[0].volL);
    for(int i=0;i<48;++i)ms_tick(&p);
    assert(!active()&&!p.tracks[0].gate);
    for(int i=0;i<48;++i)ms_tick(&p);
    assert(p.bpm==150&&p.tracks[0].notes==4);
    for(int i=0;i<96;++i)ms_tick(&p);
    assert(p.loops==1&&p.tracks[0].notes==6&&!unknown&&!bad);
    puts("PASS relative root/song pointers, chords, 96-PPQN tempo, note durations, control track, loops, PCM/mute/pitch");
}
static void preview(void){
    fixture();memset(data+0x164,0,0x1C);data[0x164]=12;
    data[0x16A]=15;w16(0x16C,1);data[0x172]=4;w16(0x174,65535);
    ms_tick(&p);assert(!active()&&unknown==2&&(p.unsupported[0]&(1u<<12)));
    fixture();p.preview=1;memset(data+0x164,0,0x1C);data[0x164]=12;
    data[0x16A]=15;w16(0x16C,1);data[0x172]=4;w16(0x174,65535);
    ms_tick(&p);assert(active()==2&&unknown==2);
    fixture();p.preview=1;data[0x16C]=0;ms_tick(&p);assert(active()==2&&(p.warnings&MS_WARN_PREVIEW_TAIL));
    fixture();data[0x164]=13;data[0x165]=0;w32(0x166,0);ms_tick(&p);assert(!active()&&p.rejected==2&&(p.unsupported[1]&(1u<<31)));
    puts("PASS strict rejection, explicit preview skipping/tails, bounded self-jumping macros");
}
static void macros(void){
    fixture();memset(data+0x164,0,0x1C);
    // SETNOTE absolute 72, STARTSAMPLE, SCALEVOLUME x0.5, WAIT forever.
    data[0x164]=9;data[0x165]=1;w16(0x166,72*256);
    data[0x168]=15;w16(0x16A,1);
    data[0x170]=47;data[0x171]=1;w16(0x174,128);
    data[0x176]=4;w16(0x178,65535);
    ms_tick(&p);assert(active()==2&&g_ms_mix[0].step==8192&&p.voices[0].volume==50);
    fixture();memset(data+0x164,0,0x1C);
    data[0x164]=16;w32(0x166,0x179-0x164); // keyoff trap
    data[0x16A]=15;w16(0x16C,1);data[0x172]=4;w16(0x174,65535);
    data[0x179]=0;
    ms_tick(&p);assert(active()==2);
    for(int i=0;i<48;++i)ms_tick(&p);
    assert(!active());
    fixture();data[0x16C]=47;data[0x16D]=0;data[0x16E]=0;data[0x16F]=0;w16(0x170,256);data[0x172]=4;w16(0x174,65535);
    ms_tick(&p);assert(active()==2);
    puts("PASS absolute pitch, volume scale and note-off trap/END");
}
static void corrupt(void *ctx,unsigned i,MsPcm *v){(void)ctx;(void)i;v->data=UINT32_MAX;}
static void safety(void){
    fixture();p.hooks.note=corrupt;ms_tick(&p);assert(!active()&&bad==2);
    fixture();w32(0x328,UINT32_MAX);ms_tick(&p);assert(!active()&&p.rejected==2);
    fixture();p.mods.scrub=INT32_MIN;ms_tick(&p);assert(!active());
    fixture();data[0x164]=255;ms_tick(&p);assert(!active()&&unknown==2&&(p.unsupported[1]&(1u<<30)));
    fixture();w32(0xB10,0x007F0000); // ordinary 6-byte event, arbitrary later data remains bounded
    for(int i=0;i<256&&!p.error;++i)ms_tick(&p);
    fixture();p.tracks[0].message=BASE+4094;p.tracks[0].pattern_active=1;ms_tick(&p);assert(p.error);
    fixture();w32(0xAA8,3);for(int i=0;i<193&&!p.error;++i)ms_tick(&p);assert(p.error);
    puts("PASS corrupt callback/sample/offset/opcode, truncated messages and invalid loop metadata");
}
static void audition(void){
    fixture();w32(0x20C,256);assert(ms_root_valid(&p.rom,BASE+0x100));assert(ms_audition_sample(&p,BASE+0x100,1)&&!p.running&&g_ms_mix[0].active&&g_ms_mix[0].step==4096);
    p.mods.mute=65535;ms_update(&p);assert(!g_ms_mix[0].volL);
    render();assert(!ms_audition_sample(&p,BASE+0x100,99)&&g_ms_mix[0].active);
    ms_stop(&p);assert(!g_ms_mix[0].active);
    puts("PASS independent sample browser and invalid selection preservation");
}
static uint32_t seed=1234;
static uint32_t random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void fuzz(void){
    for(unsigned run=0;run<3000;++run){fixture();p.preview=run&1;
        for(unsigned i=0x164;i<0x180;++i)data[i]=random32();
        for(unsigned i=0xB10;i<0xB58;++i)data[i]=random32();
        for(unsigned i=0;i<sizeof(p.mods);++i)((uint8_t *)&p.mods)[i]=random32();
        for(unsigned i=0;i<96;++i){ms_tick(&p);ms_update(&p);render();}
    }puts("PASS 3,000 randomized macro/song/control cases");
}
int main(void){playback();preview();macros();safety();audition();fuzz();return 0;}
