// eng_chaos.h — CHAOS for every engine but MP2K (which has its own, inside player.c).
//
// Per-note dice, rolled in the engines' `note` hook from a private generator: interval jumps, hard
// pans, reverse, a random start, accents and (above 128) raw cart data as the sample. The engines
// recompute a voice's step and volume on every update, so the pitch, pan and accent dice are kept
// per renderer slot and applied after the engine, at the start and on every update (eng_mixer.c).
//
// A PATTERN THAT REPEATS, as MP2K's: the generator is reseeded from the seed at every song start and
// every counted loop, and draws four numbers per note whatever happens, so the same notes get the
// same dice each time round. FREE chaos skips the reseeds; REROLL deals a new seed.
#ifndef ENG_CHAOS_H
#define ENG_CHAOS_H
#include "eng.h"

void eng_chaos_set(uint8_t amount, uint8_t free);   // 0..255; free = never reseed
void eng_chaos_reseed(void);                        // song start, counted loop (ignored when free)
void eng_chaos_reroll(void);                        // a new pattern
// The note hook's work: roll slot i's dice and alter the proposed voice (reverse, start, sample).
// The engine re-validates the voice afterwards.
void eng_chaos_note(unsigned i, EngVoice *v, const EngRom *rom);
// The dice that persist: slot i's step and volumes as the engine computed them, chaos applied.
void eng_chaos_apply(unsigned i, uint32_t *step, uint16_t *left, uint16_t *right);

#endif
