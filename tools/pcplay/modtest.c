// modtest.c — host unit test for the modulator (payload/mod.c): CV, knobs, switch and GBA buttons
// in, g_mods out. The link is stubbed (its inputs are plain variables here); the cart, the scan
// and the player are the real ones, on a ROM from mktestrom.py.
//
//   modtest TEST.gba          prints each check; exit status 0 only if all pass

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "gba.h"
#include "rom.h"
#include "scan.h"
#include "player.h"
#include "mixer.h"
#include "clock.h"
#include "mod.h"
#include "link.h"

uint8_t g_hostIo[0x400];
static uint8_t *s_rom;

const uint8_t *host_rom_ptr(uint32_t a)
{
    if (a < ROM_BASE || a >= ROM_BASE + ROM_MAX) { fprintf(stderr, "read outside cart 0x%08X\n", a); exit(3); }
    return s_rom + (a - ROM_BASE);
}

// ---- link stubs: what link.c would have decoded from the host ----
volatile uint16_t g_in[4]      = { 2048, 2048, 2048, 2048 };
volatile uint8_t  g_gate       = 0;
volatile uint32_t g_gateEdges  = 0;
volatile uint16_t g_knob[3]    = { 2048, 2048, 2048 };
volatile uint8_t  g_switch     = 1;
volatile uint16_t g_hostCaps   = 0;
volatile uint32_t g_audEdges[2] = { 0, 0 };
void link_set_buttons(uint16_t b) { (void)b; }
static uint8_t s_noteKey, s_noteGate;
void link_post_note(uint8_t note, uint8_t gate) { s_noteKey = note; s_noteGate = gate; }
void link_post_pulse(uint8_t beats, uint8_t loops) { (void)beats; (void)loops; }

// ---- helpers ----
static int s_fail, s_pass;
#define CHECK(cond, ...) do { if (cond) s_pass++; else { s_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static void keys(uint16_t held) { *(uint16_t *)(g_hostIo + 0x130) = (uint16_t)~held; }   // active low
static void update(void) { mod_update(); }
static void press(uint16_t key) { keys(key); update(); keys(0); update(); }

static void neutral(void)
{
    for (int i = 0; i < 4; i++) g_in[i] = 2048;
    for (int i = 0; i < 3; i++) g_knob[i] = 2048;
    g_switch = 1;
    g_perform = 0;
    keys(0);
    for (int d = 0; d < DEST_COUNT; d++) g_patch.base[d] = 0;
    update();
}

#define K_A (1u << 0)
#define K_B (1u << 1)
#define K_R (1u << 8)

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: modtest TEST.gba\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    s_rom = malloc(ROM_MAX + 64);
    size_t len = fread(s_rom, 1, ROM_MAX, f);
    fclose(f);
    for (size_t o = (len + 1) & ~(size_t)1; o + 1 < ROM_MAX + 64; o += 2) {
        s_rom[o] = (uint8_t)(o >> 1);
        s_rom[o + 1] = (uint8_t)(o >> 9);
    }
    rom_init();
    scan_run(NULL);
    player_init();
    mod_init();
    keys(0);
    mod_song_select(0);

    // 1. At rest nothing is mangled.
    neutral();
    CHECK(g_mods.voiceShift == 0 && g_mods.tableOffset == 0 && g_mods.sampleStart == 0 && g_mods.romScrub == 0,
          "rest: vsh %d tbl %d sst %d scr %ld", g_mods.voiceShift, g_mods.tableOffset, g_mods.sampleStart,
          (long)g_mods.romScrub);
    CHECK(g_mods.pitch == 0 && g_mods.tempoU == 0x100 && g_mods.mute == 0 && g_mods.reverseChance == 0,
          "rest: pitch %d tempo %u mute %u rev %u", g_mods.pitch, g_mods.tempoU, g_mods.mute, g_mods.reverseChance);

    // 2. CV1 +1 V -> voice shift ~ +10 programs; CV2 +1 V -> +12 semitones (1V/oct at depth 63).
    g_in[0] = 2048 + 341; update();
    CHECK(g_mods.voiceShift == 10, "CV1 +1V: voice shift %d, want 10", g_mods.voiceShift);
    g_in[0] = 2048; g_in[1] = 2048 + 341; update();
    CHECK(g_mods.pitch == 12 * 256, "CV2 +1V: pitch %d, want %d", g_mods.pitch, 12 * 256);
    g_in[1] = 2048 - 2 * 341; update();
    CHECK(g_mods.pitch == -24 * 256, "CV2 -2V: pitch %d, want %d", g_mods.pitch, -24 * 256);
    neutral();

    // 3. Main knob full right strips layers down to track 1 (song 0 has 2 tracks).
    g_knob[0] = 4095; update();
    CHECK(g_mods.mute == 0x0002, "MAIN full: mute 0x%04X, want 0x0002", g_mods.mute);
    g_knob[0] = 0; update();
    CHECK(g_mods.mute == 0, "MAIN full left (POS): mute 0x%04X, want 0", g_mods.mute);
    neutral();

    // 4. Perform mode: A holds REVERSE, R steps VOICE SHIFT.
    press(K_A);
    CHECK(g_mods.reverseChance == 0, "A outside PERFORM must do nothing, rev %u", g_mods.reverseChance);
    g_perform = 1;
    keys(K_A); update();
    CHECK(g_mods.reverseChance >= 250, "A held: reverse %u, want ~251", g_mods.reverseChance);
    keys(0); update();
    CHECK(g_mods.reverseChance == 0, "A released: reverse %u, want 0", g_mods.reverseChance);
    press(K_R); press(K_R);
    CHECK(g_patch.base[DEST_VSHIFT] == 2 && g_mods.voiceShift == 2, "R x2: base %d vsh %d, want 2",
          g_patch.base[DEST_VSHIFT], g_mods.voiceShift);
    // A held key is ONE step, not an auto-repeat stream.
    keys(K_R);
    for (int i = 0; i < 50; i++) update();
    keys(0); update();
    CHECK(g_patch.base[DEST_VSHIFT] == 3, "R held: base %d, want 3 (no auto-repeat in PERFORM)",
          g_patch.base[DEST_VSHIFT]);

    // 5. LATCH: B set to latch CRUSH toggles on and off.
    g_patch.btn[1].mode = BTN_LATCH;
    press(K_B);
    CHECK(g_crushMask != 0xFF || g_crushHold > 1, "B latch on: hold %u mask 0x%02X", g_crushHold, g_crushMask);
    press(K_B);
    CHECK(g_crushHold == 1 && g_crushMask == 0xFF, "B latch off: hold %u mask 0x%02X", g_crushHold, g_crushMask);

    // 6. An EDGE destination on a button: NEXT SONG.
    g_patch.btn[1].dest = DEST_NEXT;
    int before = g_song;
    press(K_B);
    CHECK(g_song == (before + 1) % g_songCount, "B = NEXT: song %d -> %d", before, g_song);
    mod_default_patch();
    mod_song_select(0);

    // 7. The switch UP bypasses CV, buttons and BASE alike.
    neutral();
    g_patch.base[DEST_TRANS] = 5;
    g_in[0] = 4095;
    g_switch = 2; update();
    CHECK(g_mods.voiceShift == 0 && g_mods.pitch == 0, "bypass: vsh %d pitch %d", g_mods.voiceShift, g_mods.pitch);
    g_switch = 1; update();
    CHECK(g_mods.pitch == 5 * 256, "BASE transpose 5: pitch %d", g_mods.pitch);
    neutral();

    // 8. Switch DOWN restarts the song (once per press).
    player_tick(); player_tick();
    g_switch = 0; update();
    CHECK(g_plTicks == 0, "switch down: ticks %u, want 0 (restarted)", g_plTicks);
    g_switch = 1; update();

    // 9. A v1 patch is migrated: version 2, factory buttons, zero BASE.
    Patch p;
    mod_default_patch();
    memcpy(&p, &g_patch, sizeof p);
    memset(&p.table, 0, sizeof p - offsetof(Patch, table));
    p.version = 1;
    CHECK(mod_patch_accept(&p) == 1 && p.version == 2, "v1 patch accepted and migrated (version %u)", p.version);
    CHECK(p.btn[0].dest == DEST_REVERSE && p.btn[3].dest == DEST_VSHIFT, "v1 migration: factory buttons");
    p.magic = 0x464D;                                     // the FM card's magic
    CHECK(mod_patch_accept(&p) == 0, "an FM patch must be rejected");
    CHECK(sizeof(Patch) <= GBA_PATCH_SLOT_BYTES, "Patch %u bytes fits the slot", (unsigned)sizeof(Patch));

    printf("modtest: %d passed, %d failed  (Patch is %u bytes)\n", s_pass, s_fail, (unsigned)sizeof(Patch));
    return s_fail ? 1 : 0;
}
