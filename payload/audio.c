// audio.c — Direct Sound setup and the per-block DMA interrupts. See audio.h for the design.
// Adapted from the FM card's audio.c: same rings and interrupts, the renderer swapped for the
// sample mixer, and the PSG enabled because MP2K songs play through it too (cgb.c).

#include "audio.h"
#include "gba.h"
#include "irq.h"
#include "mixer.h"

#define REG_SOUNDCNT_L R16(0x0080)   // NR50 (PSG master volume) | NR51 (PSG pan enables)
#define REG_SOUNDCNT_H R16(0x0082)   // Direct Sound control + PSG mix ratio
#define REG_SOUNDCNT_X R16(0x0084)   // master enable
#define REG_SOUNDBIAS  R16(0x0088)
#define REG_FIFO_A     0x040000A0u
#define REG_FIFO_B     0x040000A4u
#define REG_DMA1SAD    R32(0x00BC)
#define REG_DMA1DAD    R32(0x00C0)
#define REG_DMA1CNT_H  R16(0x00C6)
#define REG_DMA2SAD    R32(0x00C8)
#define REG_DMA2DAD    R32(0x00CC)
#define REG_DMA2CNT_H  R16(0x00D2)
#define REG_TM0CNT_L   R16(0x0100)
#define REG_TM0CNT_H   R16(0x0102)
#define REG_TM3CNT_L   R16(0x010C)
#define REG_TM3CNT_H   R16(0x010E)

// 0x10000 - 16777216 / rate: the timer overflows once per sample. At 16384 Hz that is 1024 cycles,
// reload 0xFC00. (The often-quoted 0xFFFF - cycles is one cycle short of the rate it names.)
#define TM0_RELOAD (0x10000u - 16777216u / AUDIO_RATE)
#define TM_ENABLE  0x0080u

// DMA1/DMA2 control: enable | IRQ at end of each block | start on FIFO request | 32-bit |
// repeat | destination fixed. In FIFO mode the word count is ignored: every request moves four
// words.
#define DMA_SOUND (0x8000u | 0x4000u | 0x3000u | 0x0400u | 0x0200u | 0x0040u)

// SOUNDCNT_H. Timer-select bits 10 (A) and 14 (B) left at 0 = Timer 0 for both.
#define PSG_VOL_100  0x0002u
#define SNDA_VOL_100 0x0004u
#define SNDB_VOL_100 0x0008u
#define SNDA_LEFT    0x0200u
#define SNDA_RESET   0x0800u
#define SNDB_RIGHT   0x1000u
#define SNDB_RESET   0x8000u

// The sample cycle budget for one block, for the load meter.
#define BLOCK_CYCLES (AUDIO_BLOCK * (16777216u / AUDIO_RATE))

IWRAM_BSS int8_t g_ringL[AUDIO_BLOCK * AUDIO_BLOCKS] __attribute__((aligned(4)));   // FIFO A
IWRAM_BSS int8_t g_ringR[AUDIO_BLOCK * AUDIO_BLOCKS] __attribute__((aligned(4)));   // FIFO B
static IWRAM_BSS uint32_t s_blkA, s_blkB;
static IWRAM_BSS uint32_t s_dspAcc;

volatile uint16_t g_dspAvg  = 0;
volatile uint16_t g_dspPeak = 0;
volatile uint32_t g_audioBlocks = 0;

void audio_init(void)
{
    // MASTER ENABLE FIRST. With SOUNDCNT_X bit 7 clear the sound registers are not writable at
    // all, so anything written before it is silently discarded.
    REG_SOUNDCNT_X = 0x0080;
    REG_SOUNDBIAS  = 0x0200;                   // 9-bit at 32.768 kHz PWM
    // PSG master volume full both sides (NR50 = 0x77), every channel's output off until cgb.c
    // routes it (NR51 = 0) - m4a's MPlayExtender does the same.
    REG_SOUNDCNT_L = 0x0077;
    // Stereo: Sound A left, Sound B right, both full; PSG at its full mix ratio; FIFOs flushed.
    REG_SOUNDCNT_H = PSG_VOL_100 | SNDA_VOL_100 | SNDB_VOL_100 | SNDA_LEFT | SNDB_RIGHT
                   | SNDA_RESET | SNDB_RESET;

    mixer_init();

    REG_TM0CNT_H  = 0;
    REG_DMA1CNT_H = 0;
    REG_DMA1SAD   = (uint32_t)g_ringL;
    REG_DMA1DAD   = REG_FIFO_A;
    REG_DMA1CNT_H = DMA_SOUND;
    REG_DMA2CNT_H = 0;
    REG_DMA2SAD   = (uint32_t)g_ringR;
    REG_DMA2DAD   = REG_FIFO_B;
    REG_DMA2CNT_H = DMA_SOUND;

    // Timer 3 free-running at the CPU clock, for the load meter only.
    REG_TM3CNT_L = 0;
    REG_TM3CNT_H = TM_ENABLE;

    irq_enable(IRQ_DMA1 | IRQ_DMA2);

    // The sample clock goes last: nothing is requested until it runs.
    REG_TM0CNT_L = TM0_RELOAD;
    REG_TM0CNT_H = TM_ENABLE;
}

// DMA1 (left, FIFO A): the restart and nothing else. It is the higher-priority channel, so its
// block is always fetched first; the render waits for DMA2's interrupt, by which time both rings'
// block has gone.
IWRAM_CODE void audio_on_dma1_irq(void)
{
    if (++s_blkA == AUDIO_BLOCKS) {
        s_blkA = 0;
        REG_DMA1CNT_H = 0;
        REG_DMA1SAD   = (uint32_t)g_ringL;
        REG_DMA1CNT_H = DMA_SOUND;
    }
}

// DMA2 (right, FIFO B): restart, then render the block both channels have just finished with.
IWRAM_CODE void audio_on_dma2_irq(void)
{
    uint16_t t0 = REG_TM3CNT_L;

    uint32_t off = s_blkB * AUDIO_BLOCK;              // the block both DMAs have just read
    if (++s_blkB == AUDIO_BLOCKS) {
        // Restart BEFORE rendering: this is the deadline, the render is not.
        s_blkB = 0;
        REG_DMA2CNT_H = 0;
        REG_DMA2SAD   = (uint32_t)g_ringR;
        REG_DMA2CNT_H = DMA_SOUND;
    }
    mixer_render(g_ringL + off, g_ringR + off);
    g_audioBlocks = g_audioBlocks + 1;

    // Percent x 16 of the block's budget. BLOCK_CYCLES is 16384, so the divide is a shift.
    uint32_t used = (uint16_t)(REG_TM3CNT_L - t0);
    uint32_t pct  = (used * 1600u) / BLOCK_CYCLES;    // constant divisor
    // One-pole average kept x64 so small differences still move it.
    s_dspAcc += pct - (s_dspAcc >> 6);
    g_dspAvg = (uint16_t)(s_dspAcc >> 6);
    if (pct > g_dspPeak) g_dspPeak = (uint16_t)pct;
}
