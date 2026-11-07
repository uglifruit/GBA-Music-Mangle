// Bounded GBA MusyX parser, sequencer and SMaL interpreter, following the cart's own driver (see "the voices").
// Layout reference: MIT BinarySerializer.Audio.GBA MusyX (ed612685...); behaviour read from the driver in the carts.
#include "musyx.h"
#include "macro_lengths.h"
static void zero(void *v,unsigned n){volatile uint8_t *p=v;while(n--)*p++=0;}
static int clip(int x,int lo,int hi){return x<lo?lo:x>hi?hi:x;}
static int ok(const MsRom *r,uint32_t a,uint32_t n){return r->data&&a>=r->base&&a-r->base<=r->size&&n<=r->size-(a-r->base);}
static uint8_t b(const MsRom *r,uint32_t a){return r->data[a-r->base];}
static uint16_t h(const MsRom *r,uint32_t a){return b(r,a)|((uint16_t)b(r,a+1)<<8);}
static uint32_t w(const MsRom *r,uint32_t a){return h(r,a)|((uint32_t)h(r,a+2)<<16);}
static uint32_t relative(const MsRom *r,uint32_t anchor,uint32_t field,unsigned size){
    if(!ok(r,field,4))return 0;
    uint32_t offset=w(r,field);
    if(!offset||offset>UINT32_MAX-anchor)return 0;
    uint32_t a=anchor+offset;return ok(r,a,size)?a:0;
}
static uint32_t branch(uint32_t ip,int32_t offset){
    int64_t dest=(int64_t)ip+offset;if(dest<0||dest>UINT32_MAX)return 0;return (uint32_t)dest;
}
static unsigned table_count(const MsRom *r,uint32_t root,uint32_t table){
    uint32_t first=relative(r,root,table,1);if(first<=table||(first-table)&3)return 0;
    unsigned count=(first-table)/4;return count<=1024?count:0;
}
// A sample header: size, loop (-1 none), rate u16, root key u8 at +10. The byte at +11 is not part of
// the root: Harry Potter and the Chamber of Secrets sets it to 1 on its tiny waveform samples (seen
// 2026-11-03), and a u16 read (316) turned the whole root away.
static int sample_valid(const MsRom *r,uint32_t a){
    if(!ok(r,a,16))return 0;
    uint32_t size=w(r,a);int32_t loop=(int32_t)w(r,a+4);
    return size&&size<0x1000000&&ok(r,a+16,size)&&(loop<0||(uint32_t)loop<=size)&&h(r,a+8)>=1000&&h(r,a+8)<=48000&&b(r,a+10)<=127;
}
int ms_root_valid(const MsRom *r,uint32_t root){
    if((root&3)||!ok(r,root,32))return 0;
    uint32_t macros=relative(r,root,root,4),samples=relative(r,root,root+28,4),songs=relative(r,root,root+24,4);
    unsigned mc=table_count(r,root,macros),sc=table_count(r,root,samples);
    if(!mc||!sc||!songs)return 0;
    unsigned count=w(r,songs);if(!count||count>256||!ok(r,songs+4,count*4))return 0;
    for(unsigned i=0;i<(sc<4?sc:4);++i){uint32_t a=relative(r,root,samples+i*4,16);if(!a||!sample_valid(r,a))return 0;}
    for(unsigned i=0;i<(mc<4?mc:4);++i){uint32_t a=relative(r,root,macros+i*4,5);if(!a||b(r,a+2)>1)return 0;}
    // At least one structurally plausible song group must be present.
    uint32_t group=relative(r,root,songs+4,1050);if(!group)return 0;
    uint32_t song=group+1040;unsigned bpm=h(r,song+8);if(bpm<20||bpm>400)return 0;
    uint32_t tracks=relative(r,song,song,68),patterns=relative(r,song,song+4,4);
    if(!tracks||!patterns)return 0;
    unsigned real=0;
    for(unsigned i=0;i<17;++i){if(!w(r,tracks+i*4))continue;if(!relative(r,song,tracks+i*4,8))return 0;++real;}
    return real!=0;
}
int ms_catalogue(const MsRom *r,uint32_t root,MsCatalogue *out){
    if(!out||!ms_root_valid(r,root))return 0;
    out->macros=(uint16_t)table_count(r,root,relative(r,root,root,4));
    out->samples=(uint16_t)table_count(r,root,relative(r,root,root+28,4));
    out->songs=(uint16_t)w(r,relative(r,root,root+24,4));return 1;
}
unsigned ms_scan(const MsRom *r,uint32_t begin,uint32_t end,uint32_t *out,unsigned cap){
    unsigned n=0;if(!out||end<begin||!ok(r,begin,end-begin))return 0;
    for(uint32_t a=begin;a<end&&end-a>=4&&n<cap;a+=4)if(ms_root_valid(r,a))out[n++]=a;
    return n;
}
int ms_song_info(const MsRom *r,uint32_t root,unsigned index,MsSong *s){
    if(!s||!ms_root_valid(r,root))return 0;
    uint32_t table=relative(r,root,root+24,4);if(index>=w(r,table))return 0;
    uint32_t group=relative(r,root,table+4+index*4,1050);if(!group)return 0;
    s->root=root;s->group=group;s->song=group+1040;s->index=(uint16_t)index;return 1;
}
// A song's shape without playing it: its length in native (96-PPQN) ticks to the end of its longest
// track's last pattern, and whether its first track loops (a -2 entry; the play counts loops there
// too). Bounded; 0 if unreadable.
int ms_song_shape(const MsRom *r,uint32_t root,unsigned index,uint32_t *ticks,int *loops){
    MsSong s;*ticks=0;*loops=0;if(!ms_song_info(r,root,index,&s))return 0;
    uint32_t tracks=relative(r,s.song,s.song,68),patterns=relative(r,s.song,s.song+4,4);if(!tracks||!patterns)return 0;
    unsigned budget=20000;int lead=-1;
    for(unsigned c=0;c<16;++c){
        uint32_t a=relative(r,s.song,tracks+c*4,8);if(!a)continue;
        uint32_t time=0;if(lead<0)lead=(int)c;
        for(unsigned e=0;e<512&&ok(r,a,8)&&budget;++e,a+=8,--budget){
            int32_t delta=(int32_t)w(r,a),pattern=(int32_t)w(r,a+4);
            if(delta<0)break;
            time+=(uint32_t)delta;
            if(pattern==-1)break;
            if(pattern==-2){if((int)c==lead)*loops=1;break;}
            if(pattern<0||pattern>=4096||!ok(r,patterns,(uint32_t)pattern*4+4))break;
            uint32_t m=relative(r,s.song,patterns+(uint32_t)pattern*4,4),len=0;if(!m)break;
            while(budget&&ok(r,m,4)&&w(r,m)!=UINT32_MAX){
                --budget;len+=h(r,m);
                unsigned vel=b(r,m+2),note=b(r,m+3);
                m+=(!vel)?4:(note&128)?8:6;
            }
            if(time+len>*ticks)*ticks=time+len;
        }
    }
    return 1;
}
// ---- the voices, as the cart's own driver runs them ------------------------------------------------
// Read from the driver's code in Lady Sia (its macro jump table at 0x083F4568; Go! Go! Beckham, BattleBots,
// Denki Blocks, Ace Lightning, R2R2, Spider-Man, Incredibles, Rayman, Star Wars and Harry Potter CoS carry
// the same 54 handlers), 2026-11-06:
// - Macros, envelopes and pitch modulators run once a video frame (the game's VBlank), so WAIT counts
//   frames. Notes start and end on the sequencer's tick; a note's first macro step runs at once.
// - Tracks 0-3 play on the Game Boy channels, 4-15 on PCM voices. A macro command for the other kind
//   of voice does nothing.
// - The level (0..0xFF00, full at note-on) scales the volume: SETADSR (a table of four rates), or a fade.
// - Key-off only marks the voice (and releases an ADSR, and takes a trap): the macro decides what then.
// - END stops the macro; a PCM voice is then left alone, its sample playing on as it was, and is the
//   first a new note takes.
static void event(MsPlayer *p,unsigned kind,uint32_t value){if(p->hooks.event)p->hooks.event(p->hooks.user,kind,value);}
static unsigned random8(MsPlayer *p){p->rng^=p->rng<<13;p->rng^=p->rng>>17;p->rng^=p->rng<<5;return p->rng&255;}
static unsigned random_below(MsPlayer *p,unsigned n){unsigned x=random8(p);x|=random8(p)<<8;return (x*n)>>16;} // 0..n-1, no divide
// n / d, truncated toward zero, by shift and subtract (the payload has no divide).
static int32_t divide(int32_t n,uint32_t d){
    uint32_t u=n<0?(uint32_t)-n:(uint32_t)n,q=0,rem=0;if(!d)return n;
    for(int k=31;k>=0;--k){rem=(rem<<1)|((u>>k)&1u);if(rem>=d){rem-=d;q|=1u<<k;}}
    return n<0?-(int32_t)q:(int32_t)q;
}
static void off(MsPlayer *p,unsigned i){MsVoice *v=&p->voices[i];v->active=v->has_sample=v->sounding=0;v->ip=0;if(i<MS_VOICES&&p->hooks.stop)p->hooks.stop(p->hooks.user,i);}
// The sound stops (op 5); the macro runs on.
static void silence(MsPlayer *p,unsigned i){MsVoice *v=&p->voices[i];v->sounding=0;if(i<MS_VOICES&&v->has_sample){v->has_sample=0;if(p->hooks.stop)p->hooks.stop(p->hooks.user,i);}}
static int32_t pitch_of(const MsVoice *v){return (int32_t)v->note*256+v->offset+v->sweep[0][0]+v->sweep[1][0]+v->vib[0]+v->slide[0];}
static unsigned level_of(const MsVoice *v){unsigned lv=v->level>>8;return lv?lv+1u:0;}   // 0..256
static int muted(const MsPlayer *p,const MsVoice *v){return v->owner<16&&(p->mods.mute&(1u<<v->owner));}
static void psg_sync(MsPlayer *p){
    for(unsigned ch=0;ch<MS_PSG;++ch){
        MsPsg *s=&p->psg[ch];const MsVoice *v=&p->voices[MS_VOICES+ch];
        // the wave channel is silent until a waveform is loaded (no load from address 0)
        if(!v->active||!v->sounding||(ch==2&&!v->wave)){s->on=0;continue;}
        s->on=1;s->track=v->owner;s->pitch=pitch_of(v)+p->mods.pitch;
        s->volume=muted(p,v)?0:(uint8_t)((level_of(v)*v->volume)>>8);
        s->duty=v->duty;s->env=v->henv;s->noise=v->noise;s->wave=v->wave;s->wave_len=v->wave_len;s->pan=v->pan;
    }
}
void ms_stop(MsPlayer *p){for(unsigned i=0;i<MS_ALL;++i)off(p,i);for(unsigned ch=0;ch<MS_PSG;++ch)p->psg[ch].on=0;for(unsigned i=0;i<16;++i)p->tracks[i].gate=0;p->running=0;event(p,MS_EVENT_STOP,0);}
static int fail(MsPlayer *p,uint32_t address){p->error=1;ms_stop(p);event(p,MS_EVENT_BAD_DATA,address);return 0;}
static void reject(MsPlayer *p,unsigned i,unsigned why,uint32_t address){++p->rejected;p->why|=(uint8_t)why;p->warnings|=MS_WARN_REJECTED_VOICE;off(p,i);event(p,MS_EVENT_BAD_DATA,address);}
static int unsupported(MsPlayer *p,unsigned i,unsigned op){p->last_opcode=op;if(op<64)p->unsupported[op>>5]|=1u<<(op&31);else p->unsupported[1]|=1u<<30;event(p,MS_EVENT_UNSUPPORTED,op);if(!p->preview){off(p,i);return 0;}return 1;}
void ms_init(MsPlayer *p,MsRom r,const MsHooks *hooks){
    zero(p,sizeof(*p));p->rom.data=r.data;p->rom.base=r.base;p->rom.size=r.size;
    if(!r.data||r.size>0x2000000||r.base>UINT32_MAX-r.size)p->rom.size=0;
    if(hooks){p->hooks.user=hooks->user;p->hooks.start=hooks->start;p->hooks.update=hooks->update;p->hooks.stop=hooks->stop;
        p->hooks.done=hooks->done;p->hooks.note=hooks->note;p->hooks.event=hooks->event;}
    p->last_opcode=UINT32_MAX;p->mods.tempo=256;p->rng=0x4D535958;p->repeat=1;p->preview=1;
}
static int bind(MsPlayer *p,uint32_t root){
    if(!ms_root_valid(&p->rom,root))return 0;
    p->song.root=root;p->macros=relative(&p->rom,root,root,4);p->samples=relative(&p->rom,root,root+28,4);
    p->song_table=relative(&p->rom,root,root+24,4);p->song_count=(uint16_t)w(&p->rom,p->song_table);
    p->macro_count=(uint16_t)table_count(&p->rom,root,p->macros);p->sample_count=(uint16_t)table_count(&p->rom,root,p->samples);
    // The macros' code: from the first macro to the next section (the root's second pointer), or the
    // cart's end. Harry Potter CoS's macros GOTO into each other's tails (shared code).
    p->macro_lo=relative(&p->rom,root,p->macros,1);
    uint32_t section=relative(&p->rom,root,root+4,1);
    p->macro_hi=section>p->macro_lo?section:p->rom.base+p->rom.size;
    return 1;
}
static int load_entry(MsPlayer *p,MsTrack *t,uint32_t seq,uint32_t base_time,int immediate){
    const MsRom *r=&p->rom;if(!ok(r,seq,8))return fail(p,seq);
    int32_t delta=(int32_t)w(r,seq);if(delta<0||base_time>UINT32_MAX-(uint32_t)delta)return fail(p,seq);
    t->seq=seq;t->base_time=base_time;t->entry_time=immediate?p->ticks:base_time+(uint32_t)delta;return 1;
}
int ms_start(MsPlayer *p,const MsSong *requested){
    MsSong song;if(!requested||!ms_song_info(&p->rom,requested->root,requested->index,&song))return 0;
    const MsRom *r=&p->rom;uint32_t tracks=relative(r,song.song,song.song,68),patterns=relative(r,song.song,song.song+4,4);
    unsigned bpm=h(r,song.song+8);if(!tracks||!patterns||bpm<20||bpm>400)return 0;
    ms_stop(p);if(!bind(p,song.root))return 0;zero(p->tracks,sizeof(p->tracks));zero(p->voices,sizeof(p->voices));zero(p->unsupported,sizeof(p->unsupported));p->why=0;
    p->song.root=song.root;p->song.group=song.group;p->song.song=song.song;p->song.index=song.index;
    p->patterns=patterns;p->bpm=(uint16_t)bpm;p->ticks=0;p->tail=0;p->toggle=0;p->error=0;p->warnings=0;p->rejected=0;p->last_opcode=UINT32_MAX;p->running=1;
    // A song group opens with 16 bytes, one per MIDI channel: the channel's starting program (an index
    // into the group's sound list). Seen on Denki Blocks and BattleBots (2026-11-03): their songs send
    // no program changes, so starting every channel on program 0 played every part with one sound.
    for(unsigned c=0;c<16;++c)p->tracks[c].program=(uint8_t)(b(r,song.group+c)&127);
    // Loops are counted on the first track the song has (before 2026-11-06, track 0: a song without one
    // counted none, and its shape said it ended).
    p->lead=255;
    for(unsigned c=0;c<17;++c){MsTrack *t=&p->tracks[c];uint32_t a=relative(r,song.song,tracks+c*4,8);
        if(!a){t->ended=1;continue;}if(p->lead==255&&c<16)p->lead=(uint8_t)c;t->entries=a;if(!load_entry(p,t,a,0,0))return 0;}
    return 1;
}
uint32_t ms_tick_hz_q16(const MsPlayer *p){uint32_t tempo=p->mods.tempo>1024?1024:p->mods.tempo;return ((uint32_t)p->bpm*tempo*2048u)/5u;}// 96 PPQN
static uint32_t step_for(uint32_t rate,int pitch,unsigned root){
    static const uint32_t ratio[13]={65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715,131072};
    pitch=clip(pitch-(int)root*256+60*256,0,127*256);unsigned key=(unsigned)pitch>>8,oct=key/12,semi=key%12;
    uint32_t r=ratio[semi]+(((ratio[semi+1]-ratio[semi])*(pitch&255))>>8);
    uint32_t v=(uint32_t)(((uint64_t)rate*r)>>18);if(oct>=5)v<<=oct-5;else v>>=5-oct;
    return v<1?1:v>0x200000?0x200000:v;
}
// A PCM voice's renderer levels and step, the driver's own: the level's top byte + 1 times the volume
// x 2 + 1, then its equal-power pan (0.707 each side at the centre, 1 on one side at the edge). Its
// mixer adds sample x (level + 1) / 256 per voice and clips, as payload/mixer.c does at volShift 0.
// A fixed voice (STARTSAMPLE flag 1, a keymap entry's byte 6) plays at its sample's own rate,
// unpitched: the driver steps it 1:1 at its mixing rate, which is the samples' rate on the carts looked
// at (11397 Hz on Lady Sia and Beckham).
static void levels(MsPlayer *p,unsigned i){
    MsVoice *v=&p->voices[i];
    unsigned vol=v->volume?v->volume*2u+1u:0;vol=muted(p,v)?0:(vol*level_of(v))>>8;
    unsigned q=v->pan<64?v->pan:127u-v->pan,near=0xB538u+(((64u-q)*0x4AC8u)>>6),far=(q*0xB818u)>>6;
    unsigned a=(vol*near)>>16,c=(vol*far)>>16;if(a>255)a=255;if(c>255)c=255;
    v->pcm.left=(uint16_t)(v->pan<64?a:c);v->pcm.right=(uint16_t)(v->pan<64?c:a);
    v->pitch=pitch_of(v);
    v->pcm.step=step_for(v->sample_rate,(v->fixed?(int32_t)v->root_note*256:v->pitch)+p->mods.pitch,v->root_note);
}
static int valid_pcm(MsPlayer *p,MsPcm *v){return v->size&&v->size<=MS_MAX_SAMPLE&&ok(&p->rom,v->data,v->size+1)&&v->start<v->size&&
    (v->loop==MS_NOLOOP||v->loop<v->size)&&v->step&&v->step<=0x200000&&v->left<=255&&v->right<=255;}
static int sample_ended(MsPlayer *p,unsigned i){return i<MS_VOICES&&(!p->voices[i].has_sample||(p->hooks.done&&p->hooks.done(p->hooks.user,i)));}
static int start_sample(MsPlayer *p,unsigned i,unsigned id,uint32_t start,unsigned fixed){
    MsVoice *v=&p->voices[i];const MsRom *r=&p->rom;
    if(id>=p->sample_count){reject(p,i,MS_WHY_SAMPLE,p->samples);return 0;}
    uint32_t a=relative(r,p->song.root,p->samples+id*4,16);
    // On a Game Boy channel only the wave channel takes a sample: a waveform, flagged 1 at +11 (Harry
    // Potter CoS's 16- and 32-byte samples). 16 bytes are 32 4-bit steps (one bank of wave RAM), 32
    // bytes 64 steps (both banks): Lady Sia doubles a square's melody with a 32-byte wave written two
    // octaves above it, which the 64-step mode (two octaves below the squares for one register value)
    // puts in unison.
    if(i>=MS_VOICES){
        if(i==MS_VOICES+2&&a&&b(r,a+11)==1&&ok(r,a+16,16)){v->wave=a+16;v->wave_len=(uint8_t)(w(r,a)>=32&&ok(r,a+16,32)?32:16);}
        return 1;
    }
    if(!a||!sample_valid(r,a)){reject(p,i,MS_WHY_SAMPLE,a);return 0;}
    uint32_t size=w(r,a),data=a+16;int32_t loop=(int32_t)w(r,a+4);
    if(size>MS_MAX_SAMPLE){reject(p,i,MS_WHY_SAMPLE,a);return 0;}
    size-=(size*p->mods.sample_end)>>8;
    data+=(uint32_t)p->mods.scrub;if(!size||!ok(r,data,size+1)){reject(p,i,MS_WHY_SAMPLE,data);return 0;}
    v->pcm.data=data;v->pcm.size=size;v->pcm.loop=loop>=0&&(uint32_t)loop<size?(uint32_t)loop:MS_NOLOOP;
    uint32_t extra=(size*p->mods.sample_start)>>8;
    v->pcm.start=start>=size||extra>=size-start?size-1:start+extra;
    v->pcm.reverse=(uint8_t)(random8(p)<p->mods.reverse_chance);
    v->sample_rate=h(r,a+8);v->root_note=b(r,a+10);v->fixed=(uint8_t)(fixed!=0);levels(p,i);
    if(p->hooks.note)p->hooks.note(p->hooks.user,i,&v->pcm);
    if(!valid_pcm(p,&v->pcm)){reject(p,i,MS_WHY_SAMPLE,data);return 0;}
    v->has_sample=1;v->sounding=1;if(p->hooks.start)p->hooks.start(p->hooks.user,i,&v->pcm);return 1;
}
// Macro id's code (from +4), the TABLE OFFSET mangle applied; 0 if there is none. A keymap is no code.
static uint32_t macro_code(MsPlayer *p,unsigned id){
    const MsRom *r=&p->rom;uint32_t entry=p->macros+id*4u+(int32_t)p->mods.table_offset;
    if(id>=p->macro_count||!ok(r,entry,4))return 0;
    uint32_t a=relative(r,p->song.root,entry,5);return a&&!b(r,a+2)?a:0;
}
static int macro_select(MsPlayer *p,unsigned i,unsigned id,uint32_t a){
    MsVoice *v=&p->voices[i];const MsRom *r=&p->rom;
    uint32_t end=r->base+r->size;
    // Normal tables are ordered, but aliased/last macros can lack a reliable upper bound.
    if(id+1<p->macro_count){uint32_t next=relative(r,p->song.root,p->macros+(id+1)*4,1);if(next>a)end=next;}
    uint32_t section=relative(r,p->song.root,p->song.root+4,1);if(section>a&&section<end)end=section;
    v->macro_begin=a;v->macro_end=end;v->macro_id=(uint16_t)id;v->hw=b(r,a);v->ip=a+4;return 1;
}
// A PCM voice, as the driver picks one: the first that is free or whose macro has ended; else the
// lowest priority, the oldest of those; none when even that outranks the new note.
static int allocate(MsPlayer *p,unsigned prio){
    int pick=-1;unsigned best=256;uint32_t age=UINT32_MAX;
    for(unsigned i=0;i<MS_VOICES;++i){const MsVoice *v=&p->voices[i];
        if(!v->active||v->ended)return (int)i;
        if(v->prio<best||(v->prio==best&&v->age<age)){best=v->prio;age=v->age;pick=(int)i;}}
    return best>prio?-1:pick;
}
static void macro_run(MsPlayer *p,unsigned i);
// Macro id on a voice for track c. want: 0-3 a Game Boy channel (PLAYMACRO's), 254 a PCM voice, 255 by
// track: 0-3 take a channel (the macro header's when it names one, else the track's; header byte 1
// bit 0 alternates the two squares), 4-15 a PCM voice. The voice, or -1. Its first step runs once the
// tick's notes all have voices (ms_tick), as the driver allocates in its sequencer and runs macros
// after: a one-shot macro (STARTSAMPLE, END) ends at once and frees its voice, and a chord's next
// note would take it.
static int start_voice(MsPlayer *p,unsigned c,unsigned id,unsigned note,unsigned velocity,unsigned prio,uint32_t gate,unsigned want){
    const MsRom *r=&p->rom;uint32_t a=macro_code(p,id);int i;
    if(!a){++p->rejected;p->warnings|=MS_WARN_REJECTED_VOICE;p->why|=MS_WHY_MACRO;return -1;}
    unsigned hw=b(r,a),flags=b(r,a+1);
    if(want<4||(want==255&&c<4)){
        unsigned ch=want<4?want:hw<4?hw:c;
        if((flags&1)&&ch<2){ch=(ch+p->toggle)&1u;p->toggle^=1;}
        i=(int)(MS_VOICES+ch);const MsVoice *o=&p->voices[i];
        if(o->active&&!o->ended&&prio<o->prio)return -1;     // the channel is held by a higher priority
    } else if((i=allocate(p,prio))<0){p->warnings|=MS_WARN_VOICE_LIMIT;return -1;}
    else if(p->voices[i].active&&!p->voices[i].ended)p->warnings|=MS_WARN_VOICE_LIMIT;
    unsigned u=(unsigned)i;off(p,u);zero(&p->voices[u],sizeof(MsVoice));MsVoice *v=&p->voices[u];
    v->active=1;v->owner=(uint8_t)c;v->note=(uint8_t)note;v->velocity=v->cur=v->volume=(uint8_t)velocity;
    v->level=0xFF00;v->pan=64;v->prio=(uint8_t)prio;v->gate_left=gate;v->age=++p->age;v->noise=255;
    macro_select(p,u,id,a);v->fresh=1;
    return i;
}
static void note_on(MsPlayer *p,unsigned c,unsigned note,unsigned velocity,unsigned duration){
    MsTrack *t=&p->tracks[c];++t->notes;t->note=(uint8_t)note;t->activity=(uint8_t)(velocity*2);t->gate=1;
    if(random8(p)<p->mods.drop_chance)return;
    // The sound list: macro u16, priority, a byte the driver does not read.
    unsigned program=t->program&127;uint32_t list=p->song.group+16+(c==9?512:0)+program*4;
    int id=(int)h(&p->rom,list)+p->mods.voice_shift;
    if(id<0||id>=p->macro_count){++p->rejected;p->warnings|=MS_WARN_REJECTED_VOICE;p->why|=MS_WHY_PROGRAM;return;}
    // A note's length runs down on the tick; a zero length keys off on the next one (the driver's frame).
    start_voice(p,c,(unsigned)id,note,velocity,b(&p->rom,list+2),duration?duration:1,255);
    event(p,MS_EVENT_NOTE,((uint32_t)c<<24)|((uint32_t)note<<16)|duration);
}
// Key-off: the voice is marked (WAIT and op 6 see it from now on), an ADSR releases unless SETADSR
// said hold, and a trap (op 16) takes the macro there.
static void key_off(MsPlayer *p,unsigned i){
    MsVoice *v=&p->voices[i];if(!v->active)return;v->keyed=1;
    if(v->env==1&&!v->hold){v->rel=1;if(v->phase<2)v->phase=3;}
    if(v->keyoff&&!v->ended){v->ip=v->keyoff;v->wait=0;}
}
static void set_pan(MsVoice *v,int pan){v->pan=(uint8_t)clip(pan,0,127);}
// A jump (GOTO, the splits), possibly into another macro's code: Harry Potter CoS GOTOs into shared
// tails, and Spider-Man's macro 70 SPLITVELs into macro 114's velocity layers.
static void jump(MsPlayer *p,MsVoice *v,uint32_t to){
    if(p->macro_lo&&to>=p->macro_lo&&to<p->macro_hi&&(to<v->macro_begin||to>=v->macro_end)){v->macro_begin=p->macro_lo;v->macro_end=p->macro_hi;}
    v->ip=to;
}
// The macro until it waits, ends or runs 64 commands. Commands for the other kind of voice do nothing.
static void macro_run(MsPlayer *p,unsigned i){
    MsVoice *v=&p->voices[i];const MsRom *r=&p->rom;unsigned psg=i>=MS_VOICES,ch=psg?i-MS_VOICES:4;
    for(unsigned budget=0;budget<64;++budget){
        if(!v->active||!v->ip)return;
        uint32_t from=v->ip;if(from<v->macro_begin||from>=v->macro_end||!ok(r,from,1)){reject(p,i,MS_WHY_MACRO,from);return;}
        unsigned op=b(r,from);if(op>=54){unsupported(p,i,op);off(p,i);return;}
        unsigned length=ms_macro_lengths[op];if(length>v->macro_end-from||!ok(r,from,length)){reject(p,i,MS_WHY_MACRO,from);return;}
        unsigned b1=length>1?b(r,from+1):0;v->ip+=length;
        switch(op){
        case 0:case 1:                                                  // END (STOP is the same handler)
            v->ip=0;v->ended=1;v->prio=0;if(!(psg?v->sounding:v->has_sample))off(p,i);return;
        case 2:v->volume=(uint8_t)(b1>127?127:b1);break;                // SETVOLUME: absolute, velocity aside
        case 3:if(psg){v->sounding=1;p->psg[ch].trigger=1;}break;      // KEYON (a PCM sample starts at STARTSAMPLE)
        case 4:{                                                        // WAIT flags, frames (65535 for ever)
            // flag 1: on once key-off has happened; 4: on once the sample has ended; 2: a random time
            unsigned time=h(r,from+2),on=((b1&1)&&v->keyed)||((b1&4)&&sample_ended(p,i));
            if(!v->wait){if(!time||on)break;v->wait=(uint16_t)((b1&2)?1u+random_below(p,time):time);v->ip=from;return;}
            if(time!=65535&&!--v->wait)break;
            if(on){v->wait=0;break;}
            v->ip=from;return;}
        case 5:                                                         // STOP the sound; flag 1: once a fade is out
            if(b1&&v->env&&(psg?(level_of(v)*v->volume)>>11:(level_of(v)*v->volume)>>7)){v->ip=from;return;}
            silence(p,i);break;
        case 6:{                                                        // LOOP back by s16 bytes, n passes
            // as WAIT: flag 1 stops at key-off, 4 at the sample's end, 2 a random count; n 0 no loop
            unsigned n=h(r,from+2),on=((b1&1)&&v->keyed)||((b1&4)&&sample_ended(p,i));
            if(!v->repeat){if(!n||on)break;v->repeat=(uint16_t)((b1&2)?1u+random_below(p,n):n);}
            else{if(n!=65535&&!--v->repeat)break;if(on){v->repeat=0;break;}}
            v->ip=branch(from,(int16_t)h(r,from+4));break;}
        case 7:{int pan=(int)b1,spread=b(r,from+2);                    // PANNING, plus or minus a random spread
            if(spread){pan+=(int)random_below(p,spread*2u)-spread;}set_pan(v,pan);break;}
        case 8:if(ch==3)break;                                          // SETNOTE: an absolute pitch, Q8 semitones
            v->offset=(int16_t)(h(r,from+2)-v->note*256u);v->fixed=0;break;
        case 9:if(ch==3)break;{int16_t d=(int16_t)h(r,from+2);          // ADDNOTE: flag 1 from the note, 0 from here
            // Lady Sia's [01 00e8] is the note - 24 (read as an absolute pitch before 2026-11-05).
            v->offset=(int16_t)((b1&1)?d:v->offset+d);v->fixed=0;break;}
        case 10:if(ch==3)break;                                         // VIBRATO: frames a half-swing, step a frame
            v->vib_period=(uint8_t)b1;v->vib_count=(uint8_t)(b1>>1);v->vib[1]=(int16_t)h(r,from+2);v->fixed=0;break;
        case 11:if(ch==3)break;{int16_t *s=v->sweep[b1&1];              // PITCHSWEEP n: step a frame, wrap limit
            s[1]=(int16_t)h(r,from+2);s[2]=(int16_t)h(r,from+4);v->fixed=0;break;}
        case 12:if(ch==3)break;{int32_t x=(int16_t)h(r,from+2),delta;  // SLIDE by (flag 1: to) a pitch, over frames
            if(b1&1){v->slide[2]=(int16_t)(x-v->note*256);delta=x-((int32_t)v->note*256+v->slide[0]);}
            else{v->slide[2]=(int16_t)(v->slide[2]+x);delta=x;}
            v->slide[1]=(int16_t)divide(delta,h(r,from+4));v->fixed=0;break;}
        case 13:jump(p,v,branch(from,(int32_t)w(r,from+2)));break;     // GOTO
        case 14:{                                                       // SETADSR from the root's second table
            // {attack, decay, sustain level, release}, the rates per frame on a 0..0xFF00 level; flag: hold
            // (key-off leaves it). The level starts from 0.
            uint32_t tab=relative(r,p->song.root,p->song.root+4,4),entry=tab+h(r,from+2)*4u;
            uint32_t t=tab&&ok(r,entry,4)?relative(r,p->song.root,entry,8):0;if(!t)break;
            v->adsr=t;v->env=1;v->phase=0;v->hold=(uint8_t)(b1!=0);v->rel=0;v->level=0;break;}
        case 15:{uint32_t start=w(r,from+4);                            // STARTSAMPLE flags, sample, start
            // A start offset scales with the velocity (flag 0x80: shrinks); flag 1 plays it unpitched.
            if(start){unsigned vel=v->velocity?v->velocity+1u:0;uint32_t part=(start*vel)>>7;start=(b1&0x80)?start-part:part;}
            if(!start_sample(p,i,h(r,from+2),start,b1&1))return;
            break;}
        case 16:v->keyoff=branch(from,(int32_t)w(r,from+2));if(v->keyoff<v->macro_begin||v->keyoff>=v->macro_end){reject(p,i,MS_WHY_MACRO,v->keyoff);return;}break;
        case 17:v->keyoff=0;break;
        case 18:if(!psg&&v->has_sample){                                // SAMPLE RANGE flags, start, length
            // Plays [start, start + length) (0: to the end), looping over it if the sample loops or flag
            // 0x80; flags 0x7F: backwards from the end, from the start'th sample back.
            uint32_t s=w(r,from+2),n=w(r,from+6),size=v->pcm.size;
            if(s>=size)s=0;
            if(n>=size-s){if(s)--s;n=size-s;}
            if(!n)n=size-s;
            if(!(b1&0x7F)){unsigned loops=(b1&0x80)||v->pcm.loop!=MS_NOLOOP;v->pcm.start=s;v->pcm.size=s+n;v->pcm.loop=loops?s:MS_NOLOOP;v->pcm.reverse=0;}
            else{v->pcm.data+=size-s-n;v->pcm.size=n;v->pcm.start=0;v->pcm.loop=MS_NOLOOP;v->pcm.reverse=1;}
            if(!valid_pcm(p,&v->pcm)){reject(p,i,MS_WHY_SAMPLE,v->pcm.data);return;}
            if(p->hooks.start)p->hooks.start(p->hooks.user,i,&v->pcm);}
            break;
        case 21:{int32_t rate=h(r,from+2);                              // ENVELOPE: fade in (flag) or out, step a frame
            if(!v->env){v->level=(uint16_t)(b1?0:0xFF00);}v->env=2;v->fade=b1?rate:-rate;break;}
        case 22:if(ch==3)break;{                                        // RNDNOTE flags, low, high (Q8 semitones)
            // flag 1: any fraction, else whole semitones; flag 2: about the middle of the range, from the note
            int32_t lo=h(r,from+2),range=(int32_t)h(r,from+4)-lo,x;if(!range)break;
            if(b1&1)x=(int32_t)random_below(p,(unsigned)range&0xFFFFu);
            else{unsigned k=(unsigned)(range>>8)&0xFFFFu;if(!k)break;x=(int32_t)(random_below(p,k)<<8);}
            v->offset=(int16_t)((b1&2)?x-(lo+(range>>1)):lo+x-v->note*256);v->fixed=0;break;}
        case 23:case 24:case 25:{                                       // SPLITKEY / SPLITVEL / SPLITRND
            // jump when the note / velocity / a random 0..254 is at least the byte
            unsigned value=op==23?v->note:op==24?v->velocity:random_below(p,255);
            if(value>=b1){jump(p,v,branch(from,(int32_t)w(r,from+2)));}break;}
        case 29:{                                                       // KEYMAP macro, key offset: play its entry
            // Entry note + offset: {sample u16, transpose s16 (Q8), pan s8, -, unpitched flag, -}. The
            // macro carries on: a keymap was taken for a PLAYMACRO before 2026-11-06, which ended the macro
            // there (Lady Sia's strings lost their volume and their release fade). A note outside it: nothing.
            unsigned kid=h(r,from+2);uint32_t entry=p->macros+kid*4u;
            uint32_t m=kid<p->macro_count&&ok(r,entry,4)?relative(r,p->song.root,entry,6):0;
            if(!m||!b(r,m+2))break;
            int k=(int)v->note+(int16_t)h(r,from+4);
            if(k<0||k>=(int)h(r,m+4)){p->warnings|=MS_WARN_KEYMAP_RANGE;break;}
            uint32_t item=m+6+(uint32_t)k*8u;if(!ok(r,item,8)){reject(p,i,MS_WHY_MACRO,item);return;}
            unsigned fixed=b(r,item+6)!=0;
            if(psg){if(ch!=2)break;start_sample(p,i,h(r,item),0,0);if(!v->wave)break;}
            else if(!start_sample(p,i,h(r,item),0,fixed))return;
            set_pan(v,(int)v->pan+(int8_t)b(r,item+4));
            if(psg||!fixed)v->offset=(int16_t)(v->offset+(int16_t)h(r,item+2));
            break;}
        case 30:{                                                       // PLAYMACRO flags, key, priority, voice, macro
            // Another macro on another voice (255 any PCM; 0-3 that channel), at this note + key, with this
            // volume. It gets no key-off from the sequencer.
            unsigned want=b(r,from+5);if(++v->chains>8)break;
            int k=start_voice(p,v->owner,h(r,from+6),(v->note+(int8_t)b(r,from+3))&127u,v->cur,b(r,from+4),0,want<4?want:254);
            if(k==(int)i)return;                                        // it took this voice
            break;}
        case 31:case 38:case 40:case 41:case 42:case 43:case 52:break;  // nothing here: priority, age, game flags
        case 45:if(!psg&&h(r,from+4)){                                  // AUTOPAN flag, low, high, step a frame
            // from the pan now between low and high (x 256); flag 1 turns at the ends, else stops there
            v->autopan[0]=(int16_t)(v->pan<<8);v->autopan[1]=(int16_t)h(r,from+4);
            v->autopan_lo=b(r,from+2);v->autopan_hi=b(r,from+3);v->autopan_bounce=(uint8_t)(b1!=0);}
            break;
        case 32:if(ch<2)v->duty=(uint8_t)((b1==255?v->cur>>5:b1)&3u);break; // VOICE_ON: the square's duty (255 by volume)
        case 33:if(ch==3)v->noise=(uint8_t)b1;break;                    // SETNOISE: NR43
        case 34:if(b1==255||b1<4){unsigned t=b1==255?i:MS_VOICES+b1;    // KEYOFF a voice (0-3 a channel, 255 this)
            // the driver steps this command's 2 bytes after the key-off: a trap taken here lands 2 bytes in
            uint32_t was=p->voices[t].ip;key_off(p,t);if(t==i&&v->ip!=was)v->ip+=2;}
            break;
        case 36:if(ch<4&&ch!=2)v->henv=(uint8_t)(b1&15);break;          // HARDENVELOPE: NRx2's low nibble
        case 37:if(ch<4&&ch!=2){v->henv=0;p->psg[ch].trigger=1;}break;
        case 47:{unsigned base=b1?v->velocity:v->cur;                   // SCALEVOLUME: velocity or volume x scale + add
            v->cur=v->volume=(uint8_t)clip((int)((base*h(r,from+4))>>8)+(int8_t)b(r,from+2),0,127);break;}
        default:if(!unsupported(p,i,op))return;break; // preview skips only a known, checked byte length
        }
    }
    if(v->active&&v->ip){reject(p,i,MS_WHY_MACRO,v->ip);p->unsupported[1]|=1u<<31;}
}
// A frame's pitch modulators (op 10, 11, 12) and envelope (op 14, 21), as the driver's.
static void modulate(MsVoice *v){
    for(unsigned k=0;k<2;++k){int16_t *s=v->sweep[k];if(!s[1])continue;
        s[0]=(int16_t)(s[0]+s[1]);if(s[2]>0?s[0]>s[2]:s[0]<s[2])s[0]=(int16_t)(s[0]-s[2]);}
    if(v->vib[1]){v->vib[0]=(int16_t)(v->vib[0]+v->vib[1]);if(!v->vib_count--){v->vib_count=v->vib_period;v->vib[1]=(int16_t)-v->vib[1];}}
    if(v->slide[1]){v->slide[0]=(int16_t)(v->slide[0]+v->slide[1]);
        if(v->slide[1]>0?v->slide[0]>v->slide[2]:v->slide[0]<v->slide[2]){v->slide[0]=v->slide[2];v->slide[1]=0;}}
    if(v->autopan[1]){int32_t x=v->autopan[0]+v->autopan[1],lo=v->autopan_lo<<8,hi=v->autopan_hi<<8,edge=0;
        if(v->autopan[1]>0&&x>=hi){x=2*hi-x;edge=1;}else if(v->autopan[1]<0&&x<=lo){x=2*lo-x;edge=1;}
        if(edge)v->autopan[1]=(int16_t)(v->autopan_bounce?-v->autopan[1]:0);
        v->autopan[0]=(int16_t)x;set_pan(v,x>>8);}
}
static void envelope(MsPlayer *p,MsVoice *v){
    const MsRom *r=&p->rom;int32_t lv=v->level;
    if(v->env==1){
        // Attack to full, decay to the sustain level, held there until key-off, released to silence.
        // Key-off during the attack or decay goes straight to the release (key_off()).
        uint32_t t=v->adsr;if(!ok(r,t,8)){v->env=0;return;}
        if(v->phase==2&&v->rel)v->phase=3;
        if(v->phase==0){lv+=h(r,t);if(lv>0xFEFF){lv=0xFF00;v->phase=1;}}
        else if(v->phase==1){lv-=h(r,t+2);if(lv<=(int32_t)h(r,t+4)){lv=h(r,t+4);v->phase=2;}}
        else if(v->phase==3){lv-=h(r,t+6);if(lv<=0xFF){lv=0;v->env=0;v->phase=0;}}
    } else if(v->env==2){lv+=v->fade;if(v->fade>=0){if(lv>0xFEFF){lv=0xFF00;v->env=0;}}else if(lv<=0x100){lv=0;v->env=0;}}
    v->level=(uint16_t)lv;
}
static int process_track(MsPlayer *p,unsigned c){
    MsTrack *t=&p->tracks[c];const MsRom *r=&p->rom;unsigned budget=64;
    while(!t->ended){
        if(!budget--)return fail(p,t->seq);
        if(!t->pattern_active){
            if(p->ticks<t->entry_time)return 1;
            int32_t pattern=(int32_t)w(r,t->seq+4);
            if(pattern<0){
                if(pattern==-1){t->ended=1;return 1;}
                if(pattern!=-2||!ok(r,t->seq+8,8))return fail(p,t->seq);
                int32_t offset=(int32_t)w(r,t->seq+8),time=(int32_t)w(r,t->seq+12);
                if(offset<0||(offset&7)||(uint32_t)offset>=t->seq-t->entries)return fail(p,t->seq+8);
                if(time)p->warnings|=MS_WARN_LOOP_TIME;
                if(c==p->lead){++p->loops;event(p,MS_EVENT_LOOP,p->loops);}
                if(!load_entry(p,t,t->entries+(uint32_t)offset,p->ticks,1))return 0;
                continue;
            }
            if((uint32_t)pattern>=4096||!ok(r,p->patterns,(uint32_t)pattern*4+4))return fail(p,p->patterns);
            t->message=relative(r,p->song.song,p->patterns+(uint32_t)pattern*4,4);
            if(!t->message)return fail(p,p->patterns);
            t->message_time=t->entry_time;t->pattern_active=1;
        }
        uint32_t a=t->message;if(!ok(r,a,4))return fail(p,a);
        if(w(r,a)==UINT32_MAX){
            uint32_t base=t->entry_time;t->pattern_active=0;if(!load_entry(p,t,t->seq+8,base,0))return 0;continue;
        }
        unsigned delta=h(r,a);if(t->message_time>UINT32_MAX-delta)return fail(p,a);
        uint32_t due=t->message_time+delta;if(p->ticks<due)return 1;
        if(c==16){unsigned bpm=h(r,a+2);if(bpm<20||bpm>400)return fail(p,a);p->bpm=(uint16_t)bpm;t->message+=4;}
        else {
            unsigned velocity=b(r,a+2),note=b(r,a+3),patch=t->program;
            if(!velocity){t->program=(uint8_t)(note&127);t->message+=4;}
            else {
                unsigned length=(note&128)?8:6;if(!ok(r,a,length))return fail(p,a);
                if(note&128){patch=b(r,a+4);t->program=(uint8_t)(patch&127);}
                unsigned sustain=h(r,a+length-2);note_on(p,c,note&127,velocity>127?127:velocity,sustain);t->message+=length;
            }
        }
        t->message_time=due;
    }
    return 1;
}
// A PCM voice whose sample has played out lets it go (and the voice, once its macro is done); 1 if not.
static int reap(MsPlayer *p,unsigned i){MsVoice *v=&p->voices[i];
    if(p->hooks.done&&p->hooks.done(p->hooks.user,i)){v->has_sample=v->sounding=0;if(!v->ip)off(p,i);return 0;}
    return 1;
}
void ms_update(MsPlayer *p){
    for(unsigned i=0;i<MS_VOICES;++i){MsVoice *v=&p->voices[i];if(!v->active||!v->has_sample||!reap(p,i))continue;
        levels(p,i);if(p->hooks.update)p->hooks.update(p->hooks.user,i,&v->pcm);
    }
    psg_sync(p);
}
// One video frame: the macros, then each voice's modulators and envelope, then the levels. A PCM voice
// whose macro has ended is left as it was (the driver lets go of it).
void ms_frame(MsPlayer *p){
    if(!p->running)return;
    for(unsigned i=0;i<MS_ALL;++i)if(p->voices[i].active&&p->voices[i].ip){p->voices[i].fresh=0;macro_run(p,i);}
    for(unsigned i=0;i<MS_ALL;++i){MsVoice *v=&p->voices[i];if(!v->active||(i<MS_VOICES&&v->ended))continue;modulate(v);envelope(p,v);}
    ms_update(p);
}
// A voice the song's end waits for: its macro still runs, or it plays a sample that will end.
static int busy(const MsPlayer *p,unsigned i){const MsVoice *v=&p->voices[i];return v->active&&(v->ip||(i<MS_VOICES&&v->has_sample&&v->pcm.loop==MS_NOLOOP));}
ENG_IWRAM void ms_tick(MsPlayer *p){
    if(!p->running)return;
    for(unsigned i=0;i<MS_ALL;++i){MsVoice *v=&p->voices[i];if(v->active&&!v->keyed&&v->gate_left&&!--v->gate_left)key_off(p,i);}
    unsigned ended=0;for(unsigned c=0;c<17;++c){if(!process_track(p,c))return;ended+=p->tracks[c].ended;}
    unsigned started=0;
    for(unsigned i=0;i<MS_ALL;++i)if(p->voices[i].active&&p->voices[i].fresh){p->voices[i].fresh=0;macro_run(p,i);started=1;} // the notes' first steps
    // Levels and pitches change on the tick only when a note started (key-off moves an envelope, which
    // the frame runs): otherwise only let finished samples go. Recomputing every voice four times a
    // transport tick cost the GBA a fifth of its time on a busy song.
    if(started)ms_update(p);
    else for(unsigned i=0;i<MS_VOICES;++i)if(p->voices[i].active&&p->voices[i].has_sample)(void)reap(p,i);
    // Each track's gate: one pass over the voices (a pass per track was a third of the GBA's time).
    unsigned gates=0;for(unsigned i=0;i<MS_ALL;++i){const MsVoice *v=&p->voices[i];if(v->active&&!v->keyed&&v->owner<16)gates|=1u<<v->owner;}
    for(unsigned c=0;c<16;++c){MsTrack *t=&p->tracks[c];
        t->gate=(uint8_t)((gates>>c)&1u);if(t->activity)t->activity-=((t->activity>>3)+1);}
    ++p->ticks;
    // Every track has ended: the song ends when its voices are done, or four beats on (a voice no
    // key-off reaches, PLAYMACRO's, could hold it for ever).
    if(ended==17){unsigned active=0;for(unsigned i=0;i<MS_ALL;++i)active+=(unsigned)busy(p,i);
        if(!active||++p->tail>384){if(p->repeat){MsSong song;song.root=p->song.root;song.song=p->song.song;song.group=p->song.group;song.index=p->song.index;if(ms_start(p,&song)){++p->loops;event(p,MS_EVENT_LOOP,p->loops);}}else ms_stop(p);}}
}
int ms_audition_sample(MsPlayer *p,uint32_t root,unsigned sample){
    if(!ms_root_valid(&p->rom,root))return 0;
    uint32_t samples=relative(&p->rom,root,root+28,4);
    if(sample>=table_count(&p->rom,root,samples))return 0;
    ms_stop(p);if(!bind(p,root))return 0;zero(&p->voices[0],sizeof(MsVoice));MsVoice *v=&p->voices[0];v->active=1;v->owner=0;v->velocity=v->cur=v->volume=100;v->level=0xFF00;v->pan=64;
    uint32_t a=relative(&p->rom,root,p->samples+sample*4,16);if(!a){off(p,0);return 0;}v->note=b(&p->rom,a+10)&127;
    return start_sample(p,0,sample,0,0);
}
