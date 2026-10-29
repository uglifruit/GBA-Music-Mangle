// Standalone experimental GBA entry point. Existing main/UI/mod/save-slot code is not linked.
#include "../krawall.h"
#include "../discovery.h"
#include "../mangle_adapter.h"
#include "../pcm_mixer.h"
#include "../../../payload/gba.h"
#include "../../../payload/gfx.h"
#include "../../../payload/irq.h"
#include "../../../payload/link.h"
#include "../../../payload/audio.h"
#include "../../../payload/rom.h"
static KrPlayer player;
static KrDiscovery found;
static KrRom cart;
static unsigned module_ix,sample_ix,instrument_ix;
static uint8_t ready,paused=1;
static uint16_t last_time;
static uint32_t accumulator;
static void service(void){
    link_service();
    uint16_t now=R16(0x108),dt=(uint16_t)(now-last_time);last_time=now;
    if(!ready||paused)return;
    if(dt>1024)dt=1024;
    // Q16 ticks/sec * dt / 16384, accumulated in Q16 ticks. Rate <= 102 Hz by BPM field.
    accumulator+=(kr_tick_hz_q16(&player)>>8)*dt;
    unsigned catchup=0;
    while(accumulator>=(1u<<22)&&catchup++<8){accumulator-=1u<<22;kr_tick(&player);}
    link_post_pulse((uint8_t)player.beats,(uint8_t)player.loops);
    link_post_note(player.tracks[0].note,player.tracks[0].gate);
}
void gfx_idle(void){service();}
static void label(int y,const char *name,uint32_t v){char hex[9];hex32(hex,v);text(8,y,name,COL_MID,1);text(140,y,hex,COL_HEX,1);}
static void frame(void){while(R16(6)<160)service();while(R16(6)>=160)service();}
static void screen(void){
    srect(0,0,240,160,COL_BG);
    text(8,4,"KRAWALL TRIAL - EXPERIMENTAL",COL_TITLE,1);
    text(8,16,g_romTitle,COL_MID,1);
    text(150,16,found.module_count?(found.formats[module_ix]?"2003":"MODERN"):"NO MODULE",COL_WAIT,1);
    label(28,"MODULE",found.module_count?found.modules[module_ix]:0);
    label(39,"SAMPLE BANK",found.sample_count?found.samples[sample_ix].address:0);
    label(50,"INSTR BANK",found.instrument_count?found.instruments[instrument_ix].address:0);
    label(61,"ORDER / ROW",((uint32_t)player.order<<16)|player.row);
    label(72,"UNSUPPORTED FX",player.unsupported[0]);
    label(83,"FX HIGH / ERR",player.unsupported[1]|((uint32_t)player.error<<31));
    text(8,96,paused?"STOPPED - A TO START":"PLAYING - START TO PAUSE",COL_OK,1);
    text(8,108,"L/R SONG  UP/DN SAMPLE BANK",COL_MID,1);
    text(8,119,"LEFT/RIGHT INSTRUMENT BANK",COL_MID,1);
    text(8,130,"A RESTART  B MUTE  SELECT RESCAN",COL_MID,1);
    text(8,143,"PCM12  NO PATCH SAVES OR CV MAP",COL_WAIT,1);
}
static void begin(void){
    paused=1;kr_stop(&player);accumulator=0;
    if(!found.module_count||!found.sample_count)return;
    KrSong song;
    song.module=found.modules[module_ix];song.format=found.formats[module_ix];
    song.samples=found.samples[sample_ix].address;song.sample_count=found.samples[sample_ix].count;
    song.instruments=found.instrument_count?found.instruments[instrument_ix].address:0;
    song.instrument_count=found.instrument_count?found.instruments[instrument_ix].count:0;
    player.error=0;
    if(kr_start(&player,&song)){paused=0;kr_tick(&player);last_time=R16(0x108);}
    else player.error=1;
}
static void discover(void){
    ready=0;paused=1;kr_stop(&player);
    volatile uint8_t *b=(volatile uint8_t *)&found;for(unsigned i=0;i<sizeof(found);++i)b[i]=0;
    module_ix=sample_ix=instrument_ix=0;
    srect(0,0,240,160,COL_BG);text(8,8,"SCANNING KRAWALL CANDIDATES",COL_TITLE,1);
    if(rom_init()){
        cart.data=ROM_PTR(ROM_BASE);cart.base=ROM_BASE;cart.size=g_romSize;player.rom.size=g_romSize;
        for(uint32_t off=0;off<cart.size;off+=0x4000){
            unsigned bytes=cart.size-off;if(bytes>0x4000)bytes=0x4000;
            kr_discover_slice(&found,&cart,ROM_BASE+off,ROM_BASE+off+bytes);
            service();
            if(!(off&0x3FFFF)){srect(0,30,240,35,COL_BG);label(32,"ROM OFFSET",off);label(45,"MODULES",found.module_count);}
        }
    }
    ready=1;screen(); // Explicit A starts sound; heuristic candidates are not auto-auditioned.
}
int main(void){
    gfx_init();irq_init();link_init();
    R16(0x10A)=0;R16(0x108)=0;R16(0x10A)=0x83;last_time=R16(0x108);
    cart.data=ROM_PTR(ROM_BASE);cart.base=ROM_BASE;cart.size=0;
    KrHooks hooks;kr_mixer_hooks(&hooks,&player);kr_init(&player,cart,&hooks);
    audio_init();g_kr_gain=8; // half level for first hardware audition
    discover();uint16_t previous=0;unsigned display=0;
    for(;;){
        frame();uint16_t keys=(uint16_t)(~R16(0x130)&0x3FF),press=keys&~previous;previous=keys;
        if(press&(1u<<2))discover(); // SELECT
        int changed=0;
        if((press&(1u<<8))&&found.module_count){module_ix=module_ix+1<found.module_count?module_ix+1:0;changed=1;}
        if((press&(1u<<9))&&found.module_count){module_ix=module_ix?module_ix-1:found.module_count-1;changed=1;}
        if((press&(1u<<6))&&found.sample_count){sample_ix=sample_ix+1<found.sample_count?sample_ix+1:0;changed=1;}
        if((press&(1u<<7))&&found.sample_count){sample_ix=sample_ix?sample_ix-1:found.sample_count-1;changed=1;}
        if((press&(1u<<4))&&found.instrument_count){instrument_ix=instrument_ix+1<found.instrument_count?instrument_ix+1:0;changed=1;}
        if((press&(1u<<5))&&found.instrument_count){instrument_ix=instrument_ix?instrument_ix-1:found.instrument_count-1;changed=1;}
        if(changed){paused=1;kr_stop(&player);}
        if(press&1u)begin();
        if(press&(1u<<1)){player.mods.mute^=UINT32_MAX;kr_update(&player);}
        if((press&(1u<<3))&&player.running){paused^=1;last_time=R16(0x108);}
        if(!(display++%8)||press)screen();
    }
}
