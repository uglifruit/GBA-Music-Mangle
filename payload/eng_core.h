// eng_core.h — inside the dispatcher: what each engine but MP2K provides (eng_musyx.c, eng_gax.c,
// eng_krawall.c), and the shared transport (engine.c) that turns it into an Engine.
//
// The transport is player_tick()'s section logic, once, for all of them: the AT BAR END queue,
// REPEAT BAR, the clock out, the song-start and loop counters, CHAOS reseeds. An engine only says
// how to start, tick and snapshot itself, and fills the lanes.
#pragma once

#include <stdint.h>
#include "engine.h"
#include "../engines/common/eng.h"
#include "../engines/common/eng_mixer.h"

typedef struct EngCore {
    int      (*start)(int n);          // start song n of the current list (g_songIx[n]); 0 = unplayable
    void     (*stop)(void);
    int      (*running)(void);
    int      (*tick)(void);            // one 24-PPQN tick; returns 1 if the song restarted from the top
    uint32_t (*loops)(void);           // the engine's own loop counter
    void     (*snap_take)(void);       // the sequence state at a section line (no voices, no mods)
    void     (*snap_restore)(void);
    void     (*restart_loop)(void);    // NULL: RESTART LOOP restarts the song
    void     (*lanes)(void);           // fill g_tracks and g_plTrackCount
    void     (*sync)(void);            // push g_mods into the engine; called every millisecond
    uint32_t (*tempo_i)(void);
    void     (*diag)(char *t, int a);  // append this engine's CAL text at t[a] (NUL-terminated)
} EngCore;

// The cart, as the engines read it (bus addresses, host-mapped under -DHOST).
EngRom eng_cart_rom(void);
// v as `digits` hex digits at t[a]; returns the new end (NUL-terminated).
int eng_put_hex(char *t, int a, uint32_t v, int digits);

// The transport's entries, shared by every Engine built on an EngCore.
int      tr_start(int n);
void     tr_stop(void);
int      tr_running(void);
void     tr_tick(void);
void     tr_update(void);
void     tr_frame(void);
uint32_t tr_tempo_i(void);
void     tr_restart_loop(void);
void     tr_restart_bar(void);
void     tr_queue_restart(uint8_t what);
void     tr_queue_start(int n, uint8_t once);
void     tr_reroll(void);

// The kind of a song from its shape (scan.h KIND_*): short and ending is an effect.
static inline uint8_t eng_kind(uint32_t ticks24, int loops)
{
    return loops ? KIND_MUSIC : ticks24 < KIND_SFX_TICKS ? KIND_SFX : KIND_JINGLE;
}

// Each engine's discovery and song list (see engine.h for the dispatch).
extern const Engine g_engMusyx, g_engGax, g_engKrawall;
void mx_discover_reset(void);   void mx_discover(uint32_t b, uint32_t e);   void mx_discover_finish(void);
void gx_discover_reset(void);   void gx_discover(uint32_t b, uint32_t e);   void gx_discover_finish(void);
void krw_discover_reset(void);  void krw_discover(uint32_t b, uint32_t e);  void krw_discover_finish(void);
int  mx_table_songs(const ScanResult *t, uint16_t *ix, uint8_t *kind, int max);
int  gx_table_songs(const ScanResult *t, uint16_t *ix, uint8_t *kind, int max);
int  krw_table_songs(const ScanResult *t, uint16_t *ix, uint8_t *kind, int max);
