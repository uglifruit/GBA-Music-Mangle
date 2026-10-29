// Isolated MusyX PCM trial. Production player, modulator, UI and saves are not linked.
#include "../musyx.h"
#include "../mixer_adapter.h"
#include "../demo_data.h"
#include "../../../payload/gba.h"
#include "../../../payload/gfx.h"
#include "../../../payload/irq.h"
#include "../../../payload/link.h"
#include "../../../payload/audio.h"
#include "../../../payload/rom.h"
static MsPlayer player;
static MsCatalogue catalogue;
static uint32_t roots[32];
static unsigned root_count,root_ix,item_ix;
static uint8_t ready,paused=1,demo,sample_mode;
static uint16_t last_time;
static uint32_t accumulator;
static void service(void){
    link_service();uint16_t now=R16(0x108),dt=(uint16_t)(now-last_time);last_time=now;
    if(!ready)return;
    if(sample_mode){ms_update(&player);return;}
    if(paused)return;
    // dt is capped at ~16ms; discard older debt instead of overflowing/bursting after a stall.
    if(dt>256)dt=256;
    accumulator+=(ms_tick_hz_q16(&player)>>8)*dt;
    unsigned catchup=0;while(accumulator>=(1u<<22)&&catchup++<12){accumulator-=1u<<22;ms_tick(&player);}
    if(accumulator>=(1u<<26))accumulator=0;
    link_post_note(player.tracks[0].note,player.tracks[0].gate);
    link_post_pulse((uint8_t)(player.ticks/96),(uint8_t)player.loops);
}
void gfx_idle(void){service();}
static void label(int y,const char *name,uint32_t value){char hex[9];hex32(hex,value);text(8,y,name,COL_MID,1);text(142,y,hex,COL_HEX,1);}
static void frame(void){while(R16(6)<160)service();while(R16(6)>=160)service();}
static void screen(void){
    srect(0,0,240,160,COL_BG);text(8,4,"MUSYX PCM EXPERIMENT",COL_TITLE,1);
    text(8,16,demo?"BUILT-IN TEST DATA":g_romTitle,COL_MID,1);
    text(180,16,paused?(player.running?"PAUSE":"STOP"):(sample_mode&&!player.voices[0].active?"DONE":"PLAY"),COL_OK,1);
    label(28,"ROOT",root_count?roots[root_ix]:0);
    label(39,sample_mode?"SAMPLE / COUNT":"SONG / COUNT",(item_ix<<16)|(sample_mode?catalogue.samples:catalogue.songs));
    label(50,"BPM / TICK",((uint32_t)player.bpm<<16)|(player.ticks&65535));
    label(61,"UNKNOWN OPCODES",player.unsupported[0]);label(72,"UNKNOWN HIGH",player.unsupported[1]);
    label(83,"WARN / ERROR",player.warnings|((uint32_t)player.error<<31));
    text(8,96,player.preview?"PREVIEW (APPROXIMATE)":"STRICT MACRO SUBSET",COL_WAIT,1);
    text(8,108,"A PLAY  START PAUSE  B MUTE ALL",COL_MID,1);
    text(8,119,"L/R ITEM  UP/DN ROOT",COL_MID,1);
    text(8,130,"SELECT SONG/SAMPLE  LEFT STRICT",COL_MID,1);
    text(8,143,"RIGHT BUILT-IN DEMO / CART",COL_MID,1);
}
static void refresh(void){
    catalogue.songs=catalogue.samples=catalogue.macros=0;item_ix=0;
    if(root_count)ms_catalogue(&player.rom,roots[root_ix],&catalogue);
}
static void begin(void){
    paused=1;ms_stop(&player);accumulator=0;if(!root_count)return;
    int success=0;player.error=0;
    if(sample_mode)success=ms_audition_sample(&player,roots[root_ix],item_ix);
    else {MsSong song;if(ms_song_info(&player.rom,roots[root_ix],item_ix,&song))success=ms_start(&player,&song);}
    if(success){paused=0;if(!sample_mode)ms_tick(&player);last_time=R16(0x108);}else player.error=1;
}
static void discover(void){
    ready=0;paused=1;ms_stop(&player);root_count=root_ix=item_ix=0;player.mods.mute=0;
    srect(0,0,240,160,COL_BG);text(8,8,"SCANNING MUSYX ROOT CANDIDATES",COL_TITLE,1);
    if(demo){player.rom.data=ms_demo_rom;player.rom.base=ROM_BASE;player.rom.size=sizeof(ms_demo_rom);roots[0]=ROM_BASE+0x100;root_count=1;}
    else if(rom_init()){
        player.rom.data=ROM_PTR(ROM_BASE);player.rom.base=ROM_BASE;player.rom.size=g_romSize;
        for(uint32_t off=0;off<g_romSize;off+=0x4000){unsigned bytes=g_romSize-off;if(bytes>0x4000)bytes=0x4000;
            if(root_count<32)root_count+=ms_scan(&player.rom,ROM_BASE+off,ROM_BASE+off+bytes,roots+root_count,32-root_count);
            service();if(!(off&0x3FFFF)){srect(0,30,240,35,COL_BG);label(32,"ROM OFFSET",off);label(45,"ROOTS",root_count);}}
    }
    refresh();ready=1;screen();
}
int main(void){
    gfx_init();irq_init();link_init();R16(0x10A)=0;R16(0x108)=0;R16(0x10A)=0x83;last_time=R16(0x108);
    MsRom cart={ROM_PTR(ROM_BASE),ROM_BASE,0};MsHooks hooks;ms_mixer_hooks(&hooks,&player);ms_init(&player,cart,&hooks);
    audio_init();g_ms_gain=8;discover();uint16_t previous=0;unsigned display=0;
    for(;;){
        frame();uint16_t keys=(uint16_t)(~R16(0x130)&0x3FF),press=keys&~previous;previous=keys;
        if(press&(1u<<4)){demo^=1;sample_mode=0;discover();}
        if(press&(1u<<2)){sample_mode^=1;paused=1;ms_stop(&player);item_ix=0;}
        if(press&(1u<<5)){player.preview^=1;paused=1;ms_stop(&player);}
        if((press&(1u<<6))&&root_count){root_ix=root_ix+1<root_count?root_ix+1:0;paused=1;ms_stop(&player);refresh();}
        if((press&(1u<<7))&&root_count){root_ix=root_ix?root_ix-1:root_count-1;paused=1;ms_stop(&player);refresh();}
        unsigned count=sample_mode?catalogue.samples:catalogue.songs;
        if((press&(1u<<8))&&count){item_ix=item_ix+1<count?item_ix+1:0;paused=1;ms_stop(&player);}
        if((press&(1u<<9))&&count){item_ix=item_ix?item_ix-1:count-1;paused=1;ms_stop(&player);}
        if(press&1u)begin();
        if(press&(1u<<1)){player.mods.mute^=65535;ms_update(&player);}
        if(press&(1u<<3)){if(sample_mode){ms_stop(&player);paused=1;}else if(player.running){paused^=1;last_time=R16(0x108);}}
        if(!(display++%8)||press)screen();
    }
}
