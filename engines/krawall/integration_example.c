// Compile this only into an explicitly selected experimental build.
#include "integration_example.h"
static KrPlayer s_player;
int kr_example_start(KrRom rom,const KrSong *song) {
    // Call while audio is stopped; initialization is not an ISR-safe operation.
    KrHooks hooks;kr_pcm_init();kr_mixer_hooks(&hooks,&s_player);
    kr_init(&s_player,rom,&hooks);return kr_start(&s_player,song);
}
KrPlayer *kr_example_player(void){return &s_player;}
uint32_t kr_example_service(const PlayerMods *mods,Track followers[PL_TRACKS]) {
    return kr_mangle_sync(&s_player,mods,followers);
}
void kr_example_tick(void){kr_tick(&s_player);}
