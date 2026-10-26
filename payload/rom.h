// rom.h — the cartridge: wait states, size, header, and bounds-checked reads.
//
// The inserted game is read in place at 0x08000000. NOTHING read from it is trusted: every pointer
// that comes out of the cart goes through rom_ok() before it is followed, because the mangle hooks
// deliberately read from the wrong places and a game's own data is not guaranteed sane either. The
// worst a bad pointer may ever do is make a wrong noise.
//
// Addresses are GBA bus addresses (0x08xxxxxx) everywhere, as the game's own pointers are. ROM_PTR()
// turns one into something C can dereference: the identity on the GBA, an offset into a buffer in
// the host build (tools/pcplay).
#pragma once

#include <stdint.h>

#define ROM_BASE 0x08000000u
#define ROM_MAX  0x02000000u           // 32 MB, the largest a cart can map

#ifdef HOST
extern const uint8_t *g_romHost;       // ROM_MAX bytes, padded past the image with open-bus values
#define ROM_PTR(a) ((const uint8_t *)(g_romHost + ((uint32_t)(a) - ROM_BASE)))
#else
#define ROM_PTR(a) ((const uint8_t *)(uintptr_t)(a))
#endif

extern uint32_t g_romSize;             // detected image size in bytes (0 = no cart)
extern uint32_t g_romEnd;              // ROM_BASE + g_romSize
extern char     g_romTitle[13];        // header title, NUL-terminated, unprintables as '.'
extern char     g_romCode[5];          // 4-letter game code

// Wait states. FAST (3/1 + prefetch) is what nearly every game runs at; SAFE (4/2) is the BIOS
// default and works with anything.
#define ROM_SPEED_FAST 0
#define ROM_SPEED_SAFE 1
void rom_set_speed(int speed);

// Detect the size, read the header. Returns 0 if no cartridge seems to be present.
int rom_init(void);

// True if [addr, addr+len) lies entirely inside the image.
static inline int rom_ok(uint32_t addr, uint32_t len)
{
    return addr >= ROM_BASE && addr < g_romEnd && len <= g_romEnd - addr;
}

// True if v looks like a pointer into the image (any alignment).
static inline int rom_isptr(uint32_t v) { return v >= ROM_BASE && v < g_romEnd; }

// Byte-wise reads, so an odd address (a mangled table offset makes plenty) reads the right bytes
// rather than the ARM7's rotated word. These are control-rate; the mixer reads samples directly.
static inline uint8_t  rom_u8(uint32_t a)  { return ROM_PTR(a)[0]; }
static inline uint16_t rom_u16(uint32_t a) { const uint8_t *p = ROM_PTR(a); return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rom_u32(uint32_t a)
{
    const uint8_t *p = ROM_PTR(a);
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
