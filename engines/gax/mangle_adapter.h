// Optional Music Mangle bridge, with no definitions of Sappy player globals/functions.
#ifndef GX_MANGLE_ADAPTER_H
#define GX_MANGLE_ADAPTER_H
#include "gax.h"
#include "pcm_mixer.h"
#include "../../payload/player.h"
#ifdef __cplusplus
extern "C" {
#endif
#define GX_UNMAPPED_ENVELOPE 1u
#define GX_UNMAPPED_LFO 2u
#define GX_UNMAPPED_CHAOS 4u
#define GX_UNMAPPED_RAW 8u
#define GX_UNMAPPED_GATE 16u
void gx_mixer_hooks(GxHooks *,GxPlayer *);
uint32_t gx_mangle_sync(GxPlayer *,const PlayerMods *,Track followers[PL_TRACKS]);
#ifdef __cplusplus
}
#endif
#endif
