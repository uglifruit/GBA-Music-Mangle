// engine.h — which sound engine plays the selected song table, behind one set of calls.
//
// The card is one instrument whatever the cart's music uses. The scan tags every song table with
// the engine that reads it (ScanResult.engine); selecting a table selects its engine, and from
// then on mod.c, clock.c and the UI talk to `g_eng`, never to a player directly. A cart can hold
// more than one engine's music; each engine's songs are then their own table(s).
//
// The engines: MP2K (player.c, Nintendo's "Sappy"), GAX (Shin'en), Krawall (XM/S3M modules) and
// MusyX (Factor 5), the last three from engines/ behind a shared transport (eng_core.h).
//
// WHAT STAYS SHARED. Everything the modular and the UI touch is already engine-neutral and is not
// in here: the link, clock.c (INT/EXT, PPQN, reset alignment), mod.c (MAP, BTN, BASE, edges, cue and
// PLAY), the mixer and its output stage (crush, reverb, gain), the pages and the patch slots.
//
// THE VIEW is the MP2K player's own transport state, which every engine fills (player.h):
//   g_tracks[n]   lanes for the PLAY page and the follower: flags & MPT_FLG_EXIST, lastKey (before
//                 the transpose), act, gate, notes
//   g_plTrackCount, g_plTicks (24-PPQN), g_plBeats, g_plLoops, g_plStarts, g_plClockOut
// and reads its section controls: g_plHoldBar, g_plQueued, g_plBarTicks, g_plOutTicks, g_plRepeat,
// g_plOnce. Only one engine plays at a time, so they are never shared between two.
//
// ONE CLOCK. Every engine's tick() is ONE 24-PPQN tick, converted to its native time inside, and
// tempo_i() is its rate in m4a's tempoI units (ticks per frame x 150), which is what clock.c runs on.
#pragma once

#include <stdint.h>
#include "scan.h"

#define ENG_ID_MP2K     0
#define ENG_ID_MUSYX    1
#define ENG_ID_GAX      2
#define ENG_ID_KRAWALL  3
#define ENG_ID_COUNT    4

struct EngCore;

typedef struct {
    const char *name;                  // "MP2K", "MUSYX", "GAX", "KRAWALL": headers, CAL
    uint8_t  id;                       // ENG_ID_*
    uint32_t unsupported;              // bit per destination (mod.h DEST_*) this engine ignores
    int      (*start)(int n);          // play song n of the current list; 0 = unplayable
    void     (*stop)(void);
    int      (*running)(void);
    void     (*tick)(void);            // one 24-PPQN tick
    void     (*update)(void);          // apply g_mods; called every millisecond
    void     (*frame)(void);           // ~59.73 Hz housekeeping (envelopes, the lanes' activity)
    uint32_t (*tempo_i)(void);         // the internal clock rate, tempoI units, mangles included
    void     (*restart_loop)(void);
    void     (*restart_bar)(void);
    void     (*queue_restart)(uint8_t what);       // PL_RESTART_*, at the next section line
    void     (*queue_start)(int n, uint8_t once);  // PLAY song n at the next section line
    void     (*reroll)(void);                      // a new CHAOS pattern
    const struct EngCore *core;        // the engine behind the shared transport; NULL for MP2K
} Engine;

extern const Engine *g_eng;

// Make the engine of id `engineId` current (stopping the one playing first). Unknown ids get MP2K.
void eng_select(uint8_t engineId);
// Select the engine of the selected song table (g_scan.engine).
void eng_follow_table(void);
// The engine of id `engineId`, for names on screen; never NULL.
const Engine *eng_by_id(uint8_t engineId);
// 1 if the current engine honours destination d.
static inline int eng_supports(int d) { return d >= 32 || !((g_eng->unsupported >> d) & 1u); }

// DISCOVERY, for the scan (scan.c). Before a scan, eng_discover_reset(); then each engine's pass
// offers every 64 KB slice to eng_discover_slice(id, ...), and eng_discover_finish(id) turns what
// it found into song tables (scan_add_table). Only playable finds become tables.
void eng_discover_reset(void);
void eng_discover_slice(uint8_t engineId, uint32_t begin, uint32_t end);
void eng_discover_finish(uint8_t engineId);
// The playable songs of another engine's table: the engine's own song numbers into ix, and each
// one's KIND_* into kind. Returns how many (at most max).
int  eng_table_songs(const ScanResult *t, uint16_t *ix, uint8_t *kind, int max);

// KRAWALL'S BANKS. Its discovery cannot prove which sample and instrument bank go with its
// modules; it takes the largest of each, and the CAL page can step through the other candidates.
// The choice is g_krBankWant[] (0 = the largest), which mod.c keeps in step with the patch
// (krSampleBank / krInstrBank). which: 0 sample bank, 1 instrument bank. eng_kr_bank_text writes
// "SAMPLES 02/03 08123456 X029" for the CAL page.
extern uint32_t g_krBankWant[2];
int  eng_kr_bank_count(int which);
void eng_kr_bank_step(int which, int delta);
void eng_kr_bank_text(int which, char *t);
// Take g_krBankWant (a patch was loaded). Returns 1 if the banks in use changed.
int  eng_kr_bank_apply(void);

// One diagnostic line for the CAL page (engine name, then whatever it reports), <= 38 chars.
void eng_diag(char *out);
// Why a cart that names GAX or Krawall got no table of theirs, for the scan's result screen
// ("GAX 2.X: NOT SUPPORTED YET", "KRAWALL: NO SAMPLE BANK M03"). 0 (and out empty) if nothing to say.
int eng_scan_note(char *out);
