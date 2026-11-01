// Opt-in bridge to the namespaced unsigned-PCM renderer and public mod/follower data types.
#ifndef KR_MANGLE_ADAPTER_H
#define KR_MANGLE_ADAPTER_H
#include "krawall.h"
#include "../../payload/player.h"
#ifdef __cplusplus
extern "C" {
#endif
#define KR_ADAPTER_UNSUPPORTED_ENVELOPE 1u
#define KR_ADAPTER_UNSUPPORTED_LFO 2u
#define KR_ADAPTER_UNSUPPORTED_CHAOS 4u
#define KR_ADAPTER_UNSUPPORTED_RAW 8u
#define KR_ADAPTER_UNSUPPORTED_GATE 16u
// Initialize hooks with kr_mixer_hooks, then pass them to kr_init. No MP2K globals are defined.
void kr_mixer_hooks(KrHooks *hooks, KrPlayer *player);
// Sync controls and up to 16 followers. Return a bitmask of active, unmapped controls.
uint32_t kr_mangle_sync(KrPlayer *, const PlayerMods *, Track followers[PL_TRACKS]);
#ifdef __cplusplus
}
#endif
#endif
