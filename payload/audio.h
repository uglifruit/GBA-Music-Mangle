// audio.h — Direct Sound, stereo: Timer 0 clocks samples out of FIFO A (left) and FIFO B
// (right); DMA1 keeps A fed from the left ring, DMA2 keeps B fed from the right ring.
//
// Inherited from the FM card unchanged in design (see its audio.h and CLAUDE.md, "The audio
// engine"), at half the rate: 16384 Hz. That is GBA-authentic for a sample player (games mix at
// 10-21 kHz), halves the mixing cost, and makes the pitch maths a shift - a note's step is
// wav->freq x ratio >> 34 exactly (see player.c).
//
// THE RINGS AND THE INTERRUPTS. Each DMA runs in sound-FIFO mode and raises its interrupt after
// every four-word block. DMA1's interrupt restarts DMA1 at the ring start after the last block;
// DMA2's does the same for DMA2 and then renders sixteen fresh samples into the block BOTH
// channels have just finished with (DMA2 is the lower priority, so when its block is done DMA1's
// already was). OPEN RISK, inherited: this relies on both DMAs raising an IRQ after EVERY FIFO
// block, which GBATEK and mGBA describe but nobody has yet seen on this console. A 64 Hz click or
// silence on the first hardware test means it is not behaving as assumed.
#pragma once

#include <stdint.h>

#define AUDIO_RATE   16384                    // Hz; Timer 0 reload 0xFC00
#define AUDIO_BLOCK  16                       // samples per DMA block (four words)
#define AUDIO_BLOCKS 16                       // each ring = 256 bytes

void audio_init(void);
void audio_on_dma1_irq(void);                 // called from irq_dispatch only
void audio_on_dma2_irq(void);                 // called from irq_dispatch only

// DSP load, as a percentage of one block's time budget spent rendering it. Measured on Timer 3
// inside the handler, so it excludes the BIOS dispatch overhead (a few dozen cycles per block).
extern volatile uint16_t g_dspAvg;            // smoothed, percent x 16
extern volatile uint16_t g_dspPeak;           // worst block since boot, percent x 16

// Counts rendered blocks. 1024 a second - the player's 60 Hz frame and the UI read it.
extern volatile uint32_t g_audioBlocks;
