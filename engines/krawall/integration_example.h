// Opt-in application facade. No definitions conflict with the existing player API.
#ifndef KR_EXAMPLE_H
#define KR_EXAMPLE_H
#include "mangle_adapter.h"
#include "pcm_mixer.h"
#ifdef __cplusplus
extern "C" {
#endif
int kr_example_start(KrRom rom,const KrSong *song);
KrPlayer *kr_example_player(void);
uint32_t kr_example_service(const PlayerMods *mods,Track followers[PL_TRACKS]);
void kr_example_tick(void);
#ifdef __cplusplus
}
#endif
#endif
