#include "mixer_adapter.h"
static void start(void *ctx,unsigned i,const MsPcm *v){MsPlayer *p=ctx;if(i>=MIX_CHANNELS)return;
    ms_pcm_start((int)i,(const int8_t *)(p->rom.data+(v->data-p->rom.base)),v->size,v->loop,v->start,v->step,v->left,v->right,v->reverse?MIX_F_REVERSE:0);}
static void update(void *ctx,unsigned i,const MsPcm *v){(void)ctx;if(i>=MIX_CHANNELS)return;g_ms_mix[i].step=v->step;g_ms_mix[i].volL=v->left;g_ms_mix[i].volR=v->right;}
static void stop(void *ctx,unsigned i){(void)ctx;if(i<MIX_CHANNELS)ms_pcm_stop((int)i);}
static int done(void *ctx,unsigned i){(void)ctx;return i>=MIX_CHANNELS||ms_pcm_done((int)i);}
void ms_mixer_hooks(MsHooks *h,MsPlayer *p){h->user=p;h->start=start;h->update=update;h->stop=stop;h->done=done;h->note=0;h->event=0;}
