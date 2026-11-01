// Namespaced signed-PCM renderer. The existing MixChan layout is reused, not its state.
#ifndef MS_PCM_MIXER_H
#define MS_PCM_MIXER_H
#include "../../payload/mixer.h"
#ifdef __cplusplus
extern "C" {
#endif
extern MixChan g_ms_mix[MIX_CHANNELS];
extern volatile uint8_t g_ms_gain, g_ms_hold, g_ms_mask, g_ms_reverb;
void ms_pcm_init(void);
void ms_pcm_start(int ch,const int8_t *data,uint32_t size,uint32_t loopStart,uint32_t startAt,
                  uint32_t step,uint16_t volL,uint16_t volR,uint8_t flags);
void ms_pcm_stop(int ch);
void ms_pcm_render(int8_t *outL,int8_t *outR);
static inline int ms_pcm_done(int ch){return g_ms_mix[ch].doneGen==g_ms_mix[ch].gen;}
#ifdef __cplusplus
}
#endif
#endif
