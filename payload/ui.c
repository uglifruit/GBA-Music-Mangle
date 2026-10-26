// ui.c — the screen. See ui.h for the pages.
//
// THE FM CARD'S UI RULES APPLY (its CLAUDE.md, "Payload UI rules"), and the helpers below exist to
// keep them: every changing value is drawn through field(), which compares the new text with what
// is on screen and repaints only on a change, so nothing is erased and redrawn every frame; bars
// are painted filled-part then empty-part in one overwriting pass; static legends are drawn once
// per page entry; and big clears go through rect_clear() so the link and the clock keep running.

#include "ui.h"
#include "gfx.h"
#include "mod.h"
#include "player.h"
#include "clock.h"
#include "scan.h"
#include "rom.h"
#include "link.h"
#include "audio.h"
#include "mixer.h"

#define KEY_A      (1u << 0)
#define KEY_B      (1u << 1)
#define KEY_SELECT (1u << 2)
#define KEY_START  (1u << 3)
#define KEY_RIGHT  (1u << 4)
#define KEY_LEFT   (1u << 5)
#define KEY_UP     (1u << 6)
#define KEY_DOWN   (1u << 7)
#define KEY_R      (1u << 8)
#define KEY_L      (1u << 9)

#define CW 6                       // character advance
#define RH 10                      // list row height

uint8_t g_page = PAGE_PLAY;

static const char *page_tab[PAGE_COUNT] = { "PLAY", "MAP", "SET", "MEM", "CAL" };
static uint8_t s_editPage = PAGE_MAP;     // the editor page START returns to
static uint8_t s_drawn = 0xFF;            // the page whose statics are on screen
static int     s_cursor[PAGE_COUNT];
static uint8_t s_mapCol;                  // MAP: 0 = destination, 1 = depth
static uint8_t s_memSlot;
static uint8_t s_confirm;                 // SET: FACTORY needs a second press
void (*g_ui_rescan)(void);

// ---- cached fields ------------------------------------------------------------------------------

#define FIELDS   96
#define FIELD_CH 40
static char s_cache[FIELDS][FIELD_CH];

static void invalidate(void)
{
    for (int i = 0; i < FIELDS; i++) s_cache[i][0] = 1;      // never a printable string
}

// Draw `s` at (x, y), padded/clipped to n characters, only if it differs from what field `id` last
// drew. The background box is exactly the field's own width: a clear never reaches past what it owns.
static void field(int id, int x, int y, int n, const char *s, uint16_t fg)
{
    char buf[FIELD_CH];
    int i = 0;
    if (n > FIELD_CH - 2) n = FIELD_CH - 2;
    for (; i < n && s[i]; i++) buf[i] = s[i];
    for (; i < n; i++) buf[i] = ' ';
    buf[n] = 0;
    // The colour is part of the key: the same text in a new colour is a change.
    buf[n + 1] = (char)(fg & 0x7F);
    char *c = s_cache[id];
    int same = 1;
    for (int k = 0; k <= n + 1; k++) if (c[k] != buf[k]) { same = 0; break; }
    if (same) return;
    for (int k = 0; k <= n + 1; k++) c[k] = buf[k];
    rect(x, y, n * CW, 8, COL_BG);
    text(x, y, buf, fg, 1);
}

// Horizontal bar, filled then empty, in one pass; cached by its fill so a still bar costs nothing.
static uint8_t s_barCache[32];
static void bar(int id, int x, int y, int w, int h, int fill, uint16_t col)
{
    if (fill < 0) fill = 0;
    if (fill > w) fill = w;
    if (s_barCache[id] == (uint8_t)(fill + 1)) return;
    s_barCache[id] = (uint8_t)(fill + 1);
    if (fill) rect(x, y, fill, h, col);
    if (fill < w) rect(x + fill, y, w - fill, h, COL_PANEL);
}

static void bars_invalidate(void)
{
    for (int i = 0; i < 32; i++) s_barCache[i] = 0;
}

// ---- tiny formatters (freestanding: no printf) ----------------------------------------------------

static int put_str(char *o, int at, const char *s)
{
    while (*s) o[at++] = *s++;
    o[at] = 0;
    return at;
}

// Zero-padded to `w` digits.
static int put_dec(char *o, int at, uint32_t v, int w)
{
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    while (n < w) tmp[n++] = '0';
    while (n) o[at++] = tmp[--n];
    o[at] = 0;
    return at;
}

static int put_sdec(char *o, int at, int32_t v, int w)
{
    o[at++] = v < 0 ? '-' : '+';
    return put_dec(o, at, (uint32_t)(v < 0 ? -v : v), w);
}

static int put_hex(char *o, int at, uint32_t v, int digits)
{
    static const char H[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--) o[at++] = H[(v >> (4 * i)) & 0xF];
    o[at] = 0;
    return at;
}

static int put_note(char *o, int at, int key)
{
    static const char *nm[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    if (key < 0 || key > 127) return put_str(o, at, "---");
    at = put_str(o, at, nm[key % 12]);
    int oct = key / 12 - 1;
    o[at++] = oct < 0 ? '-' : (char)('0' + oct);
    o[at] = 0;
    return at;
}

// ---- page frame ---------------------------------------------------------------------------------

static void tabs(void)
{
    int x = 2;
    for (int p = 1; p < PAGE_COUNT; p++) {
        int w = text_width(page_tab[p], 1) + 8;
        uint16_t bg = (p == g_page) ? COL_PANEL : COL_BG;
        rect(x, 0, w, 11, bg);
        text(x + 4, 2, page_tab[p], p == g_page ? COL_TITLE : COL_DIM, 1);
        x += w + 2;
    }
    text(SCREEN_W - text_width("START: PLAY", 1) - 2, 2, "START: PLAY", COL_DIM, 1);
    rect(0, 12, SCREEN_W, 1, COL_PANEL);
}

static void enter_page(uint8_t p)
{
    g_page = p;
    if (p != PAGE_PLAY) s_editPage = p;
    s_confirm = 0;
    s_drawn = 0xFF;
}

static void draw_statics(void)
{
    rect_clear(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    invalidate();
    bars_invalidate();
    if (g_page == PAGE_PLAY) {
        char t[40];
        int a = put_str(t, 0, g_romTitle[0] ? g_romTitle : "NO TITLE");
        a = put_str(t, a, "  ");
        put_str(t, a, g_romCode);
        text(4, 2, t, COL_TITLE, 1);
        rect(0, 11, SCREEN_W, 1, COL_PANEL);
        text(4, SCREEN_H - 9, "L/R SONG  A MUTE  B SOLO  START EDIT", COL_DIM, 1);
    } else {
        tabs();
    }
    s_drawn = g_page;
}

// ---- PLAY -------------------------------------------------------------------------------------

static void play_keys(uint16_t k)
{
    int *cur = &s_cursor[PAGE_PLAY];
    int tc = g_plTrackCount ? g_plTrackCount : 1;
    if (k & KEY_L) mod_song_step(-1);
    if (k & KEY_R) mod_song_step(1);
    if (k & KEY_UP)    *cur = (*cur + PL_TRACKS - 1) & 15;
    if (k & KEY_DOWN)  *cur = (*cur + 1) & 15;
    if (k & KEY_LEFT)  *cur = (*cur + 8) & 15;
    if (k & KEY_RIGHT) *cur = (*cur + 8) & 15;
    if (*cur >= tc) *cur = (k & KEY_UP) ? tc - 1 : 0;
    if (k & KEY_A) g_uiMute ^= (uint16_t)(1u << *cur);
    if (k & KEY_B) g_uiSolo = (g_uiSolo == *cur) ? 0xFF : (uint8_t)*cur;
    if (k & KEY_SELECT) mod_restart();
    if (k & KEY_START) enter_page(s_editPage);
}

static void play_draw(void)
{
    char t[40];
    int a;

    // Song, big.
    a = put_str(t, 0, "SONG ");
    if (g_song >= 0) {
        a = put_dec(t, a, (uint32_t)g_song + 1, 3);
        a = put_str(t, a, "/");
        put_dec(t, a, g_songCount, 3);
    } else {
        put_str(t, a, "---");
    }
    {
        // A scale-2 line, redrawn whole but only when its text changes.
        int same = 1;
        for (int i = 0; i < FIELD_CH; i++) {
            if (t[i] != s_cache[0][i]) { same = 0; break; }
            if (!t[i]) break;
        }
        if (!same) {
            for (int i = 0; i < FIELD_CH; i++) { s_cache[0][i] = t[i]; if (!t[i]) break; }
            rect(4, 15, 13 * 12, 14, COL_BG);
            text(4, 15, t, COL_OK, 2);
        }
    }

    // Song table entry and its music player, small, beside it.
    if (g_song >= 0) {
        a = put_str(t, 0, "#");
        a = put_dec(t, a, g_songIx[g_song], 4);
        a = put_str(t, a, " T");
        put_dec(t, a, g_plTrackCount, 2);
    } else t[0] = 0;
    field(1, 170, 15, 11, t, COL_DIM);

    // Clock.
    if (!player_running())          a = put_str(t, 0, "STOPPED     ");
    else if (g_clkExtLive) {
        a = put_str(t, 0, "EXT ");
        a = put_dec(t, a, clk_ppqn[g_clkPpqnIx], 1);
        a = put_str(t, a, "PPQ ");
        a = put_dec(t, a, g_clkBpm, 3);
    } else if (!g_clkRun)           a = put_str(t, 0, "PAUSED      ");
    else {
        a = put_str(t, 0, "INT ");
        a = put_dec(t, a, g_clkBpm, 3);
        a = put_str(t, a, " BPM");
    }
    field(2, 4, 33, 16, t, g_clkExtLive ? COL_OK : COL_MID);

    a = put_str(t, 0, "BAR ");
    a = put_dec(t, a, g_plTicks / 96 + 1, 3);
    a = put_str(t, a, ":");
    put_dec(t, a, (g_plTicks / 24) % 4 + 1, 1);
    field(3, 110, 33, 9, t, COL_MID);
    field(4, 176, 33, 6, g_bypass ? "BYPASS" : "", COL_WAIT);
    bar(0, 220, 33, 14, 7, ((g_plTicks % 24) < 4 && player_running()) ? 14 : 0, COL_TITLE);

    // Tracks: two columns of eight.
    int tc = g_plTrackCount;
    for (int n = 0; n < PL_TRACKS; n++) {
        int x = (n < 8) ? 4 : 122;
        int y = 46 + (n & 7) * 10;
        const Track *tr = &g_tracks[n];
        int exists = n < tc;
        int muted  = (g_mods.mute >> n) & 1;
        int cur    = (n == s_cursor[PAGE_PLAY]);
        a = 0;
        t[a++] = cur ? '>' : ' ';
        a = put_dec(t, a, (uint32_t)n + 1, 2);
        t[a++] = (g_uiSolo == n) ? 'S' : ((g_uiMute >> n) & 1) ? 'M' : muted ? 'm' : ' ';
        t[a] = 0;
        uint16_t col = !exists ? COL_PANEL : cur ? COL_SEL : muted ? COL_DIM : COL_MID;
        field(8 + n, x, y, 4, t, col);
        bar(1 + n, x + 26, y, 54, 7, exists ? (tr->act * 54) >> 8 : 0, muted ? COL_DIM : COL_BAR);
        if (exists && (tr->flags & MPT_FLG_EXIST)) put_note(t, 0, tr->lastKey);
        else put_str(t, 0, "   ");
        field(24 + n, x + 84, y, 3, t, tr->gate ? COL_OK : COL_DIM);
    }

    // The mangles, live.
    a = put_str(t, 0, "VSH");
    a = put_sdec(t, a, g_mods.voiceShift, 2);
    a = put_str(t, a, " TBL");
    a = put_sdec(t, a, g_mods.tableOffset, 4);
    a = put_str(t, a, " SST");
    a = put_dec(t, a, g_mods.sampleStart, 3);
    a = put_str(t, a, " REV");
    put_dec(t, a, g_mods.reverseChance, 3);
    field(40, 4, 128, 38, t, COL_MID);

    a = put_str(t, 0, "SCR");
    a = put_sdec(t, a, g_mods.romScrub, 8);
    a = put_str(t, a, " TRN");
    a = put_sdec(t, a, g_mods.pitch >> 8, 2);
    a = put_str(t, a, " TMP");
    uint32_t tu = g_mods.tempoU;
    a = put_dec(t, a, tu >> 8, 1);
    a = put_str(t, a, ".");
    put_dec(t, a, ((tu & 255) * 100) >> 8, 2);
    field(41, 4, 138, 38, t, COL_MID);
}

// ---- MAP --------------------------------------------------------------------------------------

static void map_keys(uint16_t k)
{
    int *cur = &s_cursor[PAGE_MAP];
    if (k & KEY_UP)   *cur = (*cur + SRC_COUNT - 1) % SRC_COUNT;
    if (k & KEY_DOWN) *cur = (*cur + 1) % SRC_COUNT;
    if (k & KEY_A)    s_mapCol ^= 1;
    int d = (k & KEY_RIGHT) ? 1 : (k & KEY_LEFT) ? -1 : 0;
    if (!d) return;
    ModSlot *m = &g_patch.mod[*cur];
    if (s_mapCol == 0) {
        m->dest = (uint8_t)((m->dest + DEST_COUNT + d) % DEST_COUNT);
    } else {
        int v = m->depth + d;
        if (v < -64) v = -64;
        if (v > 63) v = 63;
        m->depth = (int8_t)v;
    }
}

static void map_draw(void)
{
    char t[40];
    text(4, 16, "SRC  DESTINATION    DEPTH  NOW", COL_DIM, 1);
    for (int s = 0; s < SRC_COUNT; s++) {
        int y = 28 + s * 12;
        int cur = (s == s_cursor[PAGE_MAP]);
        const ModSlot *m = &g_patch.mod[s];
        field(s * 4 + 0, 4, y, 5, src_name[s], cur ? COL_SEL : COL_MID);
        field(s * 4 + 1, 34, y, 13, dest_name[m->dest], cur && s_mapCol == 0 ? COL_TITLE : COL_MID);
        int a = put_sdec(t, 0, m->depth, 2);
        (void)a;
        field(s * 4 + 2, 118, y, 3, t, cur && s_mapCol == 1 ? COL_TITLE : COL_MID);
        // The source's own level, and its trigger state.
        int32_t v = 0;
        switch (s) {
        case SRC_PU2:  v = g_gate ? 2047 : 0; break;
        case SRC_MAIN: case SRC_X: case SRC_Y: v = (int32_t)g_knob[s - SRC_MAIN] - 2048; break;
        default:       v = (int32_t)g_in[s] - 2048; break;
        }
        bar(s, 158, y, 32, 7, (int)((v + 2048) >> 7), COL_BAR);   // 0 V / noon = half
        field(s * 4 + 3, 196, y, 1, (g_srcHigh >> s) & 1 ? "*" : " ", COL_OK);
    }
    text(4, SCREEN_H - 20, "UP/DN ROW  A: DEST/DEPTH", COL_DIM, 1);
    text(4, SCREEN_H - 10, "LEFT/RIGHT CHANGE  L/R PAGE", COL_DIM, 1);
}

// ---- SET --------------------------------------------------------------------------------------

#define SET_CLOCK   0
#define SET_PPQN    1
#define SET_VOICES  2
#define SET_GAIN    3
#define SET_FOLLOW  4
#define SET_REPEAT  5
#define SET_SCRUB   6
#define SET_SWUP    7
#define SET_SWDN    8
#define SET_CART    9
#define SET_RESCAN  10
#define SET_FACTORY 11
#define SET_ITEMS   12

static const char *set_label[SET_ITEMS] = {
    "CLOCK", "PPQN", "VOICES", "GAIN", "CV FOLLOWS", "SONG END", "SCRUB LEN", "SWITCH UP",
    "SWITCH DOWN", "CART SPEED", "RESCAN CART", "FACTORY MAP",
};
static const char *clk_name[3] = { "AUTO", "INTERNAL", "EXTERNAL" };

static int step_wrap(int v, int d, int n) { v += d; while (v < 0) v += n; while (v >= n) v -= n; return v; }
static int step_clamp(int v, int d, int lo, int hi) { v += d; return v < lo ? lo : v > hi ? hi : v; }

static void set_keys(uint16_t k)
{
    int *cur = &s_cursor[PAGE_SET];
    if (k & (KEY_UP | KEY_DOWN)) s_confirm = 0;
    if (k & KEY_UP)   *cur = (*cur + SET_ITEMS - 1) % SET_ITEMS;
    if (k & KEY_DOWN) *cur = (*cur + 1) % SET_ITEMS;
    int d = (k & KEY_RIGHT) ? 1 : (k & KEY_LEFT) ? -1 : 0;
    Patch *p = &g_patch;
    switch (*cur) {
    case SET_CLOCK:  p->clkMode    = (uint8_t)step_wrap(p->clkMode, d, 3); break;
    case SET_PPQN:   p->ppqnIx     = (uint8_t)step_clamp(p->ppqnIx, d, 0, CLK_PPQN_COUNT - 1); break;
    case SET_VOICES: p->maxChans   = (uint8_t)step_clamp(p->maxChans, d, 1, PL_DS_MAX); break;
    case SET_GAIN:   p->gain       = (uint8_t)step_clamp(p->gain, d, 1, 48); break;
    case SET_FOLLOW: p->follow     = (uint8_t)step_wrap(p->follow, d, PL_TRACKS); break;
    case SET_REPEAT: if (d) p->repeat ^= 1; break;
    case SET_SCRUB:  p->scrubLenIx = (uint8_t)step_clamp(p->scrubLenIx, d, 0, SCRUB_LEN_COUNT - 1); break;
    case SET_SWUP:   p->swUp       = (uint8_t)step_wrap(p->swUp, d, SWU_COUNT); break;
    case SET_SWDN:   p->swDown     = (uint8_t)step_wrap(p->swDown, d, SWD_COUNT); break;
    case SET_CART:   if (d) p->cartSafe ^= 1; break;
    case SET_RESCAN: if ((k & KEY_A) && g_ui_rescan) { g_ui_rescan(); return; } break;
    case SET_FACTORY:
        if (k & KEY_A) {
            if (s_confirm) { mod_default_patch(); s_confirm = 0; }
            else s_confirm = 1;
        }
        break;
    }
    mod_patch_applied();
}

static void set_draw(void)
{
    char t[40];
    const Patch *p = &g_patch;
    for (int i = 0; i < SET_ITEMS; i++) {
        int y = 18 + i * RH;
        int cur = (i == s_cursor[PAGE_SET]);
        field(i * 2, 4, y, 12, set_label[i], cur ? COL_SEL : COL_DIM);
        int a = 0;
        t[0] = 0;
        switch (i) {
        case SET_CLOCK:  put_str(t, 0, clk_name[p->clkMode]); break;
        case SET_PPQN:   put_dec(t, 0, clk_ppqn[p->ppqnIx], 1); break;
        case SET_VOICES: put_dec(t, 0, p->maxChans, 1); break;
        case SET_GAIN:
            a = put_dec(t, 0, p->gain / 16, 1);
            a = put_str(t, a, ".");
            put_dec(t, a, ((p->gain & 15) * 100) / 16, 2);
            break;
        case SET_FOLLOW: a = put_str(t, 0, "TRACK "); put_dec(t, a, (uint32_t)p->follow + 1, 1); break;
        case SET_REPEAT: put_str(t, 0, p->repeat ? "REPEAT" : "STOP"); break;
        case SET_SCRUB:  a = put_dec(t, 0, scrub_len[p->scrubLenIx], 1); put_str(t, a, " SAMPLES"); break;
        case SET_SWUP:   put_str(t, 0, swu_name[p->swUp]); break;
        case SET_SWDN:   put_str(t, 0, swd_name[p->swDown]); break;
        case SET_CART:   put_str(t, 0, p->cartSafe ? "SAFE 4/2" : "FAST 3/1"); break;
        case SET_RESCAN: put_str(t, 0, cur ? "PRESS A" : ""); break;
        case SET_FACTORY: put_str(t, 0, cur ? (s_confirm ? "A AGAIN TO RESET" : "PRESS A") : ""); break;
        }
        field(i * 2 + 1, 84, y, 18, t, cur ? COL_TITLE : COL_MID);
    }
}

// ---- MEM --------------------------------------------------------------------------------------

static void mem_keys(uint16_t k)
{
    if (k & KEY_LEFT)  s_memSlot = (uint8_t)((s_memSlot + 15) & 15);
    if (k & KEY_RIGHT) s_memSlot = (uint8_t)((s_memSlot + 1) & 15);
    if (k & KEY_UP)    s_memSlot = (uint8_t)((s_memSlot + 12) & 15);
    if (k & KEY_DOWN)  s_memSlot = (uint8_t)((s_memSlot + 4) & 15);
    if (g_xferState != LINK_XFER_NONE) return;
    if (k & KEY_A) link_begin_load(s_memSlot);
    if (k & KEY_B) {
        mod_patch_prepare_save();
        link_begin_save(s_memSlot, (const uint8_t *)&g_patch, (int)sizeof(Patch));
    }
}

static void mem_draw(void)
{
    char t[40];
    for (int s = 0; s < 16; s++) {
        int x = 8 + (s & 3) * 58, y = 22 + (s >> 2) * 20;
        int used = (g_slotMask >> s) & 1;
        int a = put_str(t, 0, s == s_memSlot ? ">" : " ");
        a = put_dec(t, a, (uint32_t)s + 1, 2);
        put_str(t, a, used ? " USED" : " ----");
        field(s, x, y, 8, t, s == s_memSlot ? COL_SEL : used ? COL_MID : COL_DIM);
    }
    const char *st = "A LOAD   B SAVE";
    uint16_t col = COL_DIM;
    if (g_xferState == LINK_XFER_SAVE)      { st = "SAVING...";  col = COL_WAIT; }
    else if (g_xferState == LINK_XFER_LOAD) { st = "LOADING..."; col = COL_WAIT; }
    else if (g_xferResult == LINK_RESULT_OK)    { st = "DONE";   col = COL_OK; }
    else if (g_xferResult == LINK_RESULT_EMPTY) { st = "SLOT IS EMPTY"; col = COL_WAIT; }
    else if (g_xferResult == LINK_RESULT_BAD)   { st = "BAD DATA - NOT LOADED"; col = COL_FAIL; }
    field(20, 8, 110, 24, st, col);
    bar(0, 8, 122, 224, 4, g_xferState != LINK_XFER_NONE ? (link_xfer_progress() * 224) / 100 : 0, COL_BAR);
    field(21, 8, 134, 36, "A PATCH IS THE MAP + SETTINGS. THE", COL_DIM);
    field(22, 8, 144, 36, "SONG + MUTES RETURN ON THE SAME GAME.", COL_DIM);
}

// A LOAD has landed: check it is ours and apply it (FM card's pattern).
static void mem_poll(void)
{
    if (!g_loadReady) return;
    g_loadReady = 0;
    // Copy out of the byte buffer BEFORE looking at it: g_patchBuf has no alignment guarantee, and
    // the ARM7 reads a misaligned halfword as a rotated word rather than faulting - the magic check
    // would then reject a good patch, or worse, pass a bad one.
    Patch in;
    volatile uint8_t *dst = (volatile uint8_t *)&in;
    for (unsigned i = 0; i < sizeof(Patch); i++) dst[i] = g_patchBuf[i];
    if (!mod_patch_valid(&in)) { g_xferResult = LINK_RESULT_BAD; return; }
    volatile uint8_t *gp = (volatile uint8_t *)&g_patch;
    for (unsigned i = 0; i < sizeof(Patch); i++) gp[i] = dst[i];
    mod_patch_loaded();
}

// ---- CAL --------------------------------------------------------------------------------------

static void cal_draw(void)
{
    char t[40];
    int a, y = 18;
    static const char *meth[3] = { "NONE", "SIGNATURE", "HEURISTIC" };

    a = put_str(t, 0, "SCAN   ");
    put_str(t, a, meth[g_scan.method < 3 ? g_scan.method : 0]);
    field(0, 4, y, 30, t, COL_MID); y += RH;
    a = put_str(t, 0, "TABLE  ");
    a = put_hex(t, a, g_scan.songTable, 8);
    a = put_str(t, a, " ");
    a = put_dec(t, a, g_scan.tableEntries, 1);
    put_str(t, a, " ENT");
    field(1, 4, y, 30, t, COL_MID); y += RH;
    a = put_str(t, 0, "SONGS  ");
    a = put_dec(t, a, g_songCount, 1);
    a = put_str(t, a, "  RATE ");
    put_dec(t, a, g_scan.gameRate, 1);
    field(2, 4, y, 30, t, COL_MID); y += RH;
    a = put_str(t, 0, "ROM    ");
    a = put_dec(t, a, g_romSize >> 20, 1);
    a = put_str(t, a, " MB  ");
    put_str(t, a, g_patch.cartSafe ? "WAIT 4/2" : "WAIT 3/1");
    field(3, 4, y, 30, t, COL_MID); y += RH + 4;

    a = put_str(t, 0, "LINK   ");
    a = put_str(t, a, g_hostSeen ? "HOST OK " : "NO HELLO");
    put_str(t, a, link_irq_alive() ? "  IRQ" : "  POLLED");
    field(4, 4, y, 30, t, g_hostSeen ? COL_OK : COL_WAIT); y += RH;
    a = put_str(t, 0, "RX     ");
    a = put_dec(t, a, g_rx, 1);
    a = put_str(t, a, " BAD ");
    put_dec(t, a, g_streamBad, 1);
    field(5, 4, y, 30, t, COL_MID); y += RH;
    a = put_str(t, 0, "HOST   ");
    a = put_str(t, a, (g_hostCaps & GBA_CAP_AUD_EDGES) ? "AUD EDGES " : "");
    put_str(t, a, (g_hostCaps & GBA_CAP_UP_PULSE) ? "PULSE OUT" : "");
    field(6, 4, y, 30, t, COL_MID); y += RH;
    a = put_str(t, 0, "DSP    ");
    a = put_dec(t, a, g_dspAvg >> 4, 1);
    a = put_str(t, a, "% PEAK ");
    a = put_dec(t, a, g_dspPeak >> 4, 1);
    put_str(t, a, "%");
    field(7, 4, y, 30, t, (g_dspPeak >> 4) > 80 ? COL_WAIT : COL_MID); y += RH;
    a = put_str(t, 0, "VOICES ");
    int on = 0;
    for (int i = 0; i < PL_DS_MAX; i++) if (g_ds[i].status & SF_ON) on++;
    a = put_dec(t, a, (uint32_t)on, 1);
    a = put_str(t, a, "/");
    a = put_dec(t, a, g_plMaxChans, 1);
    a = put_str(t, a, " PSG ");
    for (int i = 0; i < PL_CGB; i++) t[a++] = (g_cgb[i].status & SF_ON) ? (char)('1' + i) : '-';
    t[a] = 0;
    field(8, 4, y, 30, t, COL_MID);
}

// ---- the frame -------------------------------------------------------------------------------

void ui_init(void)
{
    g_page = PAGE_PLAY;
    s_drawn = 0xFF;
}

void ui_frame(void)
{
    mem_poll();
    uint16_t k = mod_take_presses();

    if (g_page == PAGE_PLAY) {
        play_keys(k);
    } else {
        if (k & KEY_START) { enter_page(PAGE_PLAY); k = 0; }
        if (k & KEY_L) { enter_page((uint8_t)(g_page > 1 ? g_page - 1 : PAGE_COUNT - 1)); k = 0; }
        if (k & KEY_R) { enter_page((uint8_t)(g_page < PAGE_COUNT - 1 ? g_page + 1 : 1)); k = 0; }
        switch (g_page) {
        case PAGE_MAP: map_keys(k); break;
        case PAGE_SET: set_keys(k); break;
        case PAGE_MEM: mem_keys(k); break;
        default: break;
        }
    }

    if (s_drawn != g_page) draw_statics();
    switch (g_page) {
    case PAGE_PLAY: play_draw(); break;
    case PAGE_MAP:  map_draw();  break;
    case PAGE_SET:  set_draw();  break;
    case PAGE_MEM:  mem_draw();  break;
    case PAGE_CAL:  cal_draw();  break;
    }

    uint8_t flags = (uint8_t)((player_running() ? 1 : 0) | (g_clkExtLive ? 2 : 0) | (g_bypass ? 4 : 0));
    link_post_status(g_page, g_page == PAGE_PLAY ? GBA_MODE_PLAY : GBA_MODE_EDIT, flags);
}

// ---- SCAN -------------------------------------------------------------------------------------

void ui_scan_begin(void)
{
    rect_clear(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    invalidate();
    bars_invalidate();
    s_drawn = 0xFF;
    text_centre(20, "GBA CART PLAYER", COL_TITLE, 2);
    char t[40];
    int a = put_str(t, 0, g_romTitle[0] ? g_romTitle : "NO CARTRIDGE");
    if (g_romCode[0]) { a = put_str(t, a, "  "); put_str(t, a, g_romCode); }
    text_centre(50, t, g_romSize ? COL_MID : COL_FAIL, 1);
    if (g_romSize) {
        a = put_dec(t, 0, g_romSize >> 20, 1);
        put_str(t, a, " MB");
        text_centre(62, t, COL_DIM, 1);
        text_centre(82, "SCANNING FOR MUSIC", COL_WAIT, 1);
    }
}

void ui_scan_progress(uint32_t done256)
{
    bar(0, 40, 96, 160, 6, (int)((done256 * 160) >> 8), COL_BAR);
}

void ui_scan_result(void)
{
    char t[40];
    rect(0, 80, SCREEN_W, 30, COL_BG);
    if (!g_romSize) {
        text_centre(82, "INSERT A CART, THEN HOLD", COL_WAIT, 1);
        text_centre(94, "START+SELECT WHILE POWERING ON", COL_WAIT, 1);
    } else if (g_scan.method == SCAN_NONE) {
        text_centre(82, "NO MP2K MUSIC FOUND", COL_FAIL, 1);
        text_centre(94, "THIS GAME USES ANOTHER ENGINE", COL_DIM, 1);
    } else {
        int a = put_dec(t, 0, g_songCount, 1);
        put_str(t, a, " SONGS FOUND");
        text_centre(82, t, COL_OK, 1);
    }
}
