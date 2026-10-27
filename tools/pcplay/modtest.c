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

#define DEAD_BAND_TEST 24                         // mod.c's DEAD_BAND
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

    // 2b. Readings just off 0 mangle nothing (an unpatched jack, a knob at noon), on either side.
    for (int o = -30; o <= 30; o += 3) {
        g_in[0] = (uint16_t)(2048 + o); g_in[1] = (uint16_t)(2048 + o); g_in[2] = (uint16_t)(2048 + o);
        g_in[3] = (uint16_t)(2048 + o); g_knob[1] = (uint16_t)(2048 + o); g_knob[2] = (uint16_t)(2048 + o);
        update();
        int inBand = o > -DEAD_BAND_TEST && o < DEAD_BAND_TEST;
        CHECK(g_mods.voiceShift == 0 && (!inBand || (g_mods.pitch == 0 && g_mods.sampleStart == 0 &&
              g_mods.romScrub == 0 && g_mods.tempoU == 0x100)),
              "input %+d: vsh %d pitch %d sst %u scrub %ld tempo %u", o, g_mods.voiceShift, g_mods.pitch,
              g_mods.sampleStart, (long)g_mods.romScrub, g_mods.tempoU);
    }
    g_in[0] = 2048 - 341; update();
    CHECK(g_mods.voiceShift == -10, "CV1 -1V: voice shift %d, want -10", g_mods.voiceShift);
    neutral();

    // 2c. The scan starts on the MUSIC list (the test cart has effects on players 1 and 2).
    CHECK(g_listMode == LIST_MUSIC, "default list %u, want MUSIC", g_listMode);

    // 2d. Knob X (TEMPO) roughly at noon is the song's own tempo; the ends still reach x0.25 / ~x4.
    for (int o = -190; o <= 190; o += 38) {
        g_knob[1] = (uint16_t)(2048 + o); g_knob[2] = (uint16_t)(2048 + o); update();
        CHECK(g_mods.tempoU == 0x100 && g_mods.romScrub == 0, "knobs %+d off noon: tempo %u scrub %ld",
              o, g_mods.tempoU, (long)g_mods.romScrub);
    }
    g_knob[1] = 0; update();
    CHECK(g_mods.tempoU <= 70, "knob X full left: tempo %u, want ~64 (x0.25; depth 63 is 63/64 scale)", g_mods.tempoU);
    g_knob[1] = 4095; update();
    CHECK(g_mods.tempoU >= 1000, "knob X full right: tempo %u, want ~1024 (x4)", g_mods.tempoU);
    neutral();

    // 2e. Each table's rate is m4aSoundInit's own sound mode, not the decoy before it (scan.c).
    CHECK(g_tableCount == 2 && g_tables[0].gameRate == 13379 && g_tables[1].gameRate == 18157,
          "rates %u / %u, want 13379 / 18157", g_tables[0].gameRate, g_tables[1].gameRate);

    // 2f. The factory map and buttons are the performance set (mod.h).
    CHECK(g_patch.mod[SRC_MAIN].dest == DEST_CHAOS && g_patch.mod[SRC_AU2].dest == DEST_RBAR
          && g_patch.mod[SRC_X].dest == DEST_TEMPO && g_patch.btn[0].dest == DEST_RBAR
          && g_patch.btn[1].dest == DEST_REROLL && g_patch.btn[6].dest == DEST_CLEAR
          && g_patch.btn[7].dest == DEST_CHAOS && g_patch.btn[4].dest == DEST_TRANS,
          "factory: performance map and buttons");
    g_knob[0] = 4095; update();
    CHECK(g_mods.chaos >= 240, "MAIN full right: chaos %u, want ~250", g_mods.chaos);
    g_knob[0] = 2048 + 100; update();
    CHECK(g_mods.chaos == 0, "MAIN near noon: chaos %u, want 0 (detent)", g_mods.chaos);
    neutral();

    // The tests below set up the mappings they test, rather than lean on the factory ones.
    g_patch.mod[SRC_MAIN].dest = DEST_LAYERS;
    g_patch.btn[0].dest = DEST_REVERSE; g_patch.btn[0].amount = 63; g_patch.btn[0].mode = BTN_HOLD;
    g_patch.btn[1].dest = DEST_CRUSH;   g_patch.btn[1].amount = 63; g_patch.btn[1].mode = BTN_HOLD;
    g_patch.btn[3].dest = DEST_VSHIFT;  g_patch.btn[3].amount = 1;  g_patch.btn[3].mode = BTN_STEP;

    // 3. Main knob (on LAYERS) full right strips layers down to track 1 (song 0 has 2 tracks).
    g_knob[0] = 4095; update();
    CHECK(g_mods.mute == 0x0002, "MAIN full: mute 0x%04X, want 0x0002", g_mods.mute);
    g_knob[0] = 0; update();
    CHECK(g_mods.mute == 0x0001, "MAIN full left: strips from the BOTTOM, mute 0x%04X, want 0x0001", g_mods.mute);
    neutral();

    // 3b. The other halves (2026-10-27): each one-sided mangle means something below zero too.
    g_patch.base[DEST_START] = -64; g_patch.base[DEST_REVERSE] = -32; g_patch.base[DEST_CRUSH] = -15;
    g_patch.base[DEST_VIBRATO] = -8; g_patch.base[DEST_TREMOLO] = -63; g_patch.base[DEST_CHAOS] = -40;
    g_patch.base[DEST_SOLO] = -2;
    update();
    CHECK(g_mods.sampleStart == 0 && g_mods.sampleEnd == 128, "START -64: start %u end %u, want 0 128",
          g_mods.sampleStart, g_mods.sampleEnd);
    CHECK(g_mods.reverseChance == 0 && g_mods.dropChance == 64, "REVERSE -32: rev %u drop %u, want 0 64",
          g_mods.reverseChance, g_mods.dropChance);
    CHECK(g_crushHold == 1 && g_crushMask == 0x80, "CRUSH -15: hold %u mask 0x%02X, want 1 0x80 (one bit)",
          g_crushHold, g_crushMask);
    CHECK(g_mods.vibrato == -8 && g_mods.tremolo == -63, "TRILL/GATE: vib %d trm %d", g_mods.vibrato, g_mods.tremolo);
    CHECK(g_mods.chaos == 80 && g_mods.chaosFree == 1, "CHAOS -40: amount %u free %u, want 80 1",
          g_mods.chaos, g_mods.chaosFree);
    CHECK(g_mods.mute == 0x0002, "SOLO -2 mutes track 2: mute 0x%04X", g_mods.mute);
    g_patch.base[DEST_CRUSH] = 15; update();
    CHECK(g_crushHold == 12 && g_crushMask == 0xFF, "CRUSH +15: hold %u mask 0x%02X, want 12 0xFF (rate only)",
          g_crushHold, g_crushMask);
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
    g_patch.swUp = SWU_BYPASS; g_switch = 2; update();
    CHECK(g_mods.voiceShift == 0 && g_mods.pitch == 0, "bypass: vsh %d pitch %d", g_mods.voiceShift, g_mods.pitch);
    g_switch = 1; update();
    CHECK(g_mods.pitch == 5 * 256, "BASE transpose 5: pitch %d", g_mods.pitch);
    neutral();

    // 8. Switch DOWN restarts the song (once per press).
    player_tick(); player_tick();
    g_switch = 0; update();
    CHECK(g_plTicks == 0, "switch down: ticks %u, want 0 (restarted)", g_plTicks);
    g_switch = 1; update();

    // 8b. The sound destinations: a BASE value reaches its hook; bypass zeroes it.
    neutral();
    g_patch.base[DEST_RELEASE] = 64; g_patch.base[DEST_VIBRATO] = 10; g_patch.base[DEST_REVERB] = -20;
    update();
    CHECK(g_mods.release == 1024 && g_mods.vibrato == 10 && g_mods.reverb == -20 && g_mods.lfoSpeed == 22,
          "BASE release/vibrato/reverb: %d %u %d lfo %u", g_mods.release, g_mods.vibrato, g_mods.reverb,
          g_mods.lfoSpeed);
    g_patch.swUp = SWU_BYPASS; g_switch = 2; update();
    CHECK(g_mods.release == 0 && g_mods.vibrato == 0 && g_mods.reverb == 0, "bypass clears the sound hooks");
    g_switch = 1; update();
    {   // pl_env: release +full on a DS voice goes to 255, -full to 0; a PSG voice tops out at 7.
        Chan c = { 0 };
        c.release = 200; c.type = 0;
        g_mods.release = 2047; unsigned up = pl_env(&c, PL_ENV_RELEASE);
        g_mods.release = -2047; unsigned dn = pl_env(&c, PL_ENV_RELEASE);
        c.type = 1; c.release = 3; g_mods.release = 2047; unsigned psg = pl_env(&c, PL_ENV_RELEASE);
        c.type = 0; c.attack = 255; g_mods.attack = 2047; unsigned att = pl_env(&c, PL_ENV_ATTACK);
        CHECK(up >= 254 && dn <= 1 && psg == 7 && att <= 2, "pl_env: rel %u/%u psg %u attack %u", up, dn, psg, att);
        update();
    }

    // 8b2. CHAOS: BASE x2, CV /8, and bypass zeroes it.
    neutral();
    g_patch.base[DEST_CHAOS] = 50; update();
    CHECK(g_mods.chaos == 100, "BASE chaos 50: %u, want 100", g_mods.chaos);
    g_patch.swUp = SWU_BYPASS; g_switch = 2; update();
    CHECK(g_mods.chaos == 0, "bypass clears chaos (%u)", g_mods.chaos);
    g_switch = 1;
    neutral();

    // 8c. CLEAR MANGLES (here on B): BASE values and latches go, TEMPO / TRANSPOSE / FINE PITCH stay.
    neutral();
    g_patch.base[DEST_VSHIFT] = 5; g_patch.base[DEST_RELEASE] = 30; g_patch.base[DEST_TRANS] = 7;
    g_patch.base[DEST_TEMPO] = 20; g_patch.base[DEST_PITCH] = 15;
    g_patch.btn[0].dest = DEST_CRUSH;   g_patch.btn[0].mode = BTN_LATCH; g_patch.btn[0].amount = 63;
    g_patch.btn[1].dest = DEST_CLEAR;
    g_perform = 1;
    press(K_A);
    CHECK(g_btnLatch & 1, "A latched CRUSH before the clear");
    press(K_B); update();
    CHECK(g_patch.base[DEST_VSHIFT] == 0 && g_patch.base[DEST_RELEASE] == 0 && !(g_btnLatch & 1)
          && g_crushHold == 1, "CLEAR: vsh %d rel %d latch %u", g_patch.base[DEST_VSHIFT],
          g_patch.base[DEST_RELEASE], g_btnLatch);
    CHECK(g_patch.base[DEST_TRANS] == 7 && g_patch.base[DEST_TEMPO] == 20 && g_patch.base[DEST_PITCH] == 15,
          "CLEAR keeps transpose %d tempo %d pitch %d", g_patch.base[DEST_TRANS], g_patch.base[DEST_TEMPO],
          g_patch.base[DEST_PITCH]);
    mod_default_patch();
    neutral();

    // 8d. Sections. RESTART LOOP on song 1 (a 12-tick intro, then a loop from the second note).
    mod_default_patch();
    neutral();
    mod_song_select(1);
    for (int i = 0; i < 30; i++) player_tick();
    player_restart_loop();
    CHECK(g_tracks[0].cmd == 0x08005200u + 10 && g_tracks[1].cmd == 0x08005300u + 4 && g_plTicks == 12,
          "RESTART LOOP: cmd 0x%08X / 0x%08X ticks %u, want 0x0800520A / 0x08005304, 12",
          g_tracks[0].cmd, g_tracks[1].cmd, g_plTicks);
    mod_song_select(1);
    for (int i = 0; i < 5; i++) player_tick();               // still in the intro
    player_restart_loop();
    CHECK(g_tracks[0].cmd == 0x08005200u + 10 && g_plTicks == 24,
          "RESTART LOOP in the intro: cmd 0x%08X ticks %u (squared to the beat)", g_tracks[0].cmd, g_plTicks);

    // RESTART BAR on song 0, one-bar sections: back to tick 96 with track 1 as it was there.
    mod_song_select(0);
    g_plBarTicks = 96;
    for (int i = 0; i < 96; i++) player_tick();
    uint32_t cmd96 = g_tracks[0].cmd;
    uint8_t wait96 = g_tracks[0].wait;
    for (int i = 0; i < 40; i++) player_tick();
    player_restart_bar();
    CHECK(g_plTicks == 96 && g_tracks[0].cmd == cmd96 && g_tracks[0].wait == wait96,
          "RESTART BAR: ticks %u cmd 0x%08X wait %u, want 96 0x%08X %u", g_plTicks, g_tracks[0].cmd,
          g_tracks[0].wait, cmd96, wait96);
    {   // a TIEd note (gate 0) on the track is released by the jump
        g_ds[0].status = SF_SUSTAIN; g_ds[0].track = 0; g_ds[0].gateTime = 0;
        player_restart_bar();
        CHECK(g_ds[0].status & SF_STOP, "RESTART BAR releases a tied note");
        g_ds[0].status = 0; g_ds[0].track = PL_NONE;
    }
    // The destinations, from a button: B = RESTART BAR, then RESTART LOOP.
    g_patch.btn[1].dest = DEST_RBAR;
    g_perform = 1;
    for (int i = 0; i < 20; i++) player_tick();
    press(K_B);
    CHECK(g_plTicks == 96 || g_plTicks == 97, "B = RESTART BAR: ticks %u, want 96 (97 once its tick has played)", g_plTicks);
    g_patch.btn[1].dest = DEST_RLOOP;
    press(K_B);
    CHECK(g_tracks[0].cmd == 0x08005000u + 6 || g_plTicks <= 1, "B = RESTART LOOP: cmd 0x%08X", g_tracks[0].cmd);
    mod_default_patch();
    neutral();
    mod_song_select(0);

    // 8e. RESTARTS = AT BAR END: a RESTART BAR waits for the section line, then lands on it.
    mod_default_patch();
    neutral();
    mod_song_select(0);
    g_patch.restartQ = 1;
    g_patch.btn[0].dest = DEST_RBAR;
    mod_patch_applied();                                   // one-bar sections
    g_perform = 1;
    for (int i = 0; i < 100; i++) player_tick();           // 4 ticks into bar 2
    press(K_A);
    CHECK(g_plQueued == PL_RESTART_BAR && g_plTicks == 100, "AT BAR END: queued %u, ticks %u (nothing yet)",
          g_plQueued, g_plTicks);
    for (int i = 0; i < 92; i++) player_tick();            // ticks 100..191 played: the bar's last
    CHECK(g_plQueued == PL_RESTART_BAR, "still queued at the bar's last tick (%u)", g_plQueued);
    player_tick();                                         // the line: jump back to 96, play it
    CHECK(g_plQueued == 0 && g_plTicks == 97, "on the line: queued %u ticks %u, want 0 97", g_plQueued, g_plTicks);
    g_patch.restartQ = 0;
    mod_default_patch();
    neutral();
    mod_song_select(0);

    // 8f. Switch UP = REPEAT BAR (the factory setting): the bar playing loops while it is up.
    mod_default_patch();
    neutral();
    mod_song_select(0);
    mod_patch_applied();
    CHECK(g_patch.swUp == SWU_REPEAT, "factory switch up: %u, want REPEAT BAR", g_patch.swUp);
    for (int i = 0; i < 100; i++) player_tick();            // into bar 2 (ticks 96..191)
    g_switch = 2; update();
    for (int i = 0; i < 200; i++) player_tick();
    CHECK(g_plHoldBar && g_plTicks >= 96 && g_plTicks < 192, "switch up 200 ticks: ticks %u, want inside 96..191",
          g_plTicks);
    g_switch = 1; update();
    for (int i = 0; i < 200; i++) player_tick();
    CHECK(!g_plHoldBar && g_plTicks >= 300, "switch back: ticks %u, want it moving on", g_plTicks);
    neutral();

    // 9. A v1 patch is migrated: version 2, factory buttons, zero BASE.
    Patch p;
    mod_default_patch();
    memcpy(&p, &g_patch, sizeof p);
    memset(&p.table, 0, sizeof p - offsetof(Patch, table));
    p.version = 1;
    CHECK(mod_patch_accept(&p) == 1 && p.version == 5, "v1 patch accepted and migrated (version %u)", p.version);
    CHECK(p.btn[0].dest == DEST_RBAR && p.btn[7].dest == DEST_CHAOS, "v1 migration: factory buttons");
    // A v2 patch: BASE in the old place, nothing after the buttons. It must land in base[].
    mod_default_patch();
    memcpy(&p, &g_patch, sizeof p);
    memset(p.base, 0, sizeof p.base);
    p.version = 2; p.baseV2[DEST_VSHIFT] = 9; p.baseV2[DEST_TRANS] = -3;
    CHECK(mod_patch_accept(&p) == 1 && p.version == 5 && p.base[DEST_VSHIFT] == 9 && p.base[DEST_TRANS] == -3
          && p.base[DEST_RELEASE] == 0, "v2 patch migrated: vsh %d trn %d", p.base[DEST_VSHIFT], p.base[DEST_TRANS]);
    // 10. Patch v4: a save records the cart and its tables; boot can install them without a scan.
    mod_default_patch();
    mod_patch_prepare_save();
    CHECK(g_patch.cartTag == rom_cart_tag() && g_patch.cartTag != 0 && g_patch.tableCount == 2
          && g_patch.tableAddr[0] == g_tables[0].songTable && g_patch.tableRate[1] == 18157,
          "save records tag 0x%04X tables %u (0x%08X, rate %u)", g_patch.cartTag, g_patch.tableCount,
          g_patch.tableAddr[0], g_patch.tableRate[1]);
    CHECK(mod_patch_is_this_cart(&g_patch), "the saved patch is this cart's");
    memcpy(&p, &g_patch, sizeof p);
    p.romCheck ^= 1;
    CHECK(!mod_patch_is_this_cart(&p), "another revision (checksum differs) is not this cart");
    {
        int songsBefore = g_songCount;
        CHECK(scan_install(g_patch.tableCount, g_patch.tableAddr, g_patch.tableRate) == 2
              && g_scan.method == SCAN_SAVED && g_songCount == songsBefore && g_tables[1].gameRate == 18157,
              "install: %d tables, method %u, %u songs (scan had %d)", g_tableCount, g_scan.method,
              g_songCount, songsBefore);
        uint32_t bad[2] = { g_patch.tableAddr[0] + 0x40000u, g_patch.tableAddr[1] };   // a stale address
        uint16_t rate[2] = { 13379, 18157 };
        CHECK(scan_install(2, bad, rate) == 1 && g_scan.songTable == g_patch.tableAddr[1],
              "a stale table is dropped, the good one kept (%d)", g_tableCount);
        uint32_t none[1] = { 0x08000100u };
        CHECK(scan_install(1, none, rate) == 0 && g_scan.method == SCAN_NONE, "no good table: scan for real");
        scan_run(NULL);
    }
    memcpy(&p, &g_patch, sizeof p);
    memset(&p.cartTag, 0, sizeof p - offsetof(Patch, cartTag));
    p.version = 3;
    CHECK(mod_patch_accept(&p) == 1 && p.version == 5 && p.tableCount == 0 && !mod_patch_is_this_cart(&p),
          "v3 patch migrated with no cart (version %u)", p.version);
    CHECK(offsetof(Patch, cartTag) == GBA_PATCH_TAG_OFFSET, "cartTag at the host's offset");

    p.magic = 0x464D;                                     // the FM card's magic
    CHECK(mod_patch_accept(&p) == 0, "an FM patch must be rejected");
    CHECK(sizeof(Patch) <= GBA_PATCH_SLOT_BYTES, "Patch %u bytes fits the slot", (unsigned)sizeof(Patch));

    printf("modtest: %d passed, %d failed  (Patch is %u bytes)\n", s_pass, s_fail, (unsigned)sizeof(Patch));
    return s_fail ? 1 : 0;
}
