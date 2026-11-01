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
#include "engine.h"
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
// Cue song n and PLAY it (since v6, choosing a song only cues it).
static void play(int n) { mod_song_select(n); mod_play(); }

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

// ---- the other engines --------------------------------------------------------------------------

static int load_cart(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    mod_stop();
    size_t len = fread(s_rom, 1, ROM_MAX, f);
    fclose(f);
    for (size_t o = (len + 1) & ~(size_t)1; o + 1 < ROM_MAX + 64; o += 2) {
        s_rom[o] = (uint8_t)(o >> 1);
        s_rom[o + 1] = (uint8_t)(o >> 9);
    }
    rom_init();
    scan_run(NULL, 0);
    mod_default_patch();
    mod_patch_applied();
    neutral();
    return 1;
}

// The first sounding mixer channel, ticking up to 96 times to find one; -1 if none.
static int sounding(void)
{
    for (int k = 0; k < 96; k++) {
        for (int i = 0; i < MIX_CHANNELS; i++) if (g_mix[i].active && !mixer_done(i)) return i;
        g_eng->tick();
    }
    return -1;
}

static void engine_checks(const char *path, uint8_t engine)
{
    if (!load_cart(path)) return;
    const char *nm = eng_by_id(engine)->name;
    mod_set_list(0, LIST_ALL);
    CHECK(g_scan.engine == engine && g_eng->id == engine && g_songCount >= 1,
          "%s cart: table engine %u, engine %s, %u songs", nm, g_scan.engine, g_eng->name, g_songCount);
    CHECK(!g_eng->running() && g_song == 0, "%s: cued, silent (%d)", nm, g_eng->running());
    uint8_t starts = g_plStarts, clocks;
    play(0);
    CHECK(g_eng->running() && g_songPlaying == 0 && (uint8_t)(g_plStarts - starts) == 1 && g_plTrackCount > 0,
          "%s PLAY: running %d playing %d starts +%u lanes %u", nm, g_eng->running(), g_songPlaying,
          (uint8_t)(g_plStarts - starts), g_plTrackCount);
    clocks = g_plClockOut;
    for (int i = 0; i < 48; i++) g_eng->tick();
    CHECK((uint8_t)(g_plClockOut - clocks) == 8 && g_plTicks == 48,
          "%s clock out at 4 PPQN: %u clocks in 48 ticks (ticks %u)", nm, (uint8_t)(g_plClockOut - clocks), g_plTicks);
    // Transpose an octave: every sounding voice's step doubles, at once.
    int ch = sounding();
    uint32_t step0 = ch >= 0 ? g_mix[ch].step : 0;
    g_patch.base[DEST_TRANS] = 12; update(); g_eng->update();
    CHECK(ch >= 0 && g_mix[ch].step >= step0 * 2 - 4 && g_mix[ch].step <= step0 * 2 + 4,
          "%s transpose +12: channel %d step %u -> %u", nm, ch, step0, ch >= 0 ? g_mix[ch].step : 0);
    CHECK(!eng_supports(DEST_ATTACK) && eng_supports(DEST_CHAOS) && eng_supports(DEST_REROLL)
          && eng_supports(DEST_TRANS) && eng_supports(DEST_RBAR),
          "%s: ADSR marked N/A; CHAOS, REROLL, transpose and RESTART BAR not", nm);
    // REPEAT BAR (the factory switch up): the bar playing loops, and the transpose stays.
    while (g_plTicks < 100) g_eng->tick();                           // into bar 2
    g_switch = 2; update();
    for (int i = 0; i < 200; i++) g_eng->tick();
    CHECK(g_plHoldBar && g_plTicks >= 96 && g_plTicks < 192, "%s REPEAT BAR: ticks %u, want 96..191", nm, g_plTicks);
    CHECK(g_mods.pitch == 12 * 256, "%s transpose survives REPEAT BAR (%d)", nm, g_mods.pitch);
    g_switch = 1; update();
    for (int i = 0; i < 200; i++) g_eng->tick();
    CHECK(!g_plHoldBar && g_plTicks >= 300, "%s switch back: ticks %u, moving on", nm, g_plTicks);
    g_patch.base[DEST_TRANS] = 0; update();
    // AT BAR END: a RESTART BAR waits for the line, then jumps back to the one before and plays it.
    g_patch.restartQ = 1;
    g_patch.btn[0].dest = DEST_RBAR;
    g_perform = 1;
    while (g_plTicks % 96 != 4) g_eng->tick();
    uint32_t lineAt = g_plTicks - 4;
    press(K_A);
    CHECK(g_plQueued == PL_RESTART_BAR, "%s AT BAR END: queued %u", nm, g_plQueued);
    for (int i = 0; i < 92; i++) g_eng->tick();
    CHECK(g_plQueued == PL_RESTART_BAR, "%s: still queued at the bar's last tick (%u)", nm, g_plQueued);
    g_eng->tick();
    CHECK(g_plQueued == 0 && g_plTicks == lineAt + 1, "%s on the line: queued %u ticks %u, want 0 %u",
          nm, g_plQueued, g_plTicks, lineAt + 1);
    g_patch.restartQ = 0;
    g_perform = 0;
    // CHAOS (every engine since 2026-11-01): the first note of a fresh start changes under some
    // pattern, and the same pattern deals it the same dice on the next start (it repeats).
    {
        uint32_t plain[3] = { 0, 0, 0 }, a[3] = { 0, 0, 0 }, b[3] = { 0, 0, 0 };
        g_patch.base[DEST_CHAOS] = 0; update();
        play(0); g_eng->tick(); g_eng->update();
        int c0 = sounding();
        if (c0 >= 0) { plain[0] = g_mix[c0].step; plain[1] = g_mix[c0].volL; plain[2] = g_mix[c0].volR; }
        g_patch.base[DEST_CHAOS] = 100; update();                     // x2 in BASE units: 200 of 255
        int differs = 0;
        for (int r = 0; r < 12 && !differs; r++) {
            g_eng->reroll();
            play(0); g_eng->tick(); g_eng->update();
            int c1 = sounding();
            if (c1 < 0) continue;
            a[0] = g_mix[c1].step; a[1] = g_mix[c1].volL; a[2] = g_mix[c1].volR;
            differs = a[0] != plain[0] || a[1] != plain[1] || a[2] != plain[2];
        }
        play(0); g_eng->tick(); g_eng->update();
        int c2 = sounding();
        if (c2 >= 0) { b[0] = g_mix[c2].step; b[1] = g_mix[c2].volL; b[2] = g_mix[c2].volR; }
        CHECK(c0 >= 0 && differs, "%s CHAOS changes a note (step %u vol %u/%u -> %u %u/%u)", nm,
              plain[0], plain[1], plain[2], a[0], a[1], a[2]);
        CHECK(a[0] == b[0] && a[1] == b[1] && a[2] == b[2], "%s CHAOS repeats with its seed (%u %u/%u, %u %u/%u)",
              nm, a[0], a[1], a[2], b[0], b[1], b[2]);
        g_patch.base[DEST_CHAOS] = 0; update();
    }
    // STOP clears the lanes.
    mod_stop();
    CHECK(!g_eng->running() && g_songPlaying == -1 && g_plTrackCount == 0, "%s STOP: running %d lanes %u", nm,
          g_eng->running(), g_plTrackCount);
}

// A cart with MP2K and GAX: a table each (the MP2K test cart has two of its own), and switching
// table switches engine.
static void mixed_checks(void)
{
    int gax = -1;
    for (int i = 0; i < g_tableCount; i++) if (g_tables[i].engine == ENG_ID_GAX) gax = i;
    CHECK(g_tableCount == 3 && g_tables[0].engine == ENG_ID_MP2K && gax == 2 && (g_engines & ENG_GAX),
          "mixed cart: %u tables, GAX table %d", g_tableCount, gax);
    mod_set_list(gax < 0 ? 0 : gax, LIST_ALL);
    CHECK(g_eng->id == ENG_ID_GAX && g_songCount >= 1, "mixed: table %d is GAX (%s, %u songs)", gax, g_eng->name, g_songCount);
    play(0);
    CHECK(g_eng->running(), "mixed: the GAX song plays");
    mod_set_list(0, LIST_MUSIC);
    CHECK(g_eng->id == ENG_ID_MP2K && !g_eng->running() && g_song == 0, "mixed: back to MP2K, stopped, cued");
    play(0);
    CHECK(g_eng->running() && g_plHeader != 0, "mixed: the MP2K song plays");
    mod_stop();
}

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
    scan_run(NULL, 0);
    player_init();
    mod_init();
    keys(0);
    play(0);

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
    play(0);

    // 7. The switch UP bypasses CV, buttons and BASE alike.
    neutral();
    g_patch.base[DEST_TRANS] = 5;
    g_in[0] = 4095;
    g_patch.swUp = SWU_BYPASS; g_switch = 2; update();
    CHECK(g_mods.voiceShift == 0 && g_mods.pitch == 0, "bypass: vsh %d pitch %d", g_mods.voiceShift, g_mods.pitch);
    g_switch = 1; update();
    CHECK(g_mods.pitch == 5 * 256, "BASE transpose 5: pitch %d", g_mods.pitch);
    neutral();

    // 8. Switch DOWN (factory: PLAY, on the cue playing) restarts the song, once per press.
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
    play(1);
    for (int i = 0; i < 30; i++) player_tick();
    player_restart_loop();
    CHECK(g_tracks[0].cmd == 0x08005200u + 10 && g_tracks[1].cmd == 0x08005300u + 4 && g_plTicks == 12,
          "RESTART LOOP: cmd 0x%08X / 0x%08X ticks %u, want 0x0800520A / 0x08005304, 12",
          g_tracks[0].cmd, g_tracks[1].cmd, g_plTicks);
    play(1);
    for (int i = 0; i < 5; i++) player_tick();               // still in the intro
    player_restart_loop();
    CHECK(g_tracks[0].cmd == 0x08005200u + 10 && g_plTicks == 24,
          "RESTART LOOP in the intro: cmd 0x%08X ticks %u (squared to the beat)", g_tracks[0].cmd, g_plTicks);

    // RESTART BAR on song 0, one-bar sections: back to tick 96 with track 1 as it was there.
    play(0);
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
    play(0);

    // 8e. RESTARTS = AT BAR END: a RESTART BAR waits for the section line, then lands on it.
    mod_default_patch();
    neutral();
    play(0);
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
    play(0);

    // 8f. Switch UP = REPEAT BAR (the factory setting): the bar playing loops while it is up.
    mod_default_patch();
    neutral();
    play(0);
    mod_patch_applied();
    CHECK(g_patch.swUp == SWU_REPEAT, "factory switch up: %u, want REPEAT BAR", g_patch.swUp);
    for (int i = 0; i < 100; i++) player_tick();            // into bar 2 (ticks 96..191)
    g_switch = 2; update();
    for (int i = 0; i < 200; i++) player_tick();
    CHECK(g_plHoldBar && g_plTicks >= 96 && g_plTicks < 192, "switch up 200 ticks: ticks %u, want inside 96..191",
          g_plTicks);
    // A transpose made while the bar repeats survives the jump back (the copy holds the old one).
    g_patch.base[DEST_TRANS] = 12; update(); player_update();
    for (int i = 0; i < 100; i++) player_tick();
    CHECK(g_tracks[0].keyShiftX == 12 && g_tracks[1].keyShiftX == 12,
          "REPEAT BAR keeps a new transpose: keyShiftX %d %d, want 12", g_tracks[0].keyShiftX, g_tracks[1].keyShiftX);
    g_patch.base[DEST_TRANS] = 0; update(); player_update();
    g_switch = 1; update();
    for (int i = 0; i < 200; i++) player_tick();
    CHECK(!g_plHoldBar && g_plTicks >= 300, "switch back: ticks %u, want it moving on", g_plTicks);
    neutral();

    // 8g. Cue, then PLAY (2026-10-29): choosing a song never plays it.
    mod_default_patch();
    neutral();
    mod_patch_applied();
    CHECK(g_patch.mod[SRC_AU1].dest == DEST_PLAY && g_patch.swDown == SWD_PLAY,
          "factory: AU1 %u, switch down %u, want PLAY", g_patch.mod[SRC_AU1].dest, g_patch.swDown);
    mod_stop();
    mod_song_select(1);
    CHECK(!player_running() && g_song == 1 && g_songPlaying == -1, "select cues: running %d cue %d playing %d",
          player_running(), g_song, g_songPlaying);
    g_perform = 1;
    g_patch.btn[1].dest = DEST_NEXT; g_patch.btn[1].mode = BTN_HOLD;
    press(K_B);
    CHECK(!player_running() && g_song == 2 % g_songCount, "NEXT cues: running %d cue %d", player_running(), g_song);
    g_patch.btn[1].dest = DEST_PLAY;
    press(K_B);
    CHECK(player_running() && g_songPlaying == g_song, "PLAY plays the cue: playing %d cue %d", g_songPlaying, g_song);
    for (int i = 0; i < 30; i++) player_tick();
    press(K_B);
    CHECK(g_plTicks == 0, "PLAY on the cue playing restarts it: ticks %u", g_plTicks);
    {
        int pl = g_songPlaying;
        mod_song_step(1);
        CHECK(player_running() && g_songPlaying == pl && g_song != pl, "a new cue leaves the music playing");
    }
    g_patch.btn[1].dest = DEST_STOP;
    press(K_B);
    CHECK(!player_running() && g_songPlaying == -1, "STOP: running %d playing %d", player_running(), g_songPlaying);
    g_patch.btn[1].dest = DEST_RESET;
    press(K_B);
    CHECK(player_running() && g_songPlaying == g_song, "RESET while stopped plays the cue (%d)", g_songPlaying);
    {   // SONG CV moves the cue only
        int cue = g_song, pl = g_songPlaying;
        g_patch.mod[SRC_CV1].dest = DEST_SONG; g_patch.mod[SRC_CV1].depth = 63;
        g_in[0] = 2048 + 600; update();
        CHECK(g_song != cue && g_songPlaying == pl, "SONG CV cues: cue %d -> %d, playing %d (was %d)",
              cue, g_song, g_songPlaying, pl);
        g_in[0] = 2048; update();
        g_patch.mod[SRC_CV1].dest = DEST_VSHIFT;
    }
    {   // a STEP on SONG moves the cue by its amount (SONG has no BASE value)
        int cue = g_song;
        g_patch.btn[1].dest = DEST_SONG; g_patch.btn[1].mode = BTN_STEP; g_patch.btn[1].amount = 2;
        press(K_B);
        CHECK(g_song == (cue + 2) % g_songCount, "STEP 2 on SONG: cue %d -> %d", cue, g_song);
        g_patch.btn[1].mode = BTN_HOLD; g_patch.btn[1].amount = 0;
    }
    // AT BAR END: a PLAY waits for the section line while music plays, and lands on it...
    play(0);
    g_patch.restartQ = 1;
    mod_patch_applied();                                   // one-bar sections
    for (int i = 0; i < 10; i++) player_tick();
    mod_song_select(1);
    g_patch.btn[1].dest = DEST_PLAY;
    press(K_B);
    CHECK(g_plQueued == PL_RESTART_SONG && g_songPlaying == 0, "AT BAR END: PLAY queued %u, playing %d",
          g_plQueued, g_songPlaying);
    for (int i = 0; i < 86; i++) player_tick();            // ticks 10..95
    CHECK(g_songPlaying == 0 && g_plQueued, "still the old song before the line");
    player_tick(); update();                               // the line: song 1 starts and plays tick 0
    CHECK(g_plQueued == 0 && g_songPlaying == 1 && g_plTicks == 1, "on the line: queued %u playing %d ticks %u",
          g_plQueued, g_songPlaying, g_plTicks);
    // ...but starts at once when nothing is playing (a stopped player has no grid).
    mod_stop();
    mod_song_select(0);
    press(K_B);
    CHECK(player_running() && g_songPlaying == 0 && !g_plQueued, "PLAY from stopped is immediate even AT BAR END");
    g_patch.restartQ = 0;
    // The SFX list: switching stops the music and cues the first effect; an effect plays ONCE.
    mod_set_list(g_tableIx, LIST_SFX);
    CHECK(!player_running() && g_song == 0 && g_listMode == LIST_SFX && g_songPlayer[0] != 0,
          "SFX list: running %d cue %d player %u", player_running(), g_song, g_songPlayer[0]);
    mod_play();
    CHECK(player_running() && g_plOnce && g_plRepeat, "the effect plays, once (SONG END is REPEAT)");
    for (int i = 0; i < 200; i++) { player_tick(); update(); }
    CHECK(!player_running() && g_songPlaying == -1, "the looping effect stopped after one pass (running %d)",
          player_running());
    press(K_B);
    CHECK(player_running(), "PLAY fires it again");
    mod_set_list(g_tableIx, LIST_MUSIC);
    play(0);
    for (int i = 0; i < 200; i++) { player_tick(); update(); }
    CHECK(player_running() && !g_plOnce, "a music song keeps looping (running %d)", player_running());
    mod_default_patch();
    neutral();
    play(0);

    // 8h. The outputs: Audio Out 1 a clock at SET > CLOCK OUT's PPQN, from the sequencer's ticks.
    mod_default_patch();
    neutral();
    mod_patch_applied();
    CHECK(g_patch.clkOutIx == CLK_OUT_DEFAULT && g_plOutTicks == 6 && g_patch.out2Mode == OUT2_LOOP,
          "factory outputs: clock out ix %u (%u ticks), out 2 %u", g_patch.clkOutIx, g_plOutTicks, g_patch.out2Mode);
    {
        uint8_t starts = g_plStarts;
        play(0);
        CHECK((uint8_t)(g_plStarts - starts) == 1, "PLAY counts a song start (%u)", (uint8_t)(g_plStarts - starts));
        uint8_t c0 = g_plClockOut;
        for (int i = 0; i < 48; i++) player_tick();
        CHECK((uint8_t)(g_plClockOut - c0) == 8, "4 PPQN: 48 ticks give %u clocks, want 8",
              (uint8_t)(g_plClockOut - c0));
        // RESTART BAR jumps the song but not the clock: ticks keep coming at the same spacing.
        g_plBarTicks = 96;
        for (int i = 0; i < 99; i++) player_tick();             // tick 147: 3 into a period
        c0 = g_plClockOut;
        player_restart_bar();
        for (int i = 0; i < 3; i++) player_tick();              // the rest of the period: no clock
        uint8_t mid = (uint8_t)(g_plClockOut - c0);
        player_tick();                                          // the next period's first tick
        CHECK(mid == 0 && (uint8_t)(g_plClockOut - c0) == 1, "RESTART BAR keeps the clock's phase: %u then %u, want 0 1",
              mid, (uint8_t)(g_plClockOut - c0));
        // A song start restarts the phase: its tick 0 is a clock.
        for (int i = 0; i < 2; i++) player_tick();
        c0 = g_plClockOut;
        mod_restart();
        player_tick();
        CHECK((uint8_t)(g_plClockOut - c0) == 1, "a restart's tick 0 is a clock (%u)", (uint8_t)(g_plClockOut - c0));
        g_patch.clkOutIx = CLK_PPQN_COUNT - 1;                  // 24 PPQN
        mod_patch_applied();
        c0 = g_plClockOut;
        for (int i = 0; i < 10; i++) player_tick();
        CHECK((uint8_t)(g_plClockOut - c0) == 10, "24 PPQN: a clock every tick (%u in 10)", (uint8_t)(g_plClockOut - c0));
    }
    {   // a v5 patch gets the factory outputs (those bytes were padding)
        Patch q;
        mod_default_patch();
        memcpy(&q, &g_patch, sizeof q);
        q.version = 5; q.clkOutIx = 0; q.out2Mode = 0;
        CHECK(mod_patch_accept(&q) == 1 && q.version == PATCH_VERSION && q.clkOutIx == CLK_OUT_DEFAULT && q.out2Mode == OUT2_LOOP,
              "v5 -> v6: clock out %u out 2 %u", q.clkOutIx, q.out2Mode);
    }
    mod_default_patch();
    neutral();
    mod_patch_applied();
    play(0);

    // 9. A v1 patch is migrated: version 2, factory buttons, zero BASE.
    Patch p;
    mod_default_patch();
    memcpy(&p, &g_patch, sizeof p);
    memset(&p.table, 0, sizeof p - offsetof(Patch, table));
    p.version = 1;
    CHECK(mod_patch_accept(&p) == 1 && p.version == PATCH_VERSION, "v1 patch accepted and migrated (version %u)", p.version);
    CHECK(p.btn[0].dest == DEST_RBAR && p.btn[7].dest == DEST_CHAOS, "v1 migration: factory buttons");
    // A v2 patch: BASE in the old place, nothing after the buttons. It must land in base[].
    mod_default_patch();
    memcpy(&p, &g_patch, sizeof p);
    memset(p.base, 0, sizeof p.base);
    p.version = 2; p.baseV2[DEST_VSHIFT] = 9; p.baseV2[DEST_TRANS] = -3;
    CHECK(mod_patch_accept(&p) == 1 && p.version == PATCH_VERSION && p.base[DEST_VSHIFT] == 9 && p.base[DEST_TRANS] == -3
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
        CHECK(g_patch.tableEngine[0] == ENG_ID_MP2K && g_patch.tableEngine[1] == ENG_ID_MP2K,
              "save records the tables' engines (%u %u)", g_patch.tableEngine[0], g_patch.tableEngine[1]);
        CHECK(scan_install(g_patch.tableCount, g_patch.tableAddr, g_patch.tableRate, g_patch.tableEngine) == 2
              && g_scan.method == SCAN_SAVED && g_songCount == songsBefore && g_tables[1].gameRate == 18157,
              "install: %d tables, method %u, %u songs (scan had %d)", g_tableCount, g_scan.method,
              g_songCount, songsBefore);
        uint32_t bad[2] = { g_patch.tableAddr[0] + 0x40000u, g_patch.tableAddr[1] };   // a stale address
        uint16_t rate[2] = { 13379, 18157 };
        uint8_t mp2k[2] = { ENG_ID_MP2K, ENG_ID_MP2K }, musyx[2] = { ENG_ID_MUSYX, ENG_ID_MUSYX };
        CHECK(scan_install(2, g_patch.tableAddr, rate, musyx) == 0,
              "an MP2K table saved as MUSYX holds no MusyX song: dropped (%d)", g_tableCount);
        CHECK(scan_install(2, bad, rate, mp2k) == 1 && g_scan.songTable == g_patch.tableAddr[1],
              "a stale table is dropped, the good one kept (%d)", g_tableCount);
        uint32_t none[1] = { 0x08000100u };
        CHECK(scan_install(1, none, rate, mp2k) == 0 && g_scan.method == SCAN_NONE, "no good table: scan for real");
        scan_run(NULL, 0);
    }
    memcpy(&p, &g_patch, sizeof p);
    memset(&p.cartTag, 0, sizeof p - offsetof(Patch, cartTag));
    p.version = 3;
    CHECK(mod_patch_accept(&p) == 1 && p.version == PATCH_VERSION && p.tableCount == 0 && !mod_patch_is_this_cart(&p),
          "v3 patch migrated with no cart (version %u)", p.version);
    CHECK(offsetof(Patch, cartTag) == GBA_PATCH_TAG_OFFSET, "cartTag at the host's offset");
    {   // v6 -> v7: the table engines were past a v6 patch's end, so whatever was there is MP2K now
        Patch q;
        mod_default_patch();
        mod_patch_prepare_save();
        memcpy(&q, &g_patch, sizeof q);
        q.version = 6;
        memset(q.tableEngine, 0xA5, sizeof q.tableEngine);
        CHECK(mod_patch_accept(&q) == 1 && q.version == PATCH_VERSION && q.tableEngine[0] == ENG_ID_MP2K
              && q.tableEngine[7] == ENG_ID_MP2K && q.tableCount == 2,
              "v6 -> v7: engines %u..%u, tables %u", q.tableEngine[0], q.tableEngine[7], q.tableCount);
        q.version = 7;                                    // v7 -> v8: Krawall's banks were past its end
        q.krSampleBank = 0xA5A5A5A5u; q.krInstrBank = 0x5A5A5A5Au;
        CHECK(mod_patch_accept(&q) == 1 && q.version == 8 && q.krSampleBank == 0 && q.krInstrBank == 0,
              "v7 -> v8: Krawall banks %08X %08X, want 0 0 (the largest)", q.krSampleBank, q.krInstrBank);
        q.tableEngine[1] = 0x7F;                          // a v7 slot naming no known engine
        CHECK(mod_patch_accept(&q) == 1 && q.tableCount == 0, "an unknown engine: no tables (%u)", q.tableCount);
    }

    p.magic = 0x464D;                                     // the FM card's magic
    CHECK(mod_patch_accept(&p) == 0, "an FM patch must be rejected");
    CHECK(sizeof(Patch) <= GBA_PATCH_SLOT_BYTES, "Patch %u bytes fits the slot", (unsigned)sizeof(Patch));

    // 12. The other engines (2026-10-29, 2026-11-01): each cart after the first is scanned, its table
    //     selects its engine, and the transport, the sections, the outputs and CHAOS work through
    //     engine.c exactly as they do on MP2K. modtest TEST.gba MUSYX.gba GAX.gba KRAWALL.gba MIXED.gba
    //     (make_demo.py's MusyX cart, mkengines.py's others).
    // 11b. Song or effect, by content (2026-11-01): mktestrom's melodies loop (MUSIC); its PSG effect
    //      is short and loops on an effects player (SFX, MP2K's one use of the player number).
    {
        mod_default_patch();
        mod_patch_applied();
        mod_set_list(0, LIST_ALL);
        int k0 = -1, k1 = -1, k2 = -1;
        for (int n = 0; n < g_songCount; n++) {
            if (g_songIx[n] == 0) k0 = g_songKind[n];
            if (g_songIx[n] == 1) k1 = g_songKind[n];
            if (g_songIx[n] == 2) k2 = g_songKind[n];
        }
        CHECK(k0 == KIND_MUSIC && k1 == KIND_MUSIC && k2 == KIND_SFX, "kinds: entries 0 1 2 are %d %d %d, want %d %d %d",
              k0, k1, k2, KIND_MUSIC, KIND_MUSIC, KIND_SFX);
        uint32_t ticks;
        CHECK(player_song_shape(g_scan.songTable, &ticks) == 1 && ticks == 48,
              "song 0's shape: loops, %u ticks to its loop (want 48)", ticks);
        mod_set_list(0, LIST_SFX);
        int has2 = 0;
        for (int n = 0; n < g_songCount; n++) if (g_songIx[n] == 2) has2 = 1;
        CHECK(has2 && g_songKind[0] == KIND_SFX, "SFX list holds the effect (%u items)", g_songCount);
        mod_set_list(0, LIST_MUSIC);
    }

    static const uint8_t want[3] = { ENG_ID_MUSYX, ENG_ID_GAX, ENG_ID_KRAWALL };
    for (int c = 0; c < 3 && argc > 2 + c; c++) engine_checks(argv[2 + c], want[c]);
    if (argc > 5 && load_cart(argv[5])) mixed_checks();

    printf("modtest: %d passed, %d failed  (Patch is %u bytes)\n", s_pass, s_fail, (unsigned)sizeof(Patch));
    return s_fail ? 1 : 0;
}
