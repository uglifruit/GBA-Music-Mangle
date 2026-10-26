// gba.h — the few hardware conveniences more than one module needs.
//
// Kept deliberately small: each module still defines the registers it owns next to the code that
// uses them, as 108 always did. What lives here is what has to be spelled identically everywhere.
//
// HOST BUILDS. The engine modules (rom, scan, player, mixer, cgb, m4a_tables) also compile for the
// PC under -DHOST, for tools/pcplay, which renders a song to a WAV with the very same code. Under
// HOST the I/O registers are a plain array (so the PSG writes land somewhere harmless and can be
// read back) and the IWRAM placement attributes vanish.
#pragma once

#include <stdint.h>

#ifdef HOST
extern uint8_t g_hostIo[0x400];
#define R8(off)  (*(volatile uint8_t  *)(g_hostIo + (off)))
#define R16(off) (*(volatile uint16_t *)(g_hostIo + (off)))
#define R32(off) (*(volatile uint32_t *)(g_hostIo + (off)))
#else
#define R8(off)  (*(volatile uint8_t  *)(0x04000000 + (off)))
#define R16(off) (*(volatile uint16_t *)(0x04000000 + (off)))
#define R32(off) (*(volatile uint32_t *)(0x04000000 + (off)))
#endif

// ---- IWRAM placement --------------------------------------------------------------------------
// The image is linked for EWRAM, whose 16-bit bus costs a 32-bit ARM fetch three extra cycles.
// IWRAM is 32-bit and zero wait state, so anything that runs per sample lives there: crt0.s copies
// the .iwram section in from its load address before main() and zeroes .iwram_bss.
//
// A plain BL reaches IWRAM from EWRAM (they are 16 MB apart and BL spans 32 MB), so no long_call.
// noinline because inlining an IWRAM function into an EWRAM caller would quietly run it from EWRAM.
//
// Three sections rather than one because GCC refuses to put code, read-only data and writable data
// in the same named section ("section type conflict").
#ifdef HOST
#define IWRAM_CODE
#define IWRAM_RODATA
#define IWRAM_BSS
#else
#define IWRAM_CODE   __attribute__((section(".iwram"), noinline))
#define IWRAM_RODATA __attribute__((section(".iwram_rodata")))
#define IWRAM_BSS    __attribute__((section(".iwram_bss")))
#endif

// ---- interrupt masking ------------------------------------------------------------------------
// A handful of multi-field updates (a mixer channel start, a loop window move) must be seen by the
// audio interrupt all at once or not at all. They are a few stores long, so a short IME-off window
// is cheaper and far easier to reason about than a lock-free protocol for each.
#ifdef HOST
#define IRQ_SAVE()      0u
#define IRQ_RESTORE(s)  ((void)(s))
#else
#define IRQ_SAVE()      ({ uint16_t _s = R16(0x0208); R16(0x0208) = 0; _s; })
#define IRQ_RESTORE(s)  (R16(0x0208) = (s))
#endif
