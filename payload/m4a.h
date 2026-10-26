// m4a.h — the Nintendo MP2K ("Sappy", m4a) data format, as it sits in a cartridge.
//
// THE AUTHORITY IS pret's decompilation (pokeemerald: src/m4a.c, src/m4a_1.s, src/m4a_tables.c,
// include/gba/m4a_internal.h, constants/m4a_constants.inc), not memory. Every table and constant
// here was transcribed from those files; cross-check against them before changing anything.
//
// Everything is little-endian and read through rom.h, never by casting a cart pointer to a struct:
// the layouts are packed differently from what GCC would choose, and the mangle hooks read them at
// deliberately wrong addresses.
#pragma once

#include <stdint.h>

// ---- song table: an array of these, 8 bytes each (struct Song) ----
#define SONG_ENTRY_SIZE   8
#define SONG_HEADER(e)    ((e) + 0)    // u32 -> SongHeader
#define SONG_MS(e)        ((e) + 4)    // u16 music player index
#define SONG_ME(e)        ((e) + 6)    // u16

// ---- SongHeader ----
#define SH_TRACKS(h)      ((h) + 0)    // u8 trackCount (0..16)
#define SH_BLOCKS(h)      ((h) + 1)    // u8
#define SH_PRIORITY(h)    ((h) + 2)    // u8
#define SH_REVERB(h)      ((h) + 3)    // u8, bit 7 = set reverb to bits 6:0
#define SH_TONE(h)        ((h) + 4)    // u32 -> voicegroup (ToneData[])
#define SH_PART(h, i)     ((h) + 8 + 4 * (i))   // u32 -> track data
#define MAX_TRACKS        16

// ---- ToneData: one voicegroup entry, 12 bytes ----
#define TONE_SIZE         12
#define TONE_TYPE(t)      ((t) + 0)
#define TONE_KEY(t)       ((t) + 1)    // base key (drum entries: the key the drum plays at)
#define TONE_LENGTH(t)    ((t) + 2)    // CGB length
#define TONE_PANSWEEP(t)  ((t) + 3)    // drum pan (bit 7 set) or CGB ch1 sweep
#define TONE_WAV(t)       ((t) + 4)    // u32: WaveData / CGB duty / wave ptr / sub-voicegroup
#define TONE_ATTACK(t)    ((t) + 8)    // key-split entries: u32 -> key-split table here
#define TONE_DECAY(t)     ((t) + 9)
#define TONE_SUSTAIN(t)   ((t) + 10)
#define TONE_RELEASE(t)   ((t) + 11)

#define TONEDATA_TYPE_CGB 0x07         // 1 square+sweep, 2 square, 3 wave, 4 noise
#define TONEDATA_TYPE_FIX 0x08         // fixed rate: no resampling
#define TONEDATA_TYPE_REV 0x10         // play backwards
#define TONEDATA_TYPE_CMP 0x20         // DPCM-compressed samples (not supported: silent)
#define TONEDATA_TYPE_SPL 0x40         // key split: wav = sub-voicegroup, attack.. = split table
#define TONEDATA_TYPE_RHY 0x80         // rhythm: wav = sub-voicegroup indexed by key
#define TONEDATA_P_S_PAN  0xC0

// ---- WaveData ----
#define WAV_TYPE(w)       ((w) + 0)    // u16
#define WAV_FLAGS(w)      ((w) + 3)    // u8: WAVE_DATA_FLAG_LOOP
#define WAV_FREQ(w)       ((w) + 4)    // u32: Hz x 1024 at key 60
#define WAV_LOOP(w)       ((w) + 8)    // u32 loop start, samples
#define WAV_SIZE(w)       ((w) + 12)   // u32 length, samples
#define WAV_DATA(w)       ((w) + 16)   // s8[size]
#define WAVE_DATA_FLAG_LOOP 0xC0

// ---- the command set (m4a_tables.c and the gMPlayJumpTable, cmd - 0xB1) ----
#define CMD_WAIT0   0x80               // 0x80..0xB0: wait gClockTable[cmd - 0x80]
#define CMD_FINE    0xB1
#define CMD_GOTO    0xB2
#define CMD_PATT    0xB3
#define CMD_PEND    0xB4
#define CMD_REPT    0xB5
#define CMD_MEMACC  0xB9
#define CMD_PRIO    0xBA
#define CMD_TEMPO   0xBB
#define CMD_KEYSH   0xBC
#define CMD_VOICE   0xBD               // running status applies from here up
#define CMD_VOL     0xBE
#define CMD_PAN     0xBF
#define CMD_BEND    0xC0
#define CMD_BENDR   0xC1
#define CMD_LFOS    0xC2
#define CMD_LFODL   0xC3
#define CMD_MOD     0xC4
#define CMD_MODT    0xC5
#define CMD_TUNE    0xC8
#define CMD_PORT    0xCC
#define CMD_XCMD    0xCD
#define CMD_EOT     0xCE
#define CMD_TIE     0xCF               // 0xCF..0xFF: notes, gate gClockTable[cmd - 0xCF]

#define C_V         0x40               // centre value for PAN, BEND and TUNE

// ---- tables (m4a_tables.c) ----
extern const uint8_t  gScaleTable[180];
extern const uint32_t gFreqTable[12];
extern const uint8_t  gCgbScaleTable[132];
extern const int16_t  gCgbFreqTable[12];
extern const uint8_t  gNoiseTable[60];
extern const uint8_t  gCgb3Vol[16];
extern const uint8_t  gClockTable[49];

// The m4a mixing rates, SOUND_MODE_FREQ 1..12. A fixed-rate (TONEDATA_TYPE_FIX) sample plays one
// sample per output sample of the GAME's mixer, so its pitch depends on the game's rate.
extern const uint16_t gM4aRates[12];
