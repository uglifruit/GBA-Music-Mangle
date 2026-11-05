// Bounded GBA MusyX parser, sequencer and deliberately limited SMaL interpreter.
// Layout reference: MIT BinarySerializer.Audio.GBA MusyX (ed612685...); no original driver calls.
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
// track's last pattern, and whether any track loops (a -2 entry). Bounded; 0 if unreadable.
int ms_song_shape(const MsRom *r,uint32_t root,unsigned index,uint32_t *ticks,int *loops){
    MsSong s;*ticks=0;*loops=0;if(!ms_song_info(r,root,index,&s))return 0;
    uint32_t tracks=relative(r,s.song,s.song,68),patterns=relative(r,s.song,s.song+4,4);if(!tracks||!patterns)return 0;
    unsigned budget=20000;
    for(unsigned c=0;c<16;++c){
        uint32_t a=relative(r,s.song,tracks+c*4,8);if(!a)continue;
        uint32_t time=0;
        for(unsigned e=0;e<512&&ok(r,a,8)&&budget;++e,a+=8,--budget){
            int32_t delta=(int32_t)w(r,a),pattern=(int32_t)w(r,a+4);
            if(delta<0)break;
            time+=(uint32_t)delta;
            if(pattern==-1)break;
            if(pattern==-2){if(c==0)*loops=1;break;}
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
static void event(MsPlayer *p,unsigned kind,uint32_t value){if(p->hooks.event)p->hooks.event(p->hooks.user,kind,value);}
static unsigned random8(MsPlayer *p){p->rng^=p->rng<<13;p->rng^=p->rng>>17;p->rng^=p->rng<<5;return p->rng&255;}
static void psg_drop(MsPlayer *p,unsigned i){
    MsVoice *v=&p->voices[i];if(!v->psg)return;
    unsigned ch=v->psg-1u;if(p->psg_owner[ch]==i+1u)p->psg_owner[ch]=0;v->psg=0;
}
static void off(MsPlayer *p,unsigned i){psg_drop(p,i);p->voices[i].active=p->voices[i].has_sample=0;if(p->hooks.stop)p->hooks.stop(p->hooks.user,i);}
// PSG (see musyx.h). Which channel a macro's PSG command means: its header voice when that is 0..3;
// for "any" (255) the track's when the track is 0..3 (the Game Boy MusyX manual: "MIDI channels 1-4
// map to voices 1-4"; Harry Potter CoS's PSG parts sit exactly there); otherwise noise for SETNOISE
// and a square by the track's parity (Lady Sia has a few such on tracks 4 and 5).
static unsigned psg_channel(const MsVoice *v,unsigned noise){
    if(v->hw<4)return v->hw;
    if(v->owner<4)return v->owner;
    return noise?3u:(v->owner&1u);
}
static void psg_claim(MsPlayer *p,unsigned i,unsigned ch){
    unsigned o=p->psg_owner[ch];
    if(o&&o!=i+1u&&p->voices[o-1].psg==ch+1u)p->voices[o-1].psg=0;
    p->psg_owner[ch]=(uint8_t)(i+1u);p->voices[i].psg=(uint8_t)(ch+1u);p->psg[ch].trigger=1;
}
// The channels from the voices that hold them; called with every update.
static void psg_sync(MsPlayer *p){
    for(unsigned ch=0;ch<MS_PSG;++ch){
        MsPsg *s=&p->psg[ch];unsigned o=p->psg_owner[ch];
        const MsVoice *v=o?&p->voices[o-1]:0;
        if(!v||!v->active||v->psg!=ch+1u){s->on=0;continue;}
        s->on=1;s->track=v->owner;s->pitch=v->pitch+p->mods.pitch;
        s->volume=(v->owner<16&&(p->mods.mute&(1u<<v->owner)))?0:v->volume;
        s->duty=v->duty;s->env=v->env;s->noise=v->noise;s->wave=v->wave;
    }
}
void ms_stop(MsPlayer *p){for(unsigned i=0;i<MS_VOICES;++i)off(p,i);for(unsigned ch=0;ch<MS_PSG;++ch){p->psg[ch].on=0;p->psg_owner[ch]=0;}for(unsigned i=0;i<16;++i)p->tracks[i].gate=0;p->running=0;event(p,MS_EVENT_STOP,0);}
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
// KEYMAPS (macro header byte 2 = 1): a u16 count, then entries {sample u16, transpose s16 in 1/256
// semitone, 4 bytes}. Every keymap in ten games' dumps (2026-11-03) is reached through PLAYMACRO,
// whose key offset is how far down the map starts: -36 in most, -24 in BattleBots and Spider-Man, and
// per macro in Lady Sia (-36 -46 -56 -59), each fitting every note its songs send. So a note plays
// entry note + key_add, at note + transpose: drum kits are written with the transpose falling one
// per entry, which puts every hit on its sample's own key.
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
    p->patterns=patterns;p->bpm=(uint16_t)bpm;p->ticks=0;p->error=0;p->warnings=0;p->rejected=0;p->last_opcode=UINT32_MAX;p->running=1;
    // A song group opens with 16 bytes, one per MIDI channel: the channel's starting program (an index
    // into the group's sound list). Seen on Denki Blocks and BattleBots (2026-11-03): their songs send
    // no program changes, so starting every channel on program 0 played every part with one sound.
    for(unsigned c=0;c<16;++c)p->tracks[c].program=(uint8_t)(b(r,song.group+c)&127);
    for(unsigned c=0;c<17;++c){MsTrack *t=&p->tracks[c];uint32_t a=relative(r,song.song,tracks+c*4,8);
        if(!a){t->ended=1;continue;}t->entries=a;if(!load_entry(p,t,a,0,0))return 0;}
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
static void levels(MsPlayer *p,unsigned i){
    MsVoice *v=&p->voices[i];unsigned volume=v->volume;
    if(v->owner<16&&(p->mods.mute&(1u<<v->owner)))volume=0;
    v->pcm.left=v->pcm.right=(uint16_t)(volume*2u>255?255:volume*2u);
    v->pcm.step=step_for(v->sample_rate,v->pitch+p->mods.pitch,v->root_note);
}
static int valid_pcm(MsPlayer *p,MsPcm *v){return v->size&&v->size<=MS_MAX_SAMPLE&&ok(&p->rom,v->data,v->size+1)&&v->start<v->size&&
    (v->loop==MS_NOLOOP||v->loop<v->size)&&v->step&&v->step<=0x200000&&v->left<=255&&v->right<=255;}
static int start_sample(MsPlayer *p,unsigned i,unsigned id,uint32_t start){
    MsVoice *v=&p->voices[i];const MsRom *r=&p->rom;
    if(id>=p->sample_count){reject(p,i,MS_WHY_SAMPLE,p->samples);return 0;}
    uint32_t a=relative(r,p->song.root,p->samples+id*4,16);
    // A waveform for the wave channel: started on voice 2, or flagged at +11 (Harry Potter CoS's 16- and
    // 32-byte samples). Its first 16 bytes are the 32 4-bit steps the channel loops.
    if(a&&(v->hw==2||b(r,a+11)==1)&&ok(r,a+16,16)){v->wave=a+16;psg_claim(p,i,2);return 1;}
    if(!a||!sample_valid(r,a)){reject(p,i,MS_WHY_SAMPLE,a);return 0;}
    uint32_t size=w(r,a),data=a+16;int32_t loop=(int32_t)w(r,a+4);
    if(size>MS_MAX_SAMPLE){reject(p,i,MS_WHY_SAMPLE,a);return 0;}
    size-=(size*p->mods.sample_end)>>8;
    data+=(uint32_t)p->mods.scrub;if(!size||!ok(r,data,size+1)){reject(p,i,MS_WHY_SAMPLE,data);return 0;}
    v->pcm.data=data;v->pcm.size=size;v->pcm.loop=loop>=0&&(uint32_t)loop<size?(uint32_t)loop:MS_NOLOOP;
    uint32_t extra=(size*p->mods.sample_start)>>8;
    v->pcm.start=start>=size||extra>=size-start?size-1:start+extra;
    v->pcm.reverse=(uint8_t)(random8(p)<p->mods.reverse_chance);
    v->sample_rate=h(r,a+8);v->root_note=b(r,a+10);levels(p,i);
    if(p->hooks.note)p->hooks.note(p->hooks.user,i,&v->pcm);
    if(!valid_pcm(p,&v->pcm)){reject(p,i,MS_WHY_SAMPLE,data);return 0;}
    v->has_sample=1;if(p->hooks.start)p->hooks.start(p->hooks.user,i,&v->pcm);return 1;
}
static int macro_select(MsPlayer *p,unsigned i,unsigned id){
    MsVoice *v=&p->voices[i];const MsRom *r=&p->rom;
    uint32_t entry=p->macros+id*4u+(int32_t)p->mods.table_offset;
    if(id>=p->macro_count||!ok(r,entry,4)){reject(p,i,MS_WHY_MACRO,entry);return 0;}
    uint32_t a=relative(r,p->song.root,entry,5);
    if(!a){reject(p,i,MS_WHY_MACRO,entry);return 0;}
    uint32_t end=r->base+r->size;
    // Normal tables are ordered, but aliased/last macros can lack a reliable upper bound.
    if(id+1<p->macro_count){uint32_t next=relative(r,p->song.root,p->macros+(id+1)*4,1);if(next>a)end=next;}
    uint32_t section=relative(r,p->song.root,p->song.root+4,1);if(section>a&&section<end)end=section;
    v->macro_begin=a;v->macro_end=end;v->macro_id=(uint16_t)id;v->keyoff=0;v->wait=0;v->hw=b(r,a);
    if(b(r,a+2)){
        // A keymap (see KEYMAPS above): entry note + key_add, played at note + transpose. The pan and
        // the other trailing bytes are ignored (bit 54 still says "approximate"). A note outside the
        // map plays its nearest entry (warned).
        // An entry with byte 6 set and transpose 0 plays its sample at the sample's own key: Lady Sia's,
        // Go! Go! Beckham's and Ace Lightning's kits are all such (a sample per key, nothing to transpose),
        // and at note + 0 they sounded an octave or two low (Andy, on hardware, 2026-11-05). With a
        // transpose (BattleBots, Spider-Man) byte 6 changes nothing: note + transpose is their pitch.
        if(!p->preview||!ok(r,a+4,2)){unsupported(p,i,54);off(p,i);return 0;}
        if(!unsupported(p,i,54))return 0;
        unsigned count=h(r,a+4);int k=(int)v->note+v->key_add;
        if(!count){reject(p,i,MS_WHY_MACRO,a);return 0;}
        if(k<0||k>=(int)count){k=k<0?0:(int)count-1;p->warnings|=MS_WARN_KEYMAP_RANGE;}
        uint32_t item=a+6+(uint32_t)k*8u;
        if(!ok(r,item,8)){reject(p,i,MS_WHY_MACRO,item);return 0;}
        int t=(int16_t)h(r,item+2);unsigned sid=h(r,item);
        uint32_t sa=sid<p->sample_count?relative(r,p->song.root,p->samples+sid*4,16):0;
        v->pitch=(b(r,item+6)&&!t&&sa)?(int32_t)b(r,sa+10)*256:(int32_t)v->note*256+t;
        v->ip=0;v->tail=1;return start_sample(p,i,sid,0);
    }
    v->ip=a+4;return 1;
}
static void release(MsPlayer *p,unsigned i){
    MsVoice *v=&p->voices[i];v->released=1;
    if(v->keyoff>=v->macro_begin&&v->keyoff<v->macro_end){v->ip=v->keyoff;v->keyoff=0;v->wait=0;v->tail=0;}
    else off(p,i);
}
static void note_on(MsPlayer *p,unsigned c,unsigned note,unsigned velocity,unsigned duration){
    MsTrack *t=&p->tracks[c];++t->notes;t->note=(uint8_t)note;t->activity=(uint8_t)(velocity*2);t->gate=1;
    if(random8(p)<p->mods.drop_chance)return;
    unsigned program=t->program&127;uint32_t list=p->song.group+16+(c==9?512:0)+program*4;
    int id=(int)h(&p->rom,list)+p->mods.voice_shift;
    if(!b(&p->rom,list+3)||id<0||id>=p->macro_count){++p->rejected;p->warnings|=MS_WARN_REJECTED_VOICE;
        p->why|=(uint8_t)(!b(&p->rom,list+3)?MS_WHY_NO_VOICES:MS_WHY_PROGRAM);return;}
    int choice=-1;uint32_t age=UINT32_MAX;
    for(unsigned i=0;i<MS_VOICES;++i){
        if(p->voices[i].active&&p->voices[i].has_sample&&p->hooks.done&&p->hooks.done(p->hooks.user,i))off(p,i);
        if(!p->voices[i].active){choice=(int)i;break;}
        if(p->voices[i].age<age){age=p->voices[i].age;choice=(int)i;}
    }
    if(choice<0)return;
    unsigned i=(unsigned)choice;if(p->voices[i].active)p->warnings|=MS_WARN_VOICE_LIMIT;
    off(p,i);zero(&p->voices[i],sizeof(MsVoice));MsVoice *v=&p->voices[i];
    v->active=1;v->owner=(uint8_t)c;v->note=(uint8_t)note;v->velocity=v->volume=(uint8_t)velocity;
    v->pitch=(int32_t)note*256;v->gate_left=duration;v->age=++p->age;v->noise=255;
    macro_select(p,i,(unsigned)id);event(p,MS_EVENT_NOTE,((uint32_t)c<<24)|((uint32_t)note<<16)|duration);
}
static void macro_run(MsPlayer *p,unsigned i){
    MsVoice *v=&p->voices[i];const MsRom *r=&p->rom;
    if(v->tail||!v->ip)return;
    if(v->wait){if(v->wait==65535)return;--v->wait;if(v->wait)return;}
    for(unsigned budget=0;budget<64&&v->active;++budget){
        uint32_t from=v->ip;if(from<v->macro_begin||from>=v->macro_end||!ok(r,from,1)){reject(p,i,MS_WHY_MACRO,from);return;}
        unsigned op=b(r,from);if(op>=54){unsupported(p,i,op);off(p,i);return;}
        unsigned length=ms_macro_lengths[op];if(length>v->macro_end-from||!ok(r,from,length)){reject(p,i,MS_WHY_MACRO,from);return;}
        v->ip+=length;
        switch(op){
        case 0:case 1:
            if(p->preview&&(v->has_sample||v->psg)&&!v->released){v->tail=1;p->warnings|=MS_WARN_PREVIEW_TAIL;}else off(p,i);
            return;
        case 2:v->volume=(uint8_t)(b(r,from+1)&127);break;           // SETVOLUME (Denki's echoes step it 127..70)
        case 6:{                                                        // LOOP n times back to an offset
            // flags byte, times, a byte, s16 offset from this command: Denki's arpeggios repeat this way
            unsigned times=b(r,from+2);int32_t off=(int16_t)h(r,from+4);
            if(v->loop_at!=from){v->loop_at=from;v->loop_left=(uint8_t)times;}
            if(!times||v->loop_left){if(times)--v->loop_left;v->ip=branch(from,off);}else v->loop_at=0;
            break;}
        case 8:{                                                        // SETNOTE: absolute key, detune (cents)
            int detune=(int8_t)b(r,from+2);v->pitch=(int32_t)b(r,from+3)*256+((detune*655)>>8);break;}
        case 4:
            if(b(r,from+1)&&!unsupported(p,i,op))return;
            v->wait=h(r,from+2);if(v->wait)return;break;
        case 9:{unsigned flags=b(r,from+1);int note=(int16_t)h(r,from+2);v->pitch=(flags&1)?note:(int32_t)v->note*256+note;break;}
        case 13:{uint32_t to=branch(from,(int32_t)w(r,from+2));    // GOTO, possibly into another macro
            if(p->macro_lo&&to>=p->macro_lo&&to<p->macro_hi&&(to<v->macro_begin||to>=v->macro_end)){v->macro_begin=p->macro_lo;v->macro_end=p->macro_hi;}
            v->ip=to;break;}
        case 15:
            if(b(r,from+1)&&!unsupported(p,i,op))return;
            if(!start_sample(p,i,h(r,from+2),w(r,from+4)))return;
            break;
        case 16:v->keyoff=branch(from,(int32_t)w(r,from+2));if(v->keyoff<v->macro_begin||v->keyoff>=v->macro_end){reject(p,i,MS_WHY_MACRO,v->keyoff);return;}break;
        case 17:v->keyoff=0;break;
        case 23:case 24:case 25:{unsigned threshold=b(r,from+1),value=op==23?v->note:op==24?v->velocity:random8(p);
            if(threshold>=value)v->ip=branch(from,(int32_t)w(r,from+2));
            break;}
        case 29:
            if(!unsupported(p,i,op))return;
            if(++v->chains>4){reject(p,i,MS_WHY_MACRO,from);return;}
            // PLAYMACRO macro u16, key offset s16 (semitones). Into a keymap the offset picks the entry;
            // into a macro it moves the pitch. A keymap starts its sample and ends the macro (tail), so
            // stop here: running on from ip 0 rejected every voice that took this road.
            p->warnings|=MS_WARN_PREVIEW_CHAIN;
            {int add=(int16_t)h(r,from+4);v->key_add=(int16_t)clip(v->key_add+add,-128,127);
             if(!macro_select(p,i,h(r,from+2)))return;
             if(v->tail||!v->ip)return;
             v->pitch=clip(v->pitch+add*256,-32768,32767);}
            break;
        case 31:break; // documented no-op on GBA
        case 32:{unsigned ch=psg_channel(v,0),d=b(r,from+1);           // VOICE_ON: duty 0..3, 255 by velocity
            v->duty=(uint8_t)(d==255?v->velocity>>5:d&3);psg_claim(p,i,ch);break;}
        case 33:v->noise=b(r,from+1);psg_claim(p,i,3);break;           // SETNOISE: the noise channel's NR43
        case 36:v->env=(uint8_t)(b(r,from+1)&15);if(v->psg)p->psg[v->psg-1].trigger=1;break; // HARDENVELOPE
        case 37:v->env=0;if(v->psg)p->psg[v->psg-1].trigger=1;break;  // HARDENVELOPE_OFF
        case 34:if(b(r,from+1)==255){release(p,i);return;}if(!unsupported(p,i,op))return;break;
        case 47:{unsigned base=b(r,from+1)?v->velocity:v->volume;v->volume=(uint8_t)clip((int)((base*h(r,from+4))>>8)+b(r,from+2),0,127);break;}
        default:if(!unsupported(p,i,op))return;break; // preview skips only a known, checked byte length
        }
    }
    if(v->active){reject(p,i,MS_WHY_MACRO,v->ip);p->unsupported[1]|=1u<<31;}
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
                if(c==0){++p->loops;event(p,MS_EVENT_LOOP,p->loops);}
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
void ms_update(MsPlayer *p){
    for(unsigned i=0;i<MS_VOICES;++i){MsVoice *v=&p->voices[i];if(!v->active||!v->has_sample)continue;
        if(p->hooks.done&&p->hooks.done(p->hooks.user,i)){off(p,i);continue;}
        levels(p,i);if(p->hooks.update)p->hooks.update(p->hooks.user,i,&v->pcm);
    }
    psg_sync(p);
}
void ms_tick(MsPlayer *p){
    if(!p->running)return;
    for(unsigned i=0;i<MS_VOICES;++i){MsVoice *v=&p->voices[i];if(v->active&&!v->released&&v->gate_left&&!--v->gate_left)release(p,i);}
    unsigned ended=0;for(unsigned c=0;c<17;++c){if(!process_track(p,c))return;ended+=p->tracks[c].ended;}
    for(unsigned i=0;i<MS_VOICES;++i)if(p->voices[i].active)macro_run(p,i);
    ms_update(p);
    for(unsigned c=0;c<16;++c){MsTrack *t=&p->tracks[c];unsigned gate=0;for(unsigned i=0;i<MS_VOICES;++i)if(p->voices[i].active&&!p->voices[i].released&&p->voices[i].owner==c)gate=1;
        t->gate=(uint8_t)gate;if(t->activity)t->activity-=((t->activity>>3)+1);}
    ++p->ticks;
    if(ended==17){unsigned active=0;for(unsigned i=0;i<MS_VOICES;++i)active+=p->voices[i].active;
        if(!active){if(p->repeat){MsSong song;song.root=p->song.root;song.song=p->song.song;song.group=p->song.group;song.index=p->song.index;if(ms_start(p,&song)){++p->loops;event(p,MS_EVENT_LOOP,p->loops);}}else ms_stop(p);}}
}
int ms_audition_sample(MsPlayer *p,uint32_t root,unsigned sample){
    if(!ms_root_valid(&p->rom,root))return 0;
    uint32_t samples=relative(&p->rom,root,root+28,4);
    if(sample>=table_count(&p->rom,root,samples))return 0;
    ms_stop(p);if(!bind(p,root))return 0;zero(&p->voices[0],sizeof(MsVoice));MsVoice *v=&p->voices[0];v->active=1;v->owner=0;v->velocity=v->volume=100;
    uint32_t a=relative(&p->rom,root,p->samples+sample*4,16);if(!a){off(p,0);return 0;}v->pitch=(int32_t)b(&p->rom,a+10)*256;
    return start_sample(p,0,sample,0);
}
