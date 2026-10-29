// Connect MsPlayer voices to the separate signed PCM mixer.
#ifndef MS_MIXER_ADAPTER_H
#define MS_MIXER_ADAPTER_H
#include "musyx.h"
#include "pcm_mixer.h"
#ifdef __cplusplus
extern "C" {
#endif
void ms_mixer_hooks(MsHooks *,MsPlayer *);
#ifdef __cplusplus
}
#endif
#endif
