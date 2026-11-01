// eng.h — the interface shapes every other-engine player shares (Krawall, GAX, MusyX).
//
// The three proof-of-concept players were written to one pattern: a ROM view, a voice handed to the
// renderer, and hooks the player calls to start, update and stop voices. Their fields were the same
// and only the prefix differed, so they are these types now (each engine header typedefs its own
// names to them). The mods are NOT shared: each engine's struct has its own layout and ranges, and
// payload/engine.c fills each from PlayerMods.
#ifndef ENG_COMMON_H
#define ENG_COMMON_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// The cartridge as the player sees it: `data` holds the byte at bus address `base`. On the GBA
// data == (const uint8_t *)base; on the PC it is the loaded image.
typedef struct {
    const uint8_t *data;
    uint32_t base, size;
} EngRom;

// One renderer voice, as the player proposes it.
typedef struct {
    uint32_t data, size, loop, start, step; // bus address; samples; loop start or UINT32_MAX; Q12 step at 16384 Hz
    uint16_t left, right;                   // 0..255, after envelope and mute
    uint8_t reverse;
} EngVoice;

// The player's calls out. Voice numbers are renderer slots (0..11 here).
typedef struct {
    void *user;
    void (*start)(void *, unsigned, const EngVoice *);
    void (*update)(void *, unsigned, const EngVoice *);
    void (*stop)(void *, unsigned);
    int (*done)(void *, unsigned);
    void (*note)(void *, unsigned, EngVoice *);   // may alter a proposed voice at note-on
    void (*event)(void *, unsigned, uint32_t);
} EngHooks;

#ifdef __cplusplus
}
#endif
#endif
