// scan.h — find the MP2K song table in the inserted cartridge, and list its playable songs.
//
// Two methods, in order:
//
//   SIGNATURE  saptapper's: the library's SelectSong (m4aSongNumStart) Thumb routine, matched
//              loosely (4-aligned, at most 8 of its 30 bytes may differ - literal-pool offsets move
//              between builds). The song table's address sits in its literal pool, 40 bytes in.
//   HEURISTIC  agbplay-style, when the signature is absent (an unusual library build): the longest
//              run of consecutive entries that each point at a believable song header.
//
// Neither is allowed to trust anything: a believable header is one whose track count, voicegroup
// pointer and every track pointer check out against the image bounds.
#pragma once

#include <stdint.h>

#define SCAN_NONE      0
#define SCAN_SIGNATURE 1
#define SCAN_HEURISTIC 2

#define SCAN_MAX_SONGS 2048

typedef struct {
    uint8_t  method;        // SCAN_*
    uint32_t selectSong;    // address of the matched routine (SIGNATURE only), else 0
    uint32_t songTable;     // address of entry 0, or 0
    uint16_t tableEntries;  // entries before the first non-pointer (saptapper's count)
    uint32_t soundMode;     // the game's m4aSoundMode word if found, else 0
    uint16_t gameRate;      // the game's mixing rate in Hz (13379 if unknown)
} ScanResult;

extern ScanResult g_scan;

// Playable songs: table indices whose header has at least one track, consecutive duplicates
// dropped. This is what the PLAY page steps through.
extern uint16_t g_songIx[SCAN_MAX_SONGS];
extern uint16_t g_songCount;

// Called every 64 KB of scanning with progress in 1/256ths, so the caller can keep the link alive
// and draw a bar. May be NULL.
typedef void (*ScanProgressFn)(uint32_t done256);

// Run both methods as needed and build the song list. Returns g_scan.method.
int scan_run(ScanProgressFn progress);

// A believable song header? Writes its track count. Exposed for the player's own checks.
int scan_header_ok(uint32_t hdr, int *tracks);

// The table address of playable song n (0 .. g_songCount-1).
uint32_t scan_song_entry(int n);
