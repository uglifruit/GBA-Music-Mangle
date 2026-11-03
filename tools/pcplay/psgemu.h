// psgemu.h — the Game Boy channels, emulated for pcplay (see psgemu.c).
#pragma once
#include <stdint.h>

// Add n stereo frames of the four channels, as the registers (g_hostIo) set them, into lr.
void psg_emu_render(int16_t *lr, unsigned n);
void psg_emu_reset(void);
