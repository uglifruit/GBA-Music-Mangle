// mixer.h — the DirectSound renderer: up to twelve 8-bit sample voices, read straight from the
// cartridge, resampled and mixed into the audio rings sixteen samples at a time.
//
// THE SPLIT. This is ONLY the renderer. Which sample, how loud, what pitch, the envelopes, the
// voice allocation - all of that is player.c, at control rate. The interrupt reads a MixChan's
// live fields (step, volumes) every block and owns the playback position outright.
//
// STARTS ARE ATOMIC. A start writes the sample, its bounds and its loop, then bumps `gen` - all
// inside a few-store IME-off window (mixer_start), so the interrupt sees the old sound or the new
// one, never half of each. On the first block after a bump the interrupt rewinds its private
// position. When a one-shot sample runs out it records the generation it finished (`doneGen`) - a
// counter rather than a flag, so the control side can never mistake an old ending for the new note.
//
// POSITIONS ARE Q12 SAMPLES in an int32, which caps a sample at 512 K samples (MIX_MAX_SAMPLES),
// half a minute at the rates games use. Longer ones are truncated, never overrun.
#pragma once

#include <stdint.h>
#include "audio.h"

#define MIX_CHANNELS     12
#define MIX_MAX_SAMPLES  0x7FFFFu
#define MIX_NOLOOP       0xFFFFFFFFu

#define MIX_F_REVERSE    0x01

typedef struct {
    // ---- written by the control layer (starts under IME-off; step/vol any time) ----
    const int8_t     *data;      // sample 0 (a cart address on the GBA)
    uint32_t          startQ;    // where a start begins, Q12
    uint32_t          endQ;      // size << 12
    uint32_t          loopQ;     // loop start << 12, or MIX_NOLOOP
    volatile uint32_t step;      // Q12 samples per output sample
    volatile uint16_t volL, volR;// 0..255
    volatile uint8_t  active;    // 0 = skipped entirely
    uint8_t           flags;     // MIX_F_*
    volatile uint32_t gen;       // bumped by every start
    // ---- owned by the interrupt ----
    uint32_t          seen;      // the gen it last started
    int32_t           pos;       // Q12
    int32_t           curL, curR;// volumes actually used, eased toward volL/volR each block
    volatile uint32_t doneGen;   // == gen once a one-shot has run out
} MixChan;

extern MixChan g_mix[MIX_CHANNELS];

// Output stage, written by the control layer.
extern volatile uint8_t g_mixGain;       // 16 = unity
extern volatile uint8_t g_crushHold;     // 1 = off, else hold each output sample this many times
extern volatile uint8_t g_crushMask;     // 0xFF = off, else AND each sample with this
extern volatile uint8_t g_mixReverb;     // 0 = off, else m4a-style echo level 1..127

void mixer_init(void);
void mixer_start(int ch, const int8_t *data, uint32_t size, uint32_t loopStart, uint32_t startAt,
                 uint32_t step, uint16_t volL, uint16_t volR, uint8_t flags);
void mixer_stop(int ch);
static inline int mixer_done(int ch) { return g_mix[ch].doneGen == g_mix[ch].gen; }

// Render AUDIO_BLOCK samples of each side. Called from the DMA2 interrupt only.
void mixer_render(int8_t *outL, int8_t *outR);
