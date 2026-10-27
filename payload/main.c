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
#define REG_KEYINPUT (*(volatile uint16_t *)(REG_BASE + 0x0130))

// Set once the engine is up. Before that, gfx_idle() may only keep the link alive.
static uint8_t s_engineUp = 0;

// Called from inside every drawing routine and from the frame wait. The clock has to keep running
// while the screen is being drawn - a sequencer tick that waits for a redraw is a late note - and
// the modulator with it, so a clock pulse is seen within a millisecond or so of arriving.
// The service (modulator + clock) runs at most once per SERVICE_COUNTS of Timer 2, about 1 ms.
// It used to run on every call, i.e. after every character drawn, and from EWRAM it cost far more
// than the character: text crawled. Nothing needs it more often - edges arrive as counters, and the
// clock schedules against Timer 2 whatever the call rate. The link still runs on every call.
#define SERVICE_COUNTS 16u                  // Timer 2 at 16384 Hz: 16 counts = 0.98 ms
#define REG_TM2CNT_L (*(volatile uint16_t *)(REG_BASE + 0x0108))
#define REG_TM3CNT_L (*(volatile uint16_t *)(REG_BASE + 0x010C))

// SERVICE LOAD (CAL page): wall time inside one service as a share of its ~1 ms period, percent
// x 16, measured on Timer 3 (CPU clock, started by audio_init for DSP LOAD). Interrupts landing
// inside it count: that is time the screen did not get either.
volatile uint16_t g_svcAvg = 0, g_svcPeak = 0;
static uint32_t s_svcAcc;
static uint16_t s_svcLast;

void gfx_idle(void)
{
    link_service();
    if (!s_engineUp) return;
    uint16_t now = REG_TM2CNT_L;
    if ((uint16_t)(now - s_svcLast) < SERVICE_COUNTS) return;
    s_svcLast = now;
    uint16_t t0 = REG_TM3CNT_L;
    mod_update();
    clock_service();
    uint32_t pct = ((uint32_t)(uint16_t)(REG_TM3CNT_L - t0) * 25u) >> 8;   // cycles x 1600 / 16384
    s_svcAcc += pct - (s_svcAcc >> 6);
    g_svcAvg = (uint16_t)(s_svcAcc >> 6);
    if (pct > g_svcPeak) g_svcPeak = (uint16_t)pct;
}

static void scan_progress(uint32_t done256)
{
    ui_scan_progress(done256);
    gfx_idle();
}

static void wait_frame(void)
{
    while (REG_VCOUNT <  SCREEN_H) gfx_idle();
    while (REG_VCOUNT >= SCREEN_H) gfx_idle();
}

// ---- booting from a saved slot --------------------------------------------------------------------
// A slot saved on this cart carries its song tables' addresses (Patch v4), so the multi-second scan
// can be skipped. The host announces every slot's cart tag (GBA_OP_SLOT_TAG); the lowest-numbered
// slot whose tag matches is loaded; its full cart identity is checked and its tables must still
// hold songs. ANYTHING missing or wrong - an old host that sends no tags, no match, a lost load, a
// stale table - falls through to the real scan. Holding B skips the whole thing.
static Patch   s_bootPatch;
static uint8_t s_bootSlot = 0xFF;          // the slot booted from, 0xFF = scanned

#define TAG_WAIT_FRAMES  45                // ~0.75 s; every tag goes round in about 0.3 s
#define LOAD_WAIT_FRAMES 60                // ~1 s; a slot is about 0.3 s of link
#define KEYS_B           (1u << 1)

static int b_held(void) { return (~REG_KEYINPUT & KEYS_B) != 0; }

static int boot_from_slot(void)
{
    if (!g_romSize) return 0;
    ui_scan_status("CHECKING SAVED SLOTS", "HOLD B TO SCAN INSTEAD");
    for (int f = 0; f < TAG_WAIT_FRAMES && g_slotTagSeen != 0xFFFFu; f++) {
        wait_frame();
        if (b_held()) return 0;
    }
    uint16_t tag = rom_cart_tag();
    int slot = -1;
    for (int s = 0; s < GBA_PATCH_SLOTS && slot < 0; s++)
        if (((g_slotTagSeen >> s) & 1u) && g_slotTag[s] == tag) slot = s;
    if (slot < 0) return 0;

    g_loadReady = 0;
    link_begin_load((uint8_t)slot);
    for (int f = 0; f < LOAD_WAIT_FRAMES && g_xferState == LINK_XFER_LOAD; f++) {
        wait_frame();
        if (b_held()) break;
    }
    int got = g_loadReady;
    g_xferState  = LINK_XFER_NONE;
    g_xferResult = LINK_RESULT_IDLE;
    g_loadReady  = 0;
    if (!got) return 0;

    // Out of the byte buffer first: it has no alignment guarantee (see ui.c, mem_poll).
    volatile uint8_t *d = (volatile uint8_t *)&s_bootPatch;
    for (unsigned i = 0; i < sizeof(Patch); i++) d[i] = g_patchBuf[i];
    if (!mod_patch_accept(&s_bootPatch) || !mod_patch_is_this_cart(&s_bootPatch)) return 0;
    if (!scan_install(s_bootPatch.tableCount, s_bootPatch.tableAddr, s_bootPatch.tableRate)) return 0;
    s_bootSlot = (uint8_t)slot;
    return 1;
}

// The cart: from a saved slot when one matches (boot only), else the scan.
static void scan_cart(int allowSaved)
{
    g_song = -1;
    s_bootSlot = 0xFF;
    rom_init();
    ui_scan_begin();
    if (!(allowSaved && boot_from_slot())) {
        ui_scan_status("SCANNING FOR MUSIC", "");
        scan_run(scan_progress);
    }
    ui_scan_result(s_bootSlot);
    g_plGameRate = g_scan.gameRate;
}

// Hold the scan result on screen, keeping everything serviced: 90 frames, or until a button is
// pressed (and released) when nothing playable was found.
static void hold_result(void)
{
    int wait = (g_songCount == 0 && g_romSize != 0);
    int pressed = 0;
    for (int f = 0; wait || f < 90; f++) {
        wait_frame();
        uint16_t down = (uint16_t)(~REG_KEYINPUT & 0x03FF);
        if (down) pressed = 1;
        else if (pressed) wait = 0;
    }
}

// SET > RESCAN CART. The cart cannot change under a running console, but a scan that went wrong
// (a dusty contact on the first read) can be repeated without a power cycle.
static void rescan(void)
{
    player_stop();
    uint8_t up = s_engineUp;
    s_engineUp = 0;
    scan_cart(0);                           // a rescan always scans
    s_engineUp = up;
    hold_result();
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

    // ---- 4. the cart: from a slot saved on it, or scanned ----
    scan_cart(1);

    // ---- 5. sound, player, clock, modulator ----
    audio_init();
    player_init();
    clock_init();
    mod_init();
    s_engineUp = 1;
    g_ui_rescan = rescan;

    // Let the scan result be read (about a second and a half), then start the first song. With no
    // music it stays up until a button is pressed: it is the only thing worth reading.
    hold_result();
    if (g_songCount) mod_song_select(0);
    if (s_bootSlot != 0xFF) {
        // Booted from a slot: it is the patch now, scene and all (table, list, song, mutes).
        volatile uint8_t *gp = (volatile uint8_t *)&g_patch;
        const volatile uint8_t *bp = (const volatile uint8_t *)&s_bootPatch;
        for (unsigned i = 0; i < sizeof(Patch); i++) gp[i] = bp[i];
        mod_patch_loaded();
        ui_mem_select(s_bootSlot);
    }
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
