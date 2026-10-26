// clock.c — INT/EXT/AUTO sequencer clocking and the envelope frame. See clock.h.

#include "clock.h"
#include "player.h"
#include "gba.h"

#define REG_TM2CNT_L R16(0x0108)
#define REG_TM2CNT_H R16(0x010A)

#define T2_HZ          16384u
// One m4a video frame (59.7275 Hz) is 274.312 Timer 2 counts; kept in thousandths so the frame
// rate is exact rather than a rounding off.
#define FRAME_MILLI    274312u
// INT ticks: m4a runs tempoI/150 ticks per frame, i.e. tempoI x 59.7275/150 per second, which is
// tempoI x 407.74 per Timer 2 count in Q24.
#define TICK_Q24_PER_TEMPO 408u
#define TICK_ONE       (1u << 24)
#define EXT_TIMEOUT    (2u * T2_HZ)       // AUTO falls back to INT after 2 s without a pulse
#define RESET_WINDOW   (T2_HZ / 100u)     // a reset this soon after a pulse belongs with it (10 ms)
#define MAX_CATCHUP    8                  // ticks per service call, so a stall cannot burst a bar

const uint8_t clk_ppqn[CLK_PPQN_COUNT] = { 1, 2, 4, 8, 12, 24 };
static const uint8_t s_tpp[CLK_PPQN_COUNT] = { 24, 12, 6, 3, 2, 1 };   // 24 / ppqn, precomputed

uint8_t  g_clkMode    = CLK_AUTO;
uint8_t  g_clkPpqnIx  = 2;                // 4 PPQN: sixteenths, the common Eurorack clock
uint8_t  g_clkRun     = 1;
uint8_t  g_clkExtLive = 0;
uint16_t g_clkBpm     = 150;

static uint16_t s_t2Last;
static uint32_t s_time;                   // Timer 2 counts since boot
static uint32_t s_svcLast;                // s_time at the last clock_service (clock_pulse moves s_time too)
static uint32_t s_frameAcc;               // thousandths of a count
static uint32_t s_tickAcc;                // INT, Q24 ticks

static uint8_t  s_havePulse, s_havePeriod;
static uint32_t s_lastPulse;              // time of the last pulse
static uint32_t s_period;                 // smoothed pulse period, counts
static uint32_t s_interval;               // counts between the ticks of one pulse
static uint32_t s_extNext;                // when the next owed tick is due
static uint8_t  s_extDue;                 // ticks still owed for the current pulse

// Shift-subtract division: the payload links -nostdlib, so a divide by a variable would not link.
static uint32_t udiv32(uint32_t n, uint32_t d)
{
    if (d == 0) return 0xFFFFFFFFu;
    uint32_t q = 0, r = 0;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= 1u << i; }
    }
    return q;
}

uint32_t clock_now(void)
{
    uint16_t t = REG_TM2CNT_L;
    s_time += (uint16_t)(t - s_t2Last);
    s_t2Last = t;
    return s_time;
}

void clock_init(void)
{
    REG_TM2CNT_H = 0;
    REG_TM2CNT_L = 0;
    REG_TM2CNT_H = 0x0083;                // enable, prescaler 1024: 16384 Hz
    s_t2Last = 0;
    s_time = 0;
    s_svcLast = 0;
}

static uint32_t ticks_per_pulse(void)
{
    return s_tpp[g_clkPpqnIx < CLK_PPQN_COUNT ? g_clkPpqnIx : 2];
}

// The period spread over one pulse's ticks. A switch so every divisor is a constant.
static uint32_t interval_for(uint32_t period)
{
    switch (ticks_per_pulse()) {
    case 24: return period / 24;
    case 12: return period / 12;
    case 6:  return period / 6;
    case 3:  return period / 3;
    case 2:  return period / 2;
    default: return period;
    }
}

static int ext_active(uint32_t now)
{
    if (g_clkMode == CLK_INT) return 0;
    if (g_clkMode == CLK_EXT) return 1;
    return s_havePulse && (now - s_lastPulse) < EXT_TIMEOUT;
}

void clock_pulse(void)
{
    uint32_t now = clock_now();
    if (g_clkMode == CLK_INT) return;

    // Whatever the last pulse still owed is played NOW, before this pulse's first tick: the song
    // stays locked to the pulse count even if the clock just sped up.
    while (s_extDue) { player_tick(); s_extDue--; }

    if (s_havePulse) {
        uint32_t p = now - s_lastPulse;
        if (p > 0 && p < EXT_TIMEOUT) {
            // Light smoothing against the link's ~1 ms delivery jitter, but a real tempo change
            // (more than about 25 % off) is taken at once rather than glided into.
            if (!s_havePeriod || p > s_period + (s_period >> 2) || p + (s_period >> 2) < s_period)
                s_period = p;
            else
                s_period = (s_period * 3 + p) >> 2;
            s_havePeriod = 1;
        } else {
            s_havePeriod = 0;               // a long gap: this is a restart, not a slow clock
        }
    }
    s_havePulse = 1;
    s_lastPulse = now;

    uint32_t tpp = ticks_per_pulse();
    if (s_havePeriod) {
        s_interval = interval_for(s_period);
    } else {
        // No period yet (the first pulse after a stop): assume the song's own tempo for the spread.
        // One tick at tempoI is 16384 x 150 / (tempoI x 59.7275) = 41148 / tempoI counts.
        s_interval = udiv32(41148u, player_tempo_i());
    }
    if (s_interval == 0) s_interval = 1;

    g_clkExtLive = 1;
    s_tickAcc = 0;
    player_tick();
    s_extDue  = (uint8_t)(tpp - 1);
    s_extNext = now + s_interval;
}

void clock_reset(void)
{
    uint32_t now = clock_now();
    s_extDue = 0;
    if (ext_active(now)) {
        // A reset that lands just after its clock pulse (they are usually patched from the same
        // sequencer, and the link delivers them a poll or two apart) belongs to that pulse: play
        // the restarted song's first tick now and spread the rest from the pulse's own time.
        if (s_havePulse && now - s_lastPulse < RESET_WINDOW) {
            player_tick();
            s_extDue  = (uint8_t)(ticks_per_pulse() - 1);
            s_extNext = s_lastPulse + s_interval;
        }
    } else {
        s_tickAcc = TICK_ONE;               // INT: the first tick plays on the next service
    }
}

void clock_service(void)
{
    uint32_t now = clock_now();
    uint32_t dt = now - s_svcLast;
    s_svcLast = now;
    // After a long stall, catch up an eighth of a second at most. This also bounds the INT
    // accumulator: dt x tempoI x 408 stays under 2^31 for tempoI up to 2040 (510 BPM x 4).
    if (dt > T2_HZ / 8) dt = T2_HZ / 8;

    // The envelope frame, at m4a's video rate whatever the clock is doing.
    s_frameAcc += dt * 1000u;
    for (int n = 0; s_frameAcc >= FRAME_MILLI; n++) {
        s_frameAcc -= FRAME_MILLI;
        if (n < 4) player_frame();
    }

    if (ext_active(now)) {
        g_clkExtLive = 1;
        int n = 0;
        while (s_extDue && (int32_t)(now - s_extNext) >= 0 && n++ < MAX_CATCHUP) {
            player_tick();
            s_extDue--;
            s_extNext += s_interval;
        }
        if (s_havePeriod) {
            // BPM = 60 x 16384 / (period x PPQN)
            uint32_t ppqn = clk_ppqn[g_clkPpqnIx < CLK_PPQN_COUNT ? g_clkPpqnIx : 2];
            g_clkBpm = (uint16_t)udiv32(60u * T2_HZ, s_period * ppqn);
        }
    } else {
        g_clkExtLive = 0;
        uint32_t ti = player_tempo_i();
        g_clkBpm = (uint16_t)ti;
        if (g_clkRun && player_running()) {
            s_tickAcc += dt * ti * TICK_Q24_PER_TEMPO;
            int n = 0;
            while (s_tickAcc >= TICK_ONE && n++ < MAX_CATCHUP) {
                s_tickAcc -= TICK_ONE;
                player_tick();
            }
            if (s_tickAcc >= TICK_ONE) s_tickAcc = TICK_ONE - 1;
        }
    }

    player_update();
}
