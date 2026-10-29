// Main-loop controls/followers and namespaced atomic PCM callbacks. IRQs remain application-owned.
#include "mangle_adapter.h"
static void start(void *ctx,unsigned c,const GxVoice *v){
    GxPlayer *p=ctx;if(c>=MIX_CHANNELS)return;
    gx_pcm_start((int)c,(const int8_t *)(p->rom.data+(v->data-p->rom.base)),v->size,v->loop,v->start,v->step,v->left,v->right,v->reverse?MIX_F_REVERSE:0);
}
static void update(void *ctx,unsigned c,const GxVoice *v){(void)ctx;if(c>=MIX_CHANNELS)return;g_gx_mix[c].step=v->step;g_gx_mix[c].volL=v->left;g_gx_mix[c].volR=v->right;}
static void stop(void *ctx,unsigned c){(void)ctx;if(c<MIX_CHANNELS)gx_pcm_stop((int)c);}
static int done(void *ctx,unsigned c){(void)ctx;return c>=MIX_CHANNELS||gx_pcm_done((int)c);}
void gx_mixer_hooks(GxHooks *h,GxPlayer *p){h->user=p;h->start=start;h->update=update;h->stop=stop;h->done=done;h->note=0;h->event=0;}
uint32_t gx_mangle_sync(GxPlayer *p,const PlayerMods *m,Track f[PL_TRACKS]){
    p->mods.mute=m->mute;p->mods.pitch=m->pitch;p->mods.tempo=m->tempoU;p->mods.voice_shift=m->voiceShift;p->mods.table_offset=m->tableOffset;
    p->mods.sample_start=m->sampleStart;p->mods.sample_end=m->sampleEnd;p->mods.reverse_chance=m->reverseChance;
    p->mods.drop_chance=m->dropChance;p->mods.scrub=m->romScrub;g_gx_reverb=m->reverb>0?(uint8_t)m->reverb:0;
    if(f)for(unsigned c=0;c<PL_TRACKS;++c){f[c].flags=p->running&&c<p->channels?MPT_FLG_EXIST:0;f[c].lastKey=p->tracks[c].note;
        f[c].notes=p->tracks[c].notes;f[c].gate=p->tracks[c].gate;f[c].act=p->tracks[c].activity;}
    gx_update(p);uint32_t flags=0;
    if(m->attack||m->decay||m->sustain||m->release)flags|=GX_UNMAPPED_ENVELOPE;
    if(m->vibrato||m->tremolo)flags|=GX_UNMAPPED_LFO;
    if(m->chaos)flags|=GX_UNMAPPED_CHAOS;
    if(m->scrubLen!=4096)flags|=GX_UNMAPPED_RAW;
    if(m->gate)flags|=GX_UNMAPPED_GATE;
    return flags;
}
