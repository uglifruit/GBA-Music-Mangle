// psgemu.c — the GBA's four Game Boy channels, emulated for pcplay from the registers the payload
// writes (g_hostIo): MP2K's cgb.c and MusyX's psg_push() both drive them, and on the PC neither was
// audible before (2026-11-03). Enough for judging parts by ear and by measurement, not cycle-exact:
// squares with duty and the 64 Hz envelope, the wave channel's 32 4-bit steps, the noise LFSR.
// A trigger is the write of bit 7 to NRx4, which the hardware never reads back: the emulator takes it
// and clears it, as reading the register on the GBA would show.
#include <stdint.h>
#include "psgemu.h"
#include "gba.h"
#include "audio.h"

typedef struct {
    uint32_t phase, step;     // Q16 position in the waveform / noise clock, per output sample
    int vol, env, envDir, envTick;
    uint16_t lfsr;
    int on;
} Ch;
static Ch s_ch[4];
static uint32_t s_envClock;

static const uint8_t kDuty[4] = { 0x01, 0x81, 0x87, 0x7E };   // 12.5 %, 25 %, 50 %, 75 % over 8 steps
static const uint8_t kNrx2[4] = { 0x63, 0x69, 0x73, 0x79 }, kNrx3[4] = { 0x64, 0x6C, 0x74, 0x7C };
static const uint8_t kNrx4[4] = { 0x65, 0x6D, 0x75, 0x7D };

static uint32_t tone_step(int ch)
{
    unsigned x = R8(kNrx3[ch]) | ((R8(kNrx4[ch]) & 7u) << 8);
    // squares: 131072 / (2048 - x) Hz over 8 steps; wave: 65536 / (2048 - x) Hz over 32 steps
    double hz = (ch < 2 ? 131072.0 : 65536.0) / (2048 - x) * (ch < 2 ? 8 : 32);
    return (uint32_t)(hz * 65536.0 / AUDIO_RATE);
}

static uint32_t noise_step(void)
{
    unsigned v = R8(0x7C), s = v >> 4, r = v & 7;
    double hz = 524288.0 / (r ? r : 0.5) / (double)(2u << s);
    return (uint32_t)(hz * 65536.0 / AUDIO_RATE);
}

static void trigger(int ch)
{
    Ch *c = &s_ch[ch];
    unsigned e = R8(kNrx2[ch]);
    c->phase = 0;
    if (ch == 2) { c->on = (R8(0x70) & 0x80) != 0; return; }
    c->vol = e >> 4; c->envDir = (e & 8) ? 1 : -1; c->env = e & 7; c->envTick = c->env;
    c->on = (e & 0xF8) != 0;
    c->lfsr = 0x7FFF;
}

void psg_emu_render(int16_t *lr, unsigned n)
{
    for (int ch = 0; ch < 4; ch++) {
        if (R8(kNrx4[ch]) & 0x80) { R8(kNrx4[ch]) &= 0x7F; trigger(ch); }
        if (ch == 2 && !(R8(0x70) & 0x80)) s_ch[2].on = 0;
        s_ch[ch].step = ch == 3 ? noise_step() : tone_step(ch);
    }
    unsigned nr51 = R8(0x81), master = R8(0x80);
    int volL = (master >> 4) & 7, volR = master & 7;
    static const int kWaveShift[4] = { 4, 0, 1, 2 };
    for (unsigned i = 0; i < n; i++) {
        if (++s_envClock >= AUDIO_RATE / 64) {                 // the 64 Hz envelope clock
            s_envClock = 0;
            for (int ch = 0; ch < 4; ch++) {
                Ch *c = &s_ch[ch];
                if (ch == 2 || !c->env || !c->on) continue;
                if (--c->envTick <= 0) {
                    c->envTick = c->env;
                    int v = c->vol + c->envDir;
                    if (v >= 0 && v <= 15) c->vol = v;
                }
            }
        }
        int l = 0, r = 0;
        for (int ch = 0; ch < 4; ch++) {
            Ch *c = &s_ch[ch];
            if (!c->on) continue;
            int amp;                                           // -15..15
            if (ch < 2) {
                unsigned duty = (R8(ch ? 0x68 : 0x62) >> 6) & 3;
                amp = (kDuty[duty] >> ((c->phase >> 16) & 7) & 1) ? c->vol : -c->vol;
            } else if (ch == 2) {
                unsigned pos = (c->phase >> 16) & 31, b = R8(0x90 + (pos >> 1));
                int s4 = (pos & 1) ? (b & 15) : (b >> 4);
                unsigned v = (R8(0x73) >> 5) & 3;
                amp = v ? ((s4 * 2 - 15) >> kWaveShift[v]) : 0;
                if (R8(0x73) & 0x80) amp = ((s4 * 2 - 15) * 3) >> 2;
            } else {
                uint32_t before = c->phase >> 16;
                amp = (c->lfsr & 1) ? -c->vol : c->vol;
                c->phase += c->step;
                for (uint32_t k = before; k < (c->phase >> 16) && k < before + 64; k++) {
                    unsigned bit = (c->lfsr ^ (c->lfsr >> 1)) & 1;
                    c->lfsr = (uint16_t)((c->lfsr >> 1) | (bit << 14));
                    if (R8(0x7C) & 8) c->lfsr = (uint16_t)((c->lfsr & ~0x40u) | (bit << 6));
                }
                c->phase &= 0xFFFF;
                goto mix;
            }
            c->phase += c->step;
        mix:
            if (nr51 & (0x10u << ch)) l += amp;
            if (nr51 & (0x01u << ch)) r += amp;
        }
        // A channel at volume 15 and master 7 is about a quarter of full scale (15 x 512 of 32768).
        int ol = lr[i * 2] + l * (volL + 1) * 64;
        int or_ = lr[i * 2 + 1] + r * (volR + 1) * 64;
        lr[i * 2]     = (int16_t)(ol > 32767 ? 32767 : ol < -32768 ? -32768 : ol);
        lr[i * 2 + 1] = (int16_t)(or_ > 32767 ? 32767 : or_ < -32768 ? -32768 : or_);
    }
}

void psg_emu_reset(void)
{
    for (int ch = 0; ch < 4; ch++) s_ch[ch].on = 0;
    s_envClock = 0;
}
