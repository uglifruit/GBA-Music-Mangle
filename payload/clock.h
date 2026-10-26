// clock.h — when the sequencer ticks. The player advances one 24-PPQN tick per player_tick();
// this module decides when that happens, and runs the ~60 Hz envelope frame beside it.
//
// INT  the song's own tempo (TEMPO commands) x the TEMPO mod - m4a's tempoI/150 ticks per video
//      frame, accumulated here against Timer 2 instead of the video frame.
// EXT  a Eurorack clock: each pulse owes 24/PPQN ticks. The first plays ON the pulse; the rest are
//      spread across the pulse period measured from the previous pulses. A new pulse first FLUSHES
//      any ticks still owed, so the song is always exactly where the pulses say - it can lag a
//      fraction of a pulse when the clock speeds up, but it never drifts and never runs ahead. When
//      the pulses stop, the ticks stop: the song waits for the clock.
// AUTO EXT while pulses are arriving, INT after two seconds without one.
//
// Timing is all in Timer 2 counts: 16384 Hz (prescaler 1024), the same free-running clock the FM
// card uses for its control tick.
#pragma once

#include <stdint.h>

#define CLK_AUTO 0
#define CLK_INT  1
#define CLK_EXT  2

#define CLK_PPQN_COUNT 6
extern const uint8_t clk_ppqn[CLK_PPQN_COUNT];        // 1 2 4 8 12 24

extern uint8_t g_clkMode;        // CLK_* (setting)
extern uint8_t g_clkPpqnIx;      // index into clk_ppqn (setting)
extern uint8_t g_clkRun;         // transport: 0 = paused (INT only; an external clock always drives)
extern uint8_t g_clkExtLive;     // currently following an external clock
extern uint16_t g_clkBpm;        // tempo now in force, BPM (for the screen)

void     clock_init(void);
void     clock_service(void);    // every main-loop iteration: ticks, frames, player_update()
void     clock_pulse(void);      // one external clock edge
void     clock_reset(void);      // the song was just restarted: re-align to the clock
uint32_t clock_now(void);        // Timer 2 time, 16384 Hz, 32-bit
