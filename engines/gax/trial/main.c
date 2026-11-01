// Separate GAX audition payload; the production main/UI/mod/save code is not linked.
#include "../gax.h"
#include "../mangle_adapter.h"
#include "../../../payload/gba.h"
#include "../../../payload/gfx.h"
#include "../../../payload/irq.h"
#include "../../../payload/link.h"
#include "../../../payload/audio.h"
#include "../../../payload/rom.h"
static GxPlayer player;
static uint32_t songs[64];
static unsigned song_count,song_ix,track_ix;
static uint8_t ready,paused=1;
static uint16_t last_time;
static uint32_t accumulator;
static char version[32];
static void service(void){
    link_service();uint16_t now=R16(0x108),dt=(uint16_t)(now-last_time);last_time=now;
    if(!ready||paused)return;
    if(dt>1024)dt=1024;
    accumulator+=(gx_frame_hz_q16(&player)>>8)*dt;
    unsigned catchup=0;
    while(accumulator>=(1u<<22)&&catchup++<8){accumulator-=1u<<22;gx_frame(&player);}
    link_post_note(player.tracks[track_ix].note,player.tracks[track_ix].gate);
    // Native frames are not 24-PPQN ticks; no fabricated beat pulses are sent.
}
void gfx_idle(void){service();}
static void label(int y,const char *name,uint32_t value){char hex[9];hex32(hex,value);text(8,y,name,COL_MID,1);text(142,y,hex,COL_HEX,1);}
static void frame(void){while(R16(6)<160)service();while(R16(6)>=160)service();}
static void screen(void){
    srect(0,0,240,160,COL_BG);text(8,4,"GAX TRIAL - 3.05 DATA",COL_TITLE,1);
    text(8,16,version[0]?version:g_romTitle,COL_MID,1);
    label(28,"SONG HEADER",song_count?songs[song_ix]:0);
    label(39,"ORDER / ROW",((uint32_t)player.order<<16)|player.row);
    label(50,"UNSUPPORTED SEQ",player.unsupported[0]);label(61,"UNSUPPORTED PERF",player.unsupported[1]);
    label(72,"FEATURES / ERROR",player.unsupported[2]|((uint32_t)player.error<<31));
    label(83,"TRACK / SONGS",((uint32_t)track_ix<<16)|song_count);
    text(8,96,paused?"STOPPED / PAUSED - A START":"PLAYING - START PAUSES",COL_OK,1);
    text(8,108,"L/R SONG  UP/DN FOLLOW TRACK",COL_MID,1);
    text(8,119,"B TRACK MUTE  LEFT/RIGHT PITCH",COL_MID,1);
    text(8,130,"A RESET  SELECT RESCAN",COL_MID,1);
    text(8,143,"PCM12 - NO CV MAP OR PATCH SAVE",COL_WAIT,1);
}
static void begin(void){
    paused=1;gx_stop(&player);accumulator=0;if(!song_count)return;
    player.error=0;if(gx_start(&player,songs[song_ix])){track_ix=0;paused=0;gx_frame(&player);last_time=R16(0x108);}else player.error=1;
}
static void signature(uint32_t a,uint32_t remaining){
    static const char prefix[]="GAX Sound Engine";
    if(version[0]||remaining<sizeof(prefix))return;
    for(unsigned i=0;i<sizeof(prefix)-1;++i)if(ROM_PTR(a)[i]!=(uint8_t)prefix[i])return;
    unsigned n=remaining<sizeof(version)-1?remaining:sizeof(version)-1;
    for(unsigned i=0;i<n;++i){uint8_t c=ROM_PTR(a)[i];if(c<32||c>126){version[i]=0;return;}version[i]=(char)c;}
    version[n]=0;
}
static void discover(void){
    ready=0;paused=1;gx_stop(&player);song_count=song_ix=track_ix=0;version[0]=0;
    srect(0,0,240,160,COL_BG);text(8,8,"SCANNING GAX 3.05 SONG DATA",COL_TITLE,1);
    if(rom_init()){
        player.rom.data=ROM_PTR(ROM_BASE);player.rom.base=ROM_BASE;player.rom.size=g_romSize;
        for(uint32_t off=0;off<g_romSize;off+=0x4000){
            unsigned bytes=g_romSize-off;if(bytes>0x4000)bytes=0x4000;
            if(song_count<64)song_count+=gx_scan(&player.rom,ROM_BASE+off,ROM_BASE+off+bytes,songs+song_count,64-song_count);
            if(!version[0])for(unsigned i=0;i<bytes;++i)if(ROM_PTR(ROM_BASE+off+i)[0]=='G')signature(ROM_BASE+off+i,g_romSize-off-i);
            service();if(!(off&0x3FFFF)){srect(0,30,240,35,COL_BG);label(32,"ROM OFFSET",off);label(45,"SONG CANDIDATES",song_count);}
        }
    }
    ready=1;screen();
}
int main(void){
    gfx_init();irq_init();link_init();R16(0x10A)=0;R16(0x108)=0;R16(0x10A)=0x83;last_time=R16(0x108);
    GxRom cart={ROM_PTR(ROM_BASE),ROM_BASE,0};GxHooks hooks;gx_mixer_hooks(&hooks,&player);gx_init(&player,cart,&hooks);
    audio_init();g_gx_gain=8;discover();uint16_t previous=0;unsigned display=0;
    for(;;){
        frame();uint16_t keys=(uint16_t)(~R16(0x130)&0x3FF),press=keys&~previous;previous=keys;
        if(press&(1u<<2))discover();
        int changed=0;
        if((press&(1u<<8))&&song_count){song_ix=song_ix+1<song_count?song_ix+1:0;changed=1;}
        if((press&(1u<<9))&&song_count){song_ix=song_ix?song_ix-1:song_count-1;changed=1;}
        if(changed){paused=1;gx_stop(&player);}
        if(press&1u)begin();
        if((press&(1u<<3))&&player.running){paused^=1;last_time=R16(0x108);}
        if((press&(1u<<6))&&player.channels)track_ix=track_ix+1<player.channels?track_ix+1:0;
        if((press&(1u<<7))&&player.channels)track_ix=track_ix?track_ix-1:(unsigned)player.channels-1;
        if(press&(1u<<1)){player.mods.mute^=1u<<track_ix;gx_update(&player);}
        if((press&(1u<<4))&&player.mods.pitch<24*256){player.mods.pitch+=256;gx_update(&player);}
        if((press&(1u<<5))&&player.mods.pitch>-24*256){player.mods.pitch-=256;gx_update(&player);}
        if(!(display++%8)||press)screen();
    }
}
