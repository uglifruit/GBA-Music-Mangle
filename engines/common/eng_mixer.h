// eng_mixer.h — bind an engine's voice hooks to payload/mixer.c, the one renderer.
#ifndef ENG_MIXER_H
#define ENG_MIXER_H
#include "eng.h"

// One per engine, and it must outlive the hooks. `rom` is the player's own; `flags` is added to every
// start (MIX_F_UNSIGNED for engines whose samples are offset-binary); every volume is shifted right
// by `volShift`, to level engines that mix hotter than MP2K (each trial ran at half gain).
typedef struct {
    const EngRom *rom;
    uint8_t flags, volShift;
} EngMixCtx;

void eng_mixer_hooks(EngHooks *h, EngMixCtx *ctx);

#endif
