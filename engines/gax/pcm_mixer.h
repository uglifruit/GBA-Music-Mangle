// Namespaced unsigned-PCM renderer. The existing MixChan layout is reused, not its state.
#ifndef GX_PCM_MIXER_H
#define GX_PCM_MIXER_H
#include "../../payload/mixer.h"
#ifdef __cplusplus
extern "C" {
#endif
extern MixChan g_gx_mix[MIX_CHANNELS];
extern volatile uint8_t g_gx_gain, g_gx_hold, g_gx_mask, g_gx_reverb;
void gx_pcm_init(void);
void gx_pcm_start(int ch,const int8_t *data,uint32_t size,uint32_t loopStart,uint32_t startAt,
                  uint32_t step,uint16_t volL,uint16_t volR,uint8_t flags);
void gx_pcm_stop(int ch);
void gx_pcm_render(int8_t *outL,int8_t *outR);
static inline int gx_pcm_done(int ch){return g_gx_mix[ch].doneGen==g_gx_mix[ch].gen;}
#ifdef __cplusplus
}
#endif
#endif
