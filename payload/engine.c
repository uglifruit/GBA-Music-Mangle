// engine.c — the engine dispatcher: MP2K (player.c) as it is, and the shared transport every other
// engine (eng_musyx.c, eng_gax.c, eng_krawall.c) plays through. See engine.h and eng_core.h.

#include "engine.h"
#include "eng_core.h"
#include "player.h"
#include "mixer.h"
#include "scan.h"
#include "rom.h"
#include "mod.h"
#include "../engines/common/eng_chaos.h"

EngRom eng_cart_rom(void) { EngRom r = { ROM_PTR(ROM_BASE), ROM_BASE, g_romSize }; return r; }

// ---- MP2K: player.c as it is ------------------------------------------------------------------

static int  mp2k_start(int n) { return player_start(scan_song_entry(n)); }
static void mp2k_queue_start(int n, uint8_t once) { player_queue_start(scan_song_entry(n), once); }

static const Engine s_mp2k = {
    "MP2K", ENG_ID_MP2K, 0,
    mp2k_start, player_stop, player_running, player_tick, player_update, player_frame,
    player_tempo_i, player_restart_loop, player_restart_bar, player_queue_restart, mp2k_queue_start,
    player_chaos_reroll, 0,
};

// ---- the shared transport -----------------------------------------------------------------------
// The same section grid, AT BAR END queue, REPEAT BAR and clock out as player_tick(), in the same
// order, on the same globals, so mod.c and the UI cannot tell the engines apart. Keep the two in step.

static uint16_t s_barPos;              // ticks into the section
static uint8_t  s_outPos;              // ticks into the clock out's period
static uint8_t  s_snapValid;
static uint32_t s_snapTicks;           // g_plTicks at the snapshot
static int      s_playing = -1;        // song index playing (for RESET and a queued restart)
static int      s_queuedSong = -1;     // a queued PLAY's song, or -1 for a restart of s_playing
static uint8_t  s_queuedOnce;
static uint32_t s_loopsSeen;           // the engine's loop counter, last seen

#define CORE (g_eng->core)

// A song started from the top: the clock out restarts its phase, Audio Out 2 counts it, and the
// CHAOS pattern starts again.
static void started(int n)
{
    s_playing    = n;
    s_barPos     = 0;
    s_outPos     = 0;
    s_snapValid  = 0;
    g_plQueued   = 0;
    s_queuedSong = -1;
    g_plTicks    = 0;
    s_loopsSeen  = CORE->loops();
    ++g_plStarts;
    eng_chaos_reseed();
}

static void count_loops(void)
{
    uint32_t l = CORE->loops();
    if (l != s_loopsSeen) {
        g_plLoops += (uint8_t)(l - s_loopsSeen);
        s_loopsSeen = l;
        eng_chaos_reseed();                     // a counted loop: the same dice again
    }
}

int tr_start(int n)
{
    if (n < 0 || n >= g_songCount) return 0;
    tr_update();
    if (!CORE->start(n)) { CORE->lanes(); return 0; }
    started(n);
    CORE->lanes();
    return 1;
}

void tr_stop(void)
{
    CORE->stop();
    for (int n = 0; n < PL_TRACKS; ++n) { g_tracks[n].flags = 0; g_tracks[n].gate = 0; }
    g_plTrackCount = 0;
    s_playing = -1;
}

int  tr_running(void) { return CORE->running(); }
void tr_frame(void) { if (CORE->frame && CORE->running()) CORE->frame(); }
void tr_reroll(void) { eng_chaos_reroll(); }
uint32_t tr_tempo_i(void) { return CORE->tempo_i(); }

void tr_update(void)
{
    g_mixReverb = (uint8_t)(g_mods.reverb > 0 ? g_mods.reverb : 0);
    eng_chaos_set(g_mods.chaos, g_mods.chaosFree);
    CORE->sync();
}

void tr_restart_bar(void)
{
    if (!CORE->running() || !s_snapValid) return;
    CORE->snap_restore();
    g_plTicks = s_snapTicks;
    s_barPos  = 0;
    CORE->lanes();
}

void tr_restart_loop(void)
{
    if (CORE->restart_loop && CORE->running()) {
        CORE->restart_loop();
        ++g_plLoops;
        s_loopsSeen = CORE->loops();
        s_snapValid = 0;
        s_barPos = 0;
        eng_chaos_reseed();
        CORE->lanes();
    } else if (s_playing >= 0) tr_start(s_playing);
}

void tr_queue_restart(uint8_t what) { g_plQueued = what; s_queuedSong = -1; }
void tr_queue_start(int n, uint8_t once)
{
    g_plQueued = PL_RESTART_SONG;
    s_queuedSong = n;
    s_queuedOnce = once;
}

void tr_tick(void)
{
    if (!CORE->running()) return;
    if (g_plQueued && s_barPos == 0) {
        uint8_t q = g_plQueued;
        g_plQueued = 0;
        if (q == PL_RESTART_SONG) {
            int n = s_queuedSong >= 0 ? s_queuedSong : s_playing;
            if (s_queuedSong >= 0) g_plOnce = s_queuedOnce;
            if (!tr_start(n)) { tr_stop(); return; }
        }
        else if (q == PL_RESTART_LOOP) tr_restart_loop();
        else                           tr_restart_bar();
    }
    if (g_plHoldBar && s_barPos == 0 && s_snapValid && g_plTicks != s_snapTicks) tr_restart_bar();
    if (g_plTicks % 24 == 0) ++g_plBeats;
    if (s_outPos >= g_plOutTicks) s_outPos = 0;          // CLOCK OUT just changed to a shorter period
    if (s_outPos == 0) ++g_plClockOut;
    if (++s_outPos >= g_plOutTicks) s_outPos = 0;
    if (s_barPos == 0) {
        CORE->snap_take();
        s_snapTicks = g_plTicks;
        s_snapValid = 1;
    }
    if (++s_barPos >= g_plBarTicks) s_barPos = 0;
    if (CORE->tick()) {
        // The song ended and its engine started it again (SONG END = REPEAT): a start from the top.
        count_loops();
        started(s_playing);
    } else {
        count_loops();
        ++g_plTicks;
    }
    CORE->lanes();
}

// ---- selection ----------------------------------------------------------------------------------

const Engine *g_eng = &s_mp2k;

const Engine *eng_by_id(uint8_t id)
{
    switch (id) {
    case ENG_ID_MUSYX:   return &g_engMusyx;
    case ENG_ID_GAX:     return &g_engGax;
    case ENG_ID_KRAWALL: return &g_engKrawall;
    default:             return &s_mp2k;
    }
}

void eng_select(uint8_t id)
{
    const Engine *e = eng_by_id(id);
    if (e == g_eng) return;
    g_eng->stop();
    g_eng = e;
}

void eng_follow_table(void) { eng_select(g_tableCount ? g_scan.engine : ENG_ID_MP2K); }

// ---- discovery dispatch -------------------------------------------------------------------------

void eng_discover_reset(void) { mx_discover_reset(); gx_discover_reset(); krw_discover_reset(); }

void eng_discover_slice(uint8_t id, uint32_t begin, uint32_t end)
{
    if (id == ENG_ID_MUSYX) mx_discover(begin, end);
    else if (id == ENG_ID_GAX) gx_discover(begin, end);
    else if (id == ENG_ID_KRAWALL) krw_discover(begin, end);
}

void eng_discover_finish(uint8_t id)
{
    if (id == ENG_ID_MUSYX) mx_discover_finish();
    else if (id == ENG_ID_GAX) gx_discover_finish();
    else if (id == ENG_ID_KRAWALL) krw_discover_finish();
}

int eng_table_songs(const ScanResult *t, uint16_t *ix, uint8_t *kind, int max)
{
    if (t->engine == ENG_ID_MUSYX) return mx_table_songs(t, ix, kind, max);
    if (t->engine == ENG_ID_GAX) return gx_table_songs(t, ix, kind, max);
    if (t->engine == ENG_ID_KRAWALL) return krw_table_songs(t, ix, kind, max);
    return 0;
}

// ---- diagnostics --------------------------------------------------------------------------------

int eng_put_hex(char *t, int a, uint32_t v, int digits)
{
    for (int i = digits - 1; i >= 0; --i) {
        unsigned d = (v >> (i * 4)) & 15u;
        t[a++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
    }
    t[a] = 0;
    return a;
}

static int has_table(uint8_t id)
{
    for (int i = 0; i < g_tableCount; i++) if (g_tables[i].engine == id) return 1;
    return 0;
}

int eng_scan_note(char *t)
{
    t[0] = 0;
    if ((g_engines & ENG_GAX) && !has_table(ENG_ID_GAX) && gx_scan_note(t)) return 1;
    if ((g_engines & ENG_KRAWALL) && !has_table(ENG_ID_KRAWALL) && krw_scan_note(t)) return 1;
    return 0;
}

void eng_diag(char *t)
{
    int a = 0;
    for (const char *s = g_eng->name; *s; ) t[a++] = *s++;
    t[a] = 0;
    if (g_eng->core) g_eng->core->diag(t, a);
}
