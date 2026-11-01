// Uses the established atomic kr_pcm_start path; leaves serial/audio IRQ ownership unchanged.
#include "mangle_adapter.h"
#include "pcm_mixer.h"
static void start(void *user,unsigned n,const KrVoice *v) {
    KrPlayer *p=user;
    if(n>=MIX_CHANNELS)return;
    kr_pcm_start(n,(const int8_t *)(p->rom.data+(v->data-p->rom.base)),v->size,v->loop,
                v->start,v->step,v->left,v->right,v->reverse?MIX_F_REVERSE:0);
}
static void update(void *user,unsigned n,const KrVoice *v) {
    (void)user;
    if(n>=MIX_CHANNELS)return;
    g_kr_mix[n].step=v->step;g_kr_mix[n].volL=v->left;g_kr_mix[n].volR=v->right;
}
static void stop(void *user,unsigned n) {(void)user;if(n<MIX_CHANNELS)kr_pcm_stop(n);}
static int done(void *user,unsigned n) {(void)user;return n>=MIX_CHANNELS||kr_pcm_done(n);}
void kr_mixer_hooks(KrHooks *h,KrPlayer *p) {
    h->user=p;h->start=start;h->update=update;h->stop=stop;h->done=done;h->note=0;h->event=0;
}
uint32_t kr_mangle_sync(KrPlayer *p,const PlayerMods *m,Track f[PL_TRACKS]) {
    p->mods.mute=m->mute;p->mods.pitch=m->pitch;p->mods.tempo=m->tempoU;
    p->mods.voice_shift=m->voiceShift;p->mods.table_offset=m->tableOffset;
    p->mods.sample_start=m->sampleStart;p->mods.sample_end=m->sampleEnd;
    p->mods.reverse_chance=m->reverseChance;p->mods.drop_chance=m->dropChance;p->mods.scrub=m->romScrub;
    g_kr_reverb=m->reverb>0?(uint8_t)m->reverb:0;
    // Crush controls can be routed directly to g_kr_hold and g_kr_mask.
    if(f)for(unsigned n=0;n<PL_TRACKS;++n) {
        f[n].flags=n<p->channels&&p->running?MPT_FLG_EXIST:0;
        f[n].lastKey=p->tracks[n].note;f[n].gate=p->tracks[n].gate;
        f[n].notes=p->tracks[n].notes;f[n].act=p->tracks[n].activity;
    }
    kr_update(p);
    uint32_t unsupported=0;
    if(m->attack||m->decay||m->sustain||m->release)unsupported|=KR_ADAPTER_UNSUPPORTED_ENVELOPE;
    if(m->vibrato||m->tremolo)unsupported|=KR_ADAPTER_UNSUPPORTED_LFO;
    if(m->chaos)unsupported|=KR_ADAPTER_UNSUPPORTED_CHAOS;
    if(m->tableOffset||m->scrubLen!=4096)unsupported|=KR_ADAPTER_UNSUPPORTED_RAW;
    if(m->gate)unsupported|=KR_ADAPTER_UNSUPPORTED_GATE;
    return unsupported;
}
