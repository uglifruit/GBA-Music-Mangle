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
static int sample_valid(const MsRom *r,uint32_t a){
    if(!ok(r,a,16))return 0;
    uint32_t size=w(r,a);int32_t loop=(int32_t)w(r,a+4);
    return size&&size<0x1000000&&ok(r,a+16,size)&&(loop<0||(uint32_t)loop<=size)&&h(r,a+8)>=1000&&h(r,a+8)<=48000&&h(r,a+10)<=127;
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
static void event(MsPlayer *p,unsigned kind,uint32_t value){if(p->hooks.event)p->hooks.event(p->hooks.user,kind,value);}
static unsigned random8(MsPlayer *p){p->rng^=p->rng<<13;p->rng^=p->rng>>17;p->rng^=p->rng<<5;return p->rng&255;}
static void off(MsPlayer *p,unsigned i){p->voices[i].active=p->voices[i].has_sample=0;if(p->hooks.stop)p->hooks.stop(p->hooks.user,i);}
void ms_stop(MsPlayer *p){for(unsigned i=0;i<MS_VOICES;++i)off(p,i);for(unsigned i=0;i<16;++i)p->tracks[i].gate=0;p->running=0;event(p,MS_EVENT_STOP,0);}
static int fail(MsPlayer *p,uint32_t address){p->error=1;ms_stop(p);event(p,MS_EVENT_BAD_DATA,address);return 0;}
static void reject(MsPlayer *p,unsigned i,uint32_t address){++p->rejected;p->warnings|=MS_WARN_REJECTED_VOICE;off(p,i);event(p,MS_EVENT_BAD_DATA,address);}
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
    p->macro_count=(uint16_t)table_count(&p->rom,root,p->macros);p->sample_count=(uint16_t)table_count(&p->rom,root,p->samples);return 1;
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
    ms_stop(p);if(!bind(p,song.root))return 0;zero(p->tracks,sizeof(p->tracks));zero(p->voices,sizeof(p->voices));zero(p->unsupported,sizeof(p->unsupported));
    p->song.root=song.root;p->song.group=song.group;p->song.song=song.song;p->song.index=song.index;
    p->patterns=patterns;p->bpm=(uint16_t)bpm;p->ticks=0;p->error=0;p->warnings=0;p->rejected=0;p->last_opcode=UINT32_MAX;p->running=1;
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
    if(id>=p->sample_count){reject(p,i,p->samples);return 0;}
    uint32_t a=relative(r,p->song.root,p->samples+id*4,16);
    if(!a||!sample_valid(r,a)){reject(p,i,a);return 0;}
    uint32_t size=w(r,a),data=a+16;int32_t loop=(int32_t)w(r,a+4);
    if(size>MS_MAX_SAMPLE){reject(p,i,a);return 0;}
    size-=(size*p->mods.sample_end)>>8;
    data+=(uint32_t)p->mods.scrub;if(!size||!ok(r,data,size+1)){reject(p,i,data);return 0;}
    v->pcm.data=data;v->pcm.size=size;v->pcm.loop=loop>=0&&(uint32_t)loop<size?(uint32_t)loop:MS_NOLOOP;
    uint32_t extra=(size*p->mods.sample_start)>>8;
    v->pcm.start=start>=size||extra>=size-start?size-1:start+extra;
    v->pcm.reverse=(uint8_t)(random8(p)<p->mods.reverse_chance);
    v->sample_rate=h(r,a+8);v->root_note=h(r,a+10);levels(p,i);
    if(p->hooks.note)p->hooks.note(p->hooks.user,i,&v->pcm);
    if(!valid_pcm(p,&v->pcm)){reject(p,i,data);return 0;}
    v->has_sample=1;if(p->hooks.start)p->hooks.start(p->hooks.user,i,&v->pcm);return 1;
}
static int macro_select(MsPlayer *p,unsigned i,unsigned id){
    MsVoice *v=&p->voices[i];const MsRom *r=&p->rom;
    uint32_t entry=p->macros+id*4u+(int32_t)p->mods.table_offset;
    if(id>=p->macro_count||!ok(r,entry,4)){reject(p,i,entry);return 0;}
    uint32_t a=relative(r,p->song.root,entry,5);
    if(!a){reject(p,i,entry);return 0;}
    uint32_t end=r->base+r->size;
    // Normal tables are ordered, but aliased/last macros can lack a reliable upper bound.
    if(id+1<p->macro_count){uint32_t next=relative(r,p->song.root,p->macros+(id+1)*4,1);if(next>a)end=next;}
    uint32_t section=relative(r,p->song.root,p->song.root+4,1);if(section>a&&section<end)end=section;
    v->macro_begin=a;v->macro_end=end;v->macro_id=(uint16_t)id;v->keyoff=0;v->wait=0;
    if(b(r,a+2)){
        // GBA keymaps contain sample index and Q8 pitch per key; unknown trailing fields ignored in preview.
        if(!p->preview||!ok(r,a+4,2)){unsupported(p,i,54);off(p,i);return 0;}
        if(!unsupported(p,i,54))return 0;
        unsigned count=h(r,a+4);uint32_t item=a+6+v->note*8u;
        if(v->note>=count||!ok(r,item,8)){reject(p,i,item);return 0;}
        v->pitch=(int16_t)h(r,item+2);v->ip=0;v->tail=1;return start_sample(p,i,h(r,item),0);
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
    if(!b(&p->rom,list+3)||id<0||id>=p->macro_count){++p->rejected;p->warnings|=MS_WARN_REJECTED_VOICE;return;}
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
    v->pitch=(int32_t)note*256;v->gate_left=duration;v->age=++p->age;
    macro_select(p,i,(unsigned)id);event(p,MS_EVENT_NOTE,((uint32_t)c<<24)|((uint32_t)note<<16)|duration);
}
static void macro_run(MsPlayer *p,unsigned i){
    MsVoice *v=&p->voices[i];const MsRom *r=&p->rom;
    if(v->tail||!v->ip)return;
    if(v->wait){if(v->wait==65535)return;--v->wait;if(v->wait)return;}
    for(unsigned budget=0;budget<64&&v->active;++budget){
        uint32_t from=v->ip;if(from<v->macro_begin||from>=v->macro_end||!ok(r,from,1)){reject(p,i,from);return;}
        unsigned op=b(r,from);if(op>=54){unsupported(p,i,op);off(p,i);return;}
        unsigned length=ms_macro_lengths[op];if(length>v->macro_end-from||!ok(r,from,length)){reject(p,i,from);return;}
        v->ip+=length;
        switch(op){
        case 0:case 1:
            if(p->preview&&v->has_sample&&!v->released){v->tail=1;p->warnings|=MS_WARN_PREVIEW_TAIL;}else off(p,i);
            return;
        case 4:
            if(b(r,from+1)&&!unsupported(p,i,op))return;
            v->wait=h(r,from+2);if(v->wait)return;break;
        case 9:{unsigned flags=b(r,from+1);int note=(int16_t)h(r,from+2);v->pitch=(flags&1)?note:(int32_t)v->note*256+note;break;}
        case 13:v->ip=branch(from,(int32_t)w(r,from+2));break;
        case 15:
            if(b(r,from+1)&&!unsupported(p,i,op))return;
            if(!start_sample(p,i,h(r,from+2),w(r,from+4)))return;
            break;
        case 16:v->keyoff=branch(from,(int32_t)w(r,from+2));if(v->keyoff<v->macro_begin||v->keyoff>=v->macro_end){reject(p,i,v->keyoff);return;}break;
        case 17:v->keyoff=0;break;
        case 23:case 24:case 25:{unsigned threshold=b(r,from+1),value=op==23?v->note:op==24?v->velocity:random8(p);
            if(threshold>=value)v->ip=branch(from,(int32_t)w(r,from+2));
            break;}
        case 29:
            if(!unsupported(p,i,op))return;
            if(++v->chains>4){reject(p,i,from);return;}
            p->warnings|=MS_WARN_PREVIEW_CHAIN;v->pitch=clip(v->pitch+(int16_t)h(r,from+4)-60*256,-32768,32767);
            if(!macro_select(p,i,h(r,from+2)))return;
            break;
        case 31:break; // documented no-op on GBA
        case 34:if(b(r,from+1)==255){release(p,i);return;}if(!unsupported(p,i,op))return;break;
        case 47:{unsigned base=b(r,from+1)?v->velocity:v->volume;v->volume=(uint8_t)clip((int)((base*h(r,from+4))>>8)+b(r,from+2),0,127);break;}
        default:if(!unsupported(p,i,op))return;break; // preview skips only a known, checked byte length
        }
    }
    if(v->active){reject(p,i,v->ip);p->unsupported[1]|=1u<<31;}
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
    uint32_t a=relative(&p->rom,root,p->samples+sample*4,16);if(!a){off(p,0);return 0;}v->pitch=(int32_t)h(&p->rom,a+10)*256;
    return start_sample(p,0,sample,0);
}
