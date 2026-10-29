// Namespaced unsigned-PCM renderer. The existing MixChan layout is reused, not its state.
#ifndef KR_PCM_MIXER_H
#define KR_PCM_MIXER_H
#include "../../payload/mixer.h"
#ifdef __cplusplus
extern "C" {
#endif
extern MixChan g_kr_mix[MIX_CHANNELS];
extern volatile uint8_t g_kr_gain, g_kr_hold, g_kr_mask, g_kr_reverb;
void kr_pcm_init(void);
void kr_pcm_start(int ch,const int8_t *data,uint32_t size,uint32_t loopStart,uint32_t startAt,
                  uint32_t step,uint16_t volL,uint16_t volR,uint8_t flags);
void kr_pcm_stop(int ch);
void kr_pcm_render(int8_t *outL,int8_t *outR);
static inline int kr_pcm_done(int ch){return g_kr_mix[ch].doneGen==g_kr_mix[ch].gen;}
#ifdef __cplusplus
}
#endif
#endif
