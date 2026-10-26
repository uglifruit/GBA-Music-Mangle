// scan.h — find the MP2K song table(s) in the inserted cartridge, and list their playable songs.
//
// Two methods, in order:
//
//   SIGNATURE  saptapper's: the library's SelectSong (m4aSongNumStart) Thumb routine, matched
//              loosely (4-aligned, at most 8 of its 30 bytes may differ - literal-pool offsets move
//              between builds). The song table's address sits in its literal pool, 40 bytes in.
//   HEURISTIC  agbplay-style, when no signature matches (an unusual library build): the longest
//              runs of consecutive entries that each point at a believable song header.
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

// Select a table (0 .. g_tableCount-1) and list mode, and rebuild the song list. Returns the count.
int scan_select(int table, int listMode);

// A believable song header? Writes its track count. Exposed for the player's own checks.
int scan_header_ok(uint32_t hdr, int *tracks);

// The table address of playable song n (0 .. g_songCount-1).
uint32_t scan_song_entry(int n);
