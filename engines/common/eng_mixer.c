// eng_mixer.c — see eng_mixer.h. Replaces the three engines' identical adapters and their renamed
// mixer copies: every engine now plays through payload/mixer.c. CHAOS rides on the same hooks
// (eng_chaos.c): its dice are rolled at note-on and applied after the engine on every start and update.
#include "eng_mixer.h"
#include "eng_chaos.h"
#include "../../payload/mixer.h"

static void start(void *ctx, unsigned i, const EngVoice *v)
{
    const EngMixCtx *c = ctx;
    const EngRom *r = c->rom;
    if (i >= MIX_CHANNELS) return;
    uint32_t step = v->step;
    uint16_t l = (uint16_t)(v->left >> c->volShift), rt = (uint16_t)(v->right >> c->volShift);
    eng_chaos_apply(i, &step, &l, &rt);
    mixer_start((int)i, (const int8_t *)(r->data + (v->data - r->base)), v->size, v->loop, v->start,
                step, l, rt, (uint8_t)(c->flags | (v->reverse ? MIX_F_REVERSE : 0)));
}

static void update(void *ctx, unsigned i, const EngVoice *v)
{
    const EngMixCtx *c = ctx;
    if (i >= MIX_CHANNELS) return;
    uint32_t step = v->step;
    uint16_t l = (uint16_t)(v->left >> c->volShift), rt = (uint16_t)(v->right >> c->volShift);
    eng_chaos_apply(i, &step, &l, &rt);
    g_mix[i].step = step;
    g_mix[i].volL = l;
    g_mix[i].volR = rt;
}

static void stop(void *ctx, unsigned i) { (void)ctx; if (i < MIX_CHANNELS) mixer_stop((int)i); }
static int  done(void *ctx, unsigned i) { (void)ctx; return i >= MIX_CHANNELS || mixer_done((int)i); }
static void note(void *ctx, unsigned i, EngVoice *v) { eng_chaos_note(i, v, ((const EngMixCtx *)ctx)->rom); }

void eng_mixer_hooks(EngHooks *h, EngMixCtx *ctx)
{
    h->user = ctx;
    h->start = start;
    h->update = update;
    h->stop = stop;
    h->done = done;
    h->note = note;
    h->event = 0;
}
