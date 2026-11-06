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
// The demo (tests/make_demo.py): two-note chords on track 4 playing macro 1 (code at 0x164..0x17F):
// STARTSAMPLE 1, WAIT for key-off, STOP, END. Its notes last 48 ticks.
static void fixture(void){
    memcpy(data,ms_demo_rom,sizeof(data));bad=unknown=0;ms_pcm_init();MsRom r={data,BASE,sizeof(data)};
    MsHooks hooks;ms_mixer_hooks(&hooks,&p);hooks.event=event;ms_init(&p,r,&hooks);
    MsSong song;assert(ms_song_info(&p.rom,BASE+0x100,0,&song));assert(ms_start(&p,&song));p.preview=0;
}
static void render(void){int8_t l[16],r[16];ms_pcm_render(l,r);}
static unsigned active(void){unsigned n=0;for(unsigned i=0;i<MS_VOICES;++i)n+=p.voices[i].active;return n;}
static unsigned ended(void){unsigned n=0;for(unsigned i=0;i<MS_VOICES;++i)n+=p.voices[i].active&&p.voices[i].ended;return n;}
static void ticks(unsigned n){while(n--)ms_tick(&p);}
static void frames(unsigned n){while(n--)ms_frame(&p);}
static void playback(void){
    fixture();uint32_t roots[4];assert(ms_scan(&p.rom,BASE,BASE+4096,roots,4)==1&&roots[0]==BASE+0x100);
    assert(ms_tick_hz_q16(&p)==192*65536);ms_tick(&p);assert(p.tracks[4].notes==2&&active()==2&&p.tracks[4].gate);
    assert(g_ms_mix[0].step==4096&&g_ms_mix[1].step>5100);
    int energy=0;for(int block=0;block<4;++block){int8_t l[16],r[16];ms_pcm_render(l,r);for(int i=0;i<16;++i)energy+=l[i]*l[i]+r[i]*r[i];}assert(energy>0);
    uint32_t gen=g_ms_mix[0].gen;int32_t position=g_ms_mix[0].pos;
    p.mods.mute=1u<<4;ms_update(&p);assert(!g_ms_mix[0].volL&&!g_ms_mix[1].volR);render();assert(g_ms_mix[0].pos!=position&&g_ms_mix[0].gen==gen);
    p.mods.mute=0;p.mods.pitch=3072;ms_update(&p);assert(g_ms_mix[0].step==8192&&g_ms_mix[0].volL);
    // Key-off at the note's end only marks the voices; the macro's WAIT sees it on the next frame.
    ticks(48);assert(active()==2&&!p.tracks[4].gate&&p.voices[0].keyed);
    frames(1);assert(!active());
    ticks(48);assert(p.bpm==150&&p.tracks[4].notes==4);
    ticks(96);assert(p.loops==1&&p.tracks[4].notes==6&&!unknown&&!bad);
    puts("PASS relative root/song pointers, chords, 96-PPQN tempo, key-off on the tick, control track, loops on the first track, PCM/mute/pitch");
}
static void timing(void){
    // WAIT counts video frames, whatever the ticks do: STARTSAMPLE, WAIT 3 frames, STOP, END.
    fixture();memset(data+0x164,0,0x1C);
    data[0x164]=15;w16(0x166,1);data[0x16C]=4;w16(0x16E,3);data[0x170]=5;
    ms_tick(&p);assert(active()==2);ticks(30);assert(active()==2);
    frames(2);assert(active()==2);frames(1);assert(!active());
    // A stopped clock holds notes: no ticks, no key-off, the WAIT for it never ends.
    fixture();ms_tick(&p);frames(200);assert(active()==2&&!p.voices[0].keyed);
    puts("PASS WAIT in frames, ticks only start and end notes, a stopped clock holds");
}
static void routing(void){
    // Tracks 0-3 play on the Game Boy channels: the song moved to track 1, a square macro (VOICE_ON
    // duty 2, KEYON, WAIT for ever) sounds on channel 1 and no PCM voice starts.
    fixture();w32(0xA20+4*4,0);w32(0xA20+1*4,0xA90-0xA10);
    MsSong song;assert(ms_song_info(&p.rom,BASE+0x100,0,&song)&&ms_start(&p,&song));p.preview=0;
    memset(data+0x164,0,0x1C);data[0x164]=32;data[0x165]=2;data[0x166]=3;data[0x168]=4;w16(0x16A,65535);
    ms_tick(&p);assert(!active()&&p.psg[1].on&&p.psg[1].duty==2&&p.psg[1].trigger&&p.tracks[1].gate);
    // A sample macro there is silent (only the wave channel takes a sample, a waveform).
    fixture();w32(0xA20+4*4,0);w32(0xA20+1*4,0xA90-0xA10);assert(ms_start(&p,&song));p.preview=0;
    ms_tick(&p);assert(!active()&&!p.psg[1].on);
    puts("PASS tracks 0-3 on the Game Boy channels, sample macros silent there");
}
static void envelopes(void){
    // SETADSR 0 (root+4's table -> root+0x90): attack 0x3300, decay 0x1000 to 0x8000, release 0x2000.
    fixture();w32(0x180,0x90);w16(0x190,0x3300);w16(0x192,0x1000);w16(0x194,0x8000);w16(0x196,0x2000);
    memset(data+0x164,0,0x1C);data[0x164]=14;data[0x168]=15;w16(0x16A,1);data[0x170]=4;data[0x171]=1;w16(0x172,65535);
    data[0x174]=4;w16(0x176,40);data[0x178]=5;
    ms_tick(&p);assert(active()==2&&p.voices[0].level==0&&!g_ms_mix[0].volL);
    frames(5);assert(p.voices[0].level==0xFF00);
    frames(8);assert(p.voices[0].level==0x8000&&g_ms_mix[0].volL);
    ticks(48);frames(4);assert(p.voices[0].level==0&&!p.voices[0].env&&active()==2);
    frames(40);assert(!active());
    // A fade (op 21) out from full, 0x1000 a frame: 15 frames to silence.
    fixture();memset(data+0x164,0,0x1C);data[0x164]=15;w16(0x166,1);data[0x16C]=21;w16(0x16E,0x1000);data[0x170]=4;w16(0x172,65535);
    ms_tick(&p);frames(1);assert(p.voices[0].level==0xEF00);frames(15);assert(!p.voices[0].level&&!g_ms_mix[0].volL);
    puts("PASS SETADSR attack/decay/sustain/release by frame, fades, the level scales the volume");
}
static void keymaps(void){
    // Macro 0 becomes a one-entry keymap {sample 1, transpose +12, pan 0}; macro 1 plays it at entry
    // note - 60 and carries on to SETVOLUME 0x40 (a keymap does not end the macro).
    fixture();data[0x14A]=1;w16(0x14C,1);w16(0x14E,1);w16(0x150,12*256);data[0x152]=0;data[0x154]=0;
    memset(data+0x164,0,0x1C);data[0x164]=29;w16(0x166,0);w16(0x168,(uint16_t)-60);data[0x16A]=2;data[0x16B]=0x40;data[0x16C]=4;w16(0x16E,65535);
    ms_tick(&p);assert(p.voices[0].active&&p.voices[0].has_sample&&p.voices[0].volume==64&&g_ms_mix[0].step==8192);
    assert(!p.voices[1].has_sample&&(p.warnings&MS_WARN_KEYMAP_RANGE)); // note 64: past the one entry, nothing
    // Byte 6 set: unpitched, the transpose ignored (the sample's own rate).
    fixture();data[0x14A]=1;w16(0x14C,1);w16(0x14E,1);w16(0x150,12*256);data[0x154]=1;
    memset(data+0x164,0,0x1C);data[0x164]=29;w16(0x166,0);w16(0x168,(uint16_t)-60);data[0x16A]=4;w16(0x16C,65535);
    ms_tick(&p);assert(p.voices[0].fixed&&g_ms_mix[0].step==4096);
    puts("PASS keymaps play an entry and the macro carries on; unpitched entries");
}
static void preview(void){
    fixture();memset(data+0x164,0,0x1C);data[0x164]=19;
    data[0x16A]=15;w16(0x16C,1);data[0x172]=4;w16(0x174,65535);
    ms_tick(&p);assert(!active()&&unknown==2&&(p.unsupported[0]&(1u<<19)));
    fixture();p.preview=1;memset(data+0x164,0,0x1C);data[0x164]=19;
    data[0x16A]=15;w16(0x16C,1);data[0x172]=4;w16(0x174,65535);
    ms_tick(&p);assert(active()==2&&unknown==2);
    // END leaves the sample playing (the driver lets go of the voice).
    // A chord of one-shots: both notes get voices before either macro runs (and ends).
    fixture();data[0x16C]=0;ms_tick(&p);assert(active()==2&&ended()==2);
    fixture();data[0x164]=13;data[0x165]=0;w32(0x166,0);ms_tick(&p);assert(!active()&&p.rejected==2&&(p.unsupported[1]&(1u<<31)));
    puts("PASS strict rejection, explicit preview skipping, END lets the sample play, bounded self-jumping macros");
}
static void macros(void){
    fixture();memset(data+0x164,0,0x1C);
    // SETNOTE absolute 72 (op 8: flags, then the pitch u16 in 1/256 semitones; op 9 is the relative
    // ADDNOTE, as the carts' data and their driver show), STARTSAMPLE, SCALEVOLUME x0.5, WAIT forever.
    data[0x164]=8;data[0x165]=0;data[0x166]=0;data[0x167]=72;
    data[0x168]=15;w16(0x16A,1);
    data[0x170]=47;data[0x171]=1;w16(0x174,128);
    data[0x176]=4;w16(0x178,65535);
    ms_tick(&p);assert(active()==2&&g_ms_mix[0].step==8192&&p.voices[0].volume==50);
    fixture();memset(data+0x164,0,0x1C);
    data[0x164]=16;w32(0x166,0x179-0x164); // keyoff trap
    data[0x16A]=15;w16(0x16C,1);data[0x172]=4;w16(0x174,65535);
    data[0x179]=0;
    ms_tick(&p);assert(active()==2&&!ended());
    ticks(48);frames(1);assert(ended()==2);
    fixture();data[0x16C]=47;data[0x16D]=0;data[0x16E]=0;data[0x16F]=0;w16(0x170,256);data[0x172]=4;w16(0x174,65535);
    ms_tick(&p);assert(active()==2);
    // SPLITVEL jumps when the velocity is at least its byte (the chord's 100 and 90; split at 95).
    fixture();memset(data+0x164,0,0x1C);data[0x164]=24;data[0x165]=95;w32(0x166,0x172-0x164);
    data[0x16A]=2;data[0x16B]=10;data[0x16C]=4;w16(0x16E,65535);
    data[0x172]=2;data[0x173]=20;data[0x174]=4;w16(0x176,65535);
    ms_tick(&p);assert(p.voices[0].active&&p.voices[0].volume==20&&p.voices[1].active&&p.voices[1].volume==10);
    puts("PASS absolute pitch, volume scale, key-off trap/END, SPLITVEL at or above");
}
static void corrupt(void *ctx,unsigned i,MsPcm *v){(void)ctx;(void)i;v->data=UINT32_MAX;}
static void safety(void){
    fixture();p.hooks.note=corrupt;ms_tick(&p);assert(!active()&&bad==2);
    fixture();w32(0x328,UINT32_MAX);ms_tick(&p);assert(!active()&&p.rejected==2);
    fixture();p.mods.scrub=INT32_MIN;ms_tick(&p);assert(!active());
    fixture();data[0x164]=255;ms_tick(&p);assert(!active()&&unknown==2&&(p.unsupported[1]&(1u<<30)));
    fixture();w32(0xB10,0x007F0000); // ordinary 6-byte event, arbitrary later data remains bounded
    for(int i=0;i<256&&!p.error;++i){ms_tick(&p);ms_frame(&p);}
    fixture();p.tracks[4].message=BASE+4094;p.tracks[4].pattern_active=1;ms_tick(&p);assert(p.error);
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
        for(unsigned i=0x180;i<0x1A0;++i)data[i]=random32();      // the ADSR table a SETADSR may read
        for(unsigned i=0xB10;i<0xB58;++i)data[i]=random32();
        for(unsigned i=0;i<sizeof(p.mods);++i)((uint8_t *)&p.mods)[i]=random32();
        for(unsigned i=0;i<96;++i){ms_tick(&p);if(i&1)ms_frame(&p);ms_update(&p);render();}
    }puts("PASS 3,000 randomized macro/song/control cases");
}
int main(void){playback();timing();routing();envelopes();keymaps();preview();macros();safety();audition();fuzz();return 0;}
