// scan.h — find the MP2K song table(s) in the inserted cartridge, and list their playable songs.
//
// Two methods, in order:
//
//   SIGNATURE  saptapper's: the library's SelectSong (m4aSongNumStart) Thumb routine, matched
//              loosely (4-aligned, at most 8 of its 30 bytes may differ - literal-pool offsets move
//              between builds). The song table's address sits in its literal pool, 40 bytes in.
//   HEURISTIC  agbplay-style, when the signature finds no table with a playable song (an unusual
//              library build): the longest runs of consecutive entries that each point at a
//              believable song header.
//
// Neither is allowed to trust anything: a believable header is one whose track count, voicegroup
// pointer and every track pointer check out against the image bounds.
//
// MORE THAN ONE TABLE. A cart can carry more than one copy of the library - a two-games-in-one
// compilation has one per game - so the signature pass collects EVERY match, not just the first,
// and the heuristic keeps the longest few runs. A table with no playable song is dropped. g_scan is
// the table currently selected; scan_select() switches.
//
// SOUND EFFECTS ARE SONGS. MP2K plays effects through the same song table, on a different music
// player (the entry's `ms` field); player 0 is, by the library's own convention, the music. The
// song list can be filtered to music, effects, or both.
#pragma once

#include <stdint.h>

#define SCAN_NONE      0
#define SCAN_SIGNATURE 1
#define SCAN_HEURISTIC 2
#define SCAN_SAVED     3    // not scanned: the tables came from a slot saved on this cart (main.c)

#define SCAN_MAX_SONGS  2048
#define SCAN_MAX_TABLES 8

#define LIST_ALL   0
#define LIST_MUSIC 1        // music player 0
#define LIST_SFX   2        // any other player
#define LIST_COUNT 3

typedef struct {
    uint8_t  method;        // SCAN_*
    uint32_t selectSong;    // address of the matched routine (SIGNATURE only), else 0
    uint32_t songTable;     // address of entry 0, or 0
    uint16_t tableEntries;  // entries before the first non-pointer (saptapper's count)
    uint32_t soundMode;     // the game's m4aSoundMode word if found, else 0
    uint16_t gameRate;      // the game's mixing rate in Hz (13379 if unknown)
} ScanResult;

extern ScanResult g_tables[SCAN_MAX_TABLES];
extern uint8_t    g_tableCount;       // tables found (0 = none)
extern uint8_t    g_tableIx;          // the one selected
extern uint8_t    g_listMode;         // LIST_*
// The selected table. A macro so every reader keeps writing g_scan.songTable and friends; with no
// table found, entry 0 reads as SCAN_NONE.
#define g_scan (g_tables[g_tableIx])

// Playable songs in the selected table and list mode: table indices whose header has at least one
// track, consecutive duplicates dropped, and each one's music player. This is what PLAY steps through.
extern uint16_t g_songIx[SCAN_MAX_SONGS];
extern uint8_t  g_songPlayer[SCAN_MAX_SONGS];
extern uint16_t g_songCount;

// Called every 64 KB of scanning with progress in 1/256ths, so the caller can keep the link alive
// and draw a bar. May be NULL.
typedef void (*ScanProgressFn)(uint32_t done256);

// Find every table and select the first, in the current list mode. Returns g_scan.method, or
// SCAN_NONE if no table holds a playable song.
int scan_run(ScanProgressFn progress);

// Install tables found on an earlier boot (a saved patch's) instead of scanning: addresses and
// mixing rates. Each is checked as the scan checks its own (it must hold a playable song); rates
// outside m4a's 5734..42048 become 13379. Returns the tables kept; 0 leaves the state as a scan that
// found nothing, and the caller should scan for real.
int scan_install(int n, const uint32_t *addr, const uint16_t *rate);

// Select a table (0 .. g_tableCount-1) and list mode, and rebuild the song list. Returns the count.
int scan_select(int table, int listMode);

// A believable song header? Writes its track count. Exposed for the player's own checks.
int scan_header_ok(uint32_t hdr, int *tracks);

// The table address of playable song n (0 .. g_songCount-1).
uint32_t scan_song_entry(int n);

// OTHER ENGINES. A cart with no MP2K table gets the heuristic pass, which reads every word of the
// image; while it does, it also looks for the text other sound engines leave in a cart, so the
// screen can say what the game uses instead. Only signatures checked against a primary source:
//   GAX      Shin'en's engine, licensed to many third-party games. Its credit "GAX Sound Engine 3.05
//            (Aug 13 2003) (c) Shin'en ..." and a later "GAX ENGINE V3.05" (TCRF; loveemu's gaxtapper).
//   KRAWALL  Sebastian Kienzl's XM/S3M player. Retail builds carry a CVS "$Id: Krawall ..." line
//            (TCRF; some are stripped), the open-sourced library "$Krawall v1.0.0" (lib/general.c).
//   XM       a whole FastTracker 2 module stored as-is: "Extended Module: " and its 20-byte name.
// Engines that leave no text (Apex Audio System, checked in its source; most in-house engines)
// cannot be named this way.
#define ENG_GAX     (1u << 0)
#define ENG_KRAWALL (1u << 1)
#define ENG_XM      (1u << 2)
#define ENGINE_INFO_LEN 38                    // a full line of 5x7 text, less a margin
extern uint8_t  g_engines;                    // ENG_* found by the last scan's heuristic pass
extern uint16_t g_xmCount;                    // XM modules found
// The best signature found, verbatim from the cart with the text after it (printable ASCII only):
// "GAX Sound Engine 3.05 (Aug 13 2003)", "Krawall v1.0.0", "Extended Module: Title Theme".
extern char     g_engineInfo[ENGINE_INFO_LEN + 1];
