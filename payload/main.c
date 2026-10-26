// payload/main.c — GBA Cart Player: the multiboot payload.
//
// Runs from EWRAM after the RP2040 uploads it via BIOS Multiboot (the mixer runs from IWRAM, copied
// there by crt0.s), with a commercial cartridge still inserted - the console must be powered on
// holding START+SELECT, so the BIOS skips the cart and waits for multiboot while the cart stays
// readable at 0x08000000.
//
// Order of business, and the order matters:
//   1. LCD up.
//   2. The interrupt dispatcher installed, before anything that enables an interrupt source.
//   3. Serial slave up: the host starts polling within milliseconds, and the scan below can take a
//      few seconds, so the link has to be alive (its IRQ re-arms the slave) before it starts.
//   4. The cart: size, header, then the scan for MP2K song data, with a progress bar.
//   5. Sound up (audio_init sets SOUNDCNT_X bit 7 first, without which every other sound register
//      write is silently discarded), then the player, the clock and the modulator.
//   6. Loop: modulator, clock, a frame of UI; the link and the clock kept serviced throughout.
//
// Dependency-free (no libgba, no libc) so it builds with a bare arm-none-eabi toolchain
// targeting armv4t. See build.sh.

#include <stdint.h>

#include "gfx.h"
#include "irq.h"
#include "link.h"
#include "audio.h"
#include "rom.h"
#include "scan.h"
#include "player.h"
#include "clock.h"
#include "mod.h"
#include "ui.h"
#include "diag.h"

#define REG_BASE   0x04000000
#define REG_VCOUNT (*(volatile uint16_t *)(REG_BASE + 0x0006))

// Set once the engine is up. Before that, gfx_idle() may only keep the link alive.
static uint8_t s_engineUp = 0;

// Called from inside every drawing routine and from the frame wait. The clock has to keep running
// while the screen is being drawn - a sequencer tick that waits for a redraw is a late note - and
// the modulator with it, so a clock pulse is seen within a millisecond or so of arriving.
void gfx_idle(void)
{
    link_service();
    if (s_engineUp) {
        mod_update();
        clock_service();
    }
}

static void scan_progress(uint32_t done256)
{
    ui_scan_progress(done256);
    gfx_idle();
}

static void scan_cart(void)
{
    g_song = -1;
    rom_init();
    ui_scan_begin();
    scan_run(scan_progress);
    ui_scan_result();
    g_plGameRate = g_scan.gameRate;
}

// SET > RESCAN CART. The cart cannot change under a running console, but a scan that went wrong
// (a dusty contact on the first read) can be repeated without a power cycle.
static void rescan(void)
{
    player_stop();
    uint8_t up = s_engineUp;
    s_engineUp = 0;
    scan_cart();
    s_engineUp = up;
    // Hold the result on screen for a moment, keeping everything serviced.
    for (int f = 0; f < 90; f++) {
        while (REG_VCOUNT <  SCREEN_H) gfx_idle();
        while (REG_VCOUNT >= SCREEN_H) gfx_idle();
    }
    if (g_songCount) mod_song_select(0);
    ui_init();
}

int main(void)
{
    // ---- 1. LCD up ----
    gfx_init();

    // ---- 2. interrupts, 3. link ----
    irq_init();
    link_init();

    // ---- 4. the cart ----
    scan_cart();

    // ---- 5. sound, player, clock, modulator ----
    audio_init();
    player_init();
    clock_init();
    mod_init();
    s_engineUp = 1;
    g_ui_rescan = rescan;

    // Let the scan result be read (about a second and a half), then start the first song.
    for (int f = 0; f < 90; f++) {
        while (REG_VCOUNT <  SCREEN_H) gfx_idle();
        while (REG_VCOUNT >= SCREEN_H) gfx_idle();
    }
    if (g_songCount) mod_song_select(0);
    ui_init();

    for (;;) {
        // The diagnostics take over the whole screen when their magic words arrive. Their
        // magics live under the protocol tag reserved for them, so a live stream word can
        // never fall into this by accident.
        if (diag_active()) {
            gfx_idle();
            diag_frame();
            continue;
        }

        ui_frame();

        // Pace to roughly one frame without ever blocking the link or stalling the clock.
        while (REG_VCOUNT <  SCREEN_H) gfx_idle();
        while (REG_VCOUNT >= SCREEN_H) gfx_idle();
    }
}
