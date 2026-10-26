// player.h — the MP2K sequencer and voice logic: a new engine that plays the game's own data.
//
// It follows m4a's semantics closely (pret's MPlayMain, ply_*, TrkVolPitSet, ChnVolSetAsm and the
// SoundMain envelope; see m4a.h for the sources), with three deliberate departures:
//
//   1. THE SEQUENCER IS CLOCKED FROM OUTSIDE. m4a advances tempoI/150 ticks per video frame;
//      here player_tick() advances exactly one 24-PPQN tick and clock.c decides when - from the
//      song's own tempo, or from a Eurorack clock. Envelopes and the mixer keep running regardless
//      (player_frame() at ~60 Hz, mixer.c per block), so a stopped clock holds notes rather than
//      freezing sound.
//   2. A NOTE STARTS AT ITS TICK, not at the next frame. m4a defers the start to SoundMain, up to
//      16.7 ms later; an externally clocked instrument cannot afford that jitter, so the start is
//      processed immediately.
//   3. NOTHING IS TRUSTED. Every pointer is bounds-checked, call depth is capped, and a track that
//      runs too many commands without a wait is stopped - because the mangle hooks below exist to
//      read from places the data was never meant to be read from.
#pragma once

#include <stdint.h>
#include "m4a.h"

#define PL_TRACKS   MAX_TRACKS
#define PL_DS_MAX   12
#define PL_CGB      4
#define PL_NONE     0xFF

// ---- the mangle hooks ---------------------------------------------------------------------------
// One writer (mod.c), read at note-on (voice/sample choices) and on every update (pitch, mute,
// tempo). All fields are plain values; changing one never needs more than the next update.
typedef struct {
    int16_t  voiceShift;      // programs added to every VOICE (wraps 0..127)
    int16_t  tableOffset;     // bytes added to the voicegroup pointer itself
    uint8_t  sampleStart;     // start this far into each sample, /256 of its length
    uint8_t  reverseChance;   // 0..255: chance a note plays its sample backwards
    int32_t  romScrub;        // bytes added to each sample's data pointer
    uint32_t scrubLen;        // length, in samples, of a raw-ROM voice (mangled tone fallback)
    uint16_t mute;            // bit per track: silenced (still playing, so unmute is instant)
    int16_t  pitch;           // global, 1/256 semitone (transpose + fine), like m4aMPlayPitchControl
    uint16_t tempoU;          // 0x100 = the song's tempo (m4aMPlayTempoControl's units)
} PlayerMods;

extern PlayerMods g_mods;

// ---- tracks (MusicPlayerTrack, our layout) -------------------------------------------------------
#define MPT_FLG_VOLSET 0x01
#define MPT_FLG_VOLCHG 0x03
#define MPT_FLG_PITSET 0x04
#define MPT_FLG_PITCHG 0x0C
#define MPT_FLG_START  0x40
#define MPT_FLG_EXIST  0x80

typedef struct {
    uint8_t  flags, wait, patternLevel, repN;
    uint8_t  gateTime, key, velocity, runningStatus;
    int8_t   keyM;  uint8_t pitM; int8_t keyShift, keyShiftX;
    int8_t   tune;  uint8_t pitX; int8_t bend;     uint8_t bendRange;
    uint8_t  volMR, volML, vol, volX;
    int8_t   pan, panX, modM;      uint8_t mod;
    uint8_t  modT, lfoSpeed, lfoSpeedC, lfoDelay;
    uint8_t  lfoDelayC, priority, echoVol, echoLen;
    uint8_t  voice;                 // the raw VOICE number, re-resolved when mangled
    uint8_t  tone[TONE_SIZE];       // the ToneData in force (VOICE copies it; XCMDs edit it)
    uint16_t timer;                 // xWAIT
    uint32_t startOff;              // XCMD 0x0D: sample start offset (m4a's unk_3C)
    uint32_t cmd;                   // next command byte (cart address)
    uint32_t stack[3];
    // ---- ours: for the UI and the follower ----
    uint8_t  lastKey;               // last note's key after keyShift
    uint8_t  act;                   // note activity for the UI, decays per frame
    uint8_t  gate;                  // a note is held (its gate time has not run out)
    uint16_t gateLeft;              // ticks of gate left (0 with gate = a TIE)
    uint32_t notes;                 // note-on counter (the follower retriggers on it)
} Track;

extern Track g_tracks[PL_TRACKS];

// ---- channels (SoundChannel / CgbChannel, one shape) ---------------------------------------------
#define SF_START   0x80
#define SF_STOP    0x40
#define SF_LOOP    0x10
#define SF_IEC     0x04
#define SF_ENV     0x03
#define SF_ATTACK  0x03
#define SF_DECAY   0x02
#define SF_SUSTAIN 0x01
#define SF_RELEASE 0x00
#define SF_ON      (SF_START | SF_STOP | SF_IEC | SF_ENV)

#define CGB_MO_PIT 0x02
#define CGB_MO_VOL 0x01

typedef struct {
    uint8_t  status, type, rightVolume, leftVolume;
    uint8_t  attack, decay, sustain, release;
    uint8_t  key, envVol, envGoal, envCounter;
    uint8_t  echoVol, echoLen, gateTime, midiKey;
    uint8_t  velocity, priority; int8_t rhythmPan; uint8_t track;
    // CGB only
    uint8_t  sustainGoal, n4, pan, panMask;
    uint8_t  modify, length, sweep, pad;
    uint32_t frequency;             // CGB: the 11-bit register value (or noise byte)
    uint32_t wav;                   // CGB: duty, wave-table address, or noise width
    uint32_t curWave;               // CGB ch3: the table now in wave RAM
    // DirectSound only: the sample, resolved and bounds-checked at note-on
    uint32_t sData, sSize, sLoop;   // sLoop = MIX_NOLOOP for a one-shot
    uint32_t sFreq;                 // Hz x 1024 at key 60 (0 = fixed-rate)
    uint8_t  sFlags;                // MIX_F_*
} Chan;

extern Chan g_ds[PL_DS_MAX];
extern Chan g_cgb[PL_CGB];

// ---- the song ----------------------------------------------------------------------------------
extern uint32_t g_plHeader;         // the SongHeader playing, 0 = none
extern uint8_t  g_plTrackCount;
extern uint16_t g_plTempoD;         // song tempo in BPM (m4a's tempoD: TEMPO arg x 2)
extern uint32_t g_plTicks;          // ticks since the song started
extern uint8_t  g_plBeats;          // beat counter (every 24 ticks), wraps - GBA_UP_PULSE
extern uint8_t  g_plLoops;          // loop counter (track 1 jumped back), wraps - GBA_UP_PULSE
extern uint8_t  g_plMaxChans;       // DirectSound voices allowed, 1..PL_DS_MAX
extern uint16_t g_plGameRate;       // the game's mixing rate, for fixed-rate samples
extern uint8_t  g_plRepeat;         // restart a song that ends (jingles) instead of stopping

void     player_init(void);
int      player_start(uint32_t songEntry);   // a song-table entry; returns 0 if unplayable
void     player_stop(void);
int      player_running(void);

void     player_tick(void);                  // ONE sequencer tick (24 PPQN)
void     player_update(void);                // apply mods + volume/pitch changes; call often
void     player_frame(void);                 // the ~60 Hz envelope step (DS + CGB)
uint32_t player_tempo_i(void);               // tempoD x tempoU >> 8: ticks/frame x 150
