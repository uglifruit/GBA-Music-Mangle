// Synthetic native ROM fixtures; tests never require a commercial ROM.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mangle_adapter.h"
#define BASE 0x08000000u
#define SIZE 32768u
static uint8_t rom[SIZE];
_Alignas(4) uint8_t g_hostIo[0x400];
static GxPlayer player;
static unsigned bad,unsupported;
static void w16(unsigned a,unsigned v){rom[a]=v;rom[a+1]=v>>8;}
static void w32(unsigned a,uint32_t v){w16(a,v);w16(a+2,v>>16);}
static void event(void *ctx,unsigned e,uint32_t v){(void)ctx;(void)v;bad+=e==GX_EVENT_BAD_DATA;unsupported+=e==GX_EVENT_UNSUPPORTED;}
static void fixture(void){
    memset(rom,0,sizeof(rom));gx_pcm_init();bad=unsupported=0;
    // Header; one channel, eight rows, two orders, one shared compressed pattern.
    w16(0x1000,1);w16(0x1002,8);w16(0x1004,2);w16(0x1008,256);
    w32(0x100C,BASE+0x2000);w32(0x1010,BASE+0x3000);w32(0x1014,BASE+0x3100);
    w16(0x1018,15769);w32(0x1020,BASE+0x4000);
    // Two orders: second transposed by 12 semitones.
    rom[0x4006]=12;
    const uint8_t pattern[]={0,49|128,1,0xFF,7};memcpy(rom+0x2000,pattern,sizeof(pattern));
    w32(0x3000,BASE+0x3200);rom[0x3200]=1;w32(0x3004,BASE+0x3300);
    rom[0x3301]=1;w32(0x330C,BASE+0x3500);rom[0x3310]=1;rom[0x3311]=1;w32(0x3314,BASE+0x3600);
    w32(0x331C,0);w32(0x3320,0);w32(0x3324,256); // wave start/loop begin/end
    rom[0x3500]=1;rom[0x3501]=rom[0x3502]=rom[0x3503]=255;rom[0x3508]=255;
    rom[0x3600]=3;rom[0x3602]=1; // relative instrument pitch +1, slot 1
    w32(0x3100,BASE+0x3800);w32(0x3104,0);w32(0x3108,BASE+0x5000);w32(0x310C,256);
    for(unsigned i=0;i<=256;++i)rom[0x5000+i]=(uint8_t)(16+(i%32)*7);
    GxHooks hooks;gx_mixer_hooks(&hooks,&player);hooks.event=event;
    GxRom r={rom,BASE,SIZE};gx_init(&player,r,&hooks);assert(gx_start(&player,BASE+0x1000));
}
static void render(void){int8_t l[16],r[16];gx_pcm_render(l,r);}
static void playback(void){
    fixture();uint32_t headers[8];assert(gx_scan(&player.rom,BASE,BASE+SIZE,headers,8)==1&&headers[0]==BASE+0x1000);
    gx_frame(&player);assert(player.tracks[0].notes==1&&player.tracks[0].gate&&g_gx_mix[0].active);
    assert(g_gx_mix[0].step>2080&&g_gx_mix[0].step<2100);
    int energy=0;for(int block=0;block<4;++block){int8_t l[16],r[16];gx_pcm_render(l,r);for(int i=0;i<16;++i)energy+=l[i]*l[i]+r[i]*r[i];}assert(energy>0);
    uint32_t gen=g_gx_mix[0].gen;int32_t position=g_gx_mix[0].pos;
    player.mods.mute=1;gx_update(&player);assert(!g_gx_mix[0].volL);render();assert(g_gx_mix[0].pos!=position&&g_gx_mix[0].gen==gen);
    player.mods.mute=0;player.mods.pitch=12*256;gx_update(&player);assert(g_gx_mix[0].step>4170&&g_gx_mix[0].volL);
    assert(gx_seek(&player,1,0));gx_frame(&player);assert(g_gx_mix[0].step>8350&&player.tracks[0].notes==2);
    for(int i=0;i<48;++i)gx_frame(&player);
    assert(player.loops>=1);
    player.repeat=0;for(int i=0;i<100;++i)gx_frame(&player);assert(!player.running);
    assert(gx_restart(&player)&&player.mods.pitch==3072);
    puts("PASS header scan, compressed note/RLE, PCM, instrument pitch, mute continuity, transpose, seek, repeat/stop");
}
static void effects(void){
    fixture();const uint8_t pattern[]={0,49,1,15,2,0xFA,12,0,0x80,0xFA,7,0x32,0xFF,4};memcpy(rom+0x2000,pattern,sizeof(pattern));
    gx_frame(&player);assert(player.speed[0]==2&&player.row==0);gx_frame(&player);gx_frame(&player);assert(player.tracks[0].volume==0);
    gx_frame(&player);gx_frame(&player);gx_frame(&player);gx_frame(&player);assert(player.speed[0]==2&&player.speed[1]==3);
    fixture();rom[0x2001]=0xFA;rom[0x2002]=3;rom[0x2003]=4;gx_frame(&player);assert(unsupported==1&&(player.unsupported[0]&(1u<<3)));
    fixture();rom[0x3605]=5;rom[0x3604]=0;for(int i=0;i<10;++i)gx_frame(&player);assert(player.tracks[0].perf_index==0&&player.running);
    fixture();rom[0x331A]=1;gx_frame(&player);assert(player.unsupported[2]&(1u<<(GX_FEATURE_MODULATION-64)));
    puts("PASS speed, alternating speed, volume zero, bounded performance jumps, unsupported diagnostics");
}
static void envelopes(void){
    fixture();rom[0x3500]=3;rom[0x3501]=1;
    w16(0x3504,0);rom[0x3508]=255;w16(0x350C,2);rom[0x3510]=128;w16(0x3514,4);rom[0x3518]=0;
    gx_frame(&player);unsigned first=g_gx_mix[0].volL;gx_frame(&player);assert(g_gx_mix[0].volL<first);
    for(int i=0;i<5;++i)gx_frame(&player);
    assert(player.tracks[0].env_time==2);
    player.tracks[0].released=1;for(int i=0;i<4;++i)gx_frame(&player);assert(!player.tracks[0].active);
    puts("PASS envelope interpolation, sustain and note release");
}
static void malicious(void *ctx,unsigned c,GxVoice *v){(void)ctx;(void)c;v->data=UINT32_MAX;}
static void safety(void){
    fixture();rom[0x2001]=0xFF;rom[0x2002]=0;gx_frame(&player);assert(player.error&&bad&&!player.running);
    fixture();rom[0x2001]=0xFF;rom[0x2002]=9;gx_frame(&player);assert(player.error);
    fixture();player.tracks[0].stream=BASE+SIZE-1;rom[SIZE-1]=0xFA;gx_frame(&player);assert(player.error);
    fixture();w32(0x310C,UINT32_MAX);gx_frame(&player);assert(!g_gx_mix[0].active);
    fixture();player.hooks.note=malicious;gx_frame(&player);assert(!g_gx_mix[0].active);
    fixture();player.mods.scrub=INT32_MIN;gx_frame(&player);assert(!g_gx_mix[0].active);
    fixture();memset(rom+0x5000,128,257);gx_frame(&player);for(int b=0;b<4;++b){int8_t l[16],r[16];gx_pcm_render(l,r);for(int i=0;i<16;++i)assert(!l[i]&&!r[i]);}
    puts("PASS malformed RLE, truncated effects, bad samples, callback bounds, scrub range and unsigned silence");
}
static void slots_and_bridge(void){
    fixture();rom[0x3303]=2; // slot three, with an empty slot two
    w32(0x333C,256);w32(0x3110,BASE+0x5200);w32(0x3114,256);
    memset(rom+0x5200,160,257);rom[0x3311]=2;
    rom[0x3608]=3;rom[0x360A]=3;
    gx_frame(&player);assert(g_gx_mix[0].data==(const int8_t *)(rom+0x5000));
    gx_frame(&player);assert(g_gx_mix[0].data==(const int8_t *)(rom+0x5200));
    PlayerMods mods={0};mods.tempoU=256;mods.scrubLen=4096;Track followers[PL_TRACKS];
    assert(!gx_mangle_sync(&player,&mods,followers)&&followers[0].notes==1&&followers[0].lastKey==60);
    mods.attack=1;mods.vibrato=1;mods.gate=1;mods.chaos=1;
    assert(gx_mangle_sync(&player,&mods,followers)==(GX_UNMAPPED_ENVELOPE|GX_UNMAPPED_LFO|GX_UNMAPPED_GATE|GX_UNMAPPED_CHAOS));
    fixture();memcpy(rom+0x6FF0,rom+0x1000,36);uint32_t out[4];
    assert(gx_scan(&player.rom,BASE+0x6000,BASE+0x7000,out,4)==1&&out[0]==BASE+0x6FF0);
    puts("PASS performance waveform switching across empty slots, control/follower bridge, scan chunk boundary");
}
static uint32_t seed=42;
static uint32_t random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void fuzz(void){
    for(unsigned i=0;i<3000;++i){fixture();for(unsigned a=0x2001;a<0x2100;++a)rom[a]=random32();
        if(i&1)for(unsigned a=0x3300;a<0x3800;++a)rom[a]=random32();
        for(unsigned a=0;a<sizeof(player.mods);++a)((uint8_t *)&player.mods)[a]=random32();
        for(unsigned n=0;n<96;++n){gx_frame(&player);gx_update(&player);render();}
    }puts("PASS 3,000 randomized pattern/instrument/modulation cases");
}
int main(void){playback();effects();envelopes();safety();slots_and_bridge();fuzz();return 0;}
