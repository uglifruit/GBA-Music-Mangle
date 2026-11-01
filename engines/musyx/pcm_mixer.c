// Namespaced signed-PCM copy of the repository MIT mixer; no production state is shared.
// mixer.c — the DirectSound renderer. See mixer.h for the split and the start protocol.

#include "pcm_mixer.h"
#include "../../payload/gba.h"

IWRAM_BSS MixChan g_ms_mix[MIX_CHANNELS];

volatile uint8_t g_ms_gain   = 16;
volatile uint8_t g_ms_hold = 1;
volatile uint8_t g_ms_mask = 0xFF;
volatile uint8_t g_ms_reverb = 0;

// Accumulators live in IWRAM rather than on the IRQ stack: the BIOS hands the handler a stack of a
// few hundred bytes and the serial harvest shares it (see crt0.s on the stack move).
static IWRAM_BSS int32_t s_accL[AUDIO_BLOCK], s_accR[AUDIO_BLOCK];

// The echo: m4a's reverb is a feedback delay over its own PCM ring, two taps one frame apart, mixed
// back in mono under every new frame. 2048 samples here is 125 ms, about the 117 ms m4a gets at
// 13379 Hz with its 7-frame ring, and the second tap sits one 60 Hz frame (274 samples) later.
#define ECHO_LEN  2048u
#define ECHO_TAP2 274u
static IWRAM_BSS int8_t   s_echo[ECHO_LEN];
static IWRAM_BSS uint32_t s_echoPos;

static IWRAM_BSS uint8_t  s_holdCount;
static IWRAM_BSS int8_t   s_holdL, s_holdR;

void ms_pcm_init(void)
{
    for (int c = 0; c < MIX_CHANNELS; c++) {
        g_ms_mix[c].active  = 0;
        g_ms_mix[c].gen     = 0;
        g_ms_mix[c].seen    = 0;
        g_ms_mix[c].doneGen = 0xFFFFFFFFu;
    }
}

void ms_pcm_start(int ch, const int8_t *data, uint32_t size, uint32_t loopStart, uint32_t startAt,
                 uint32_t step, uint16_t volL, uint16_t volR, uint8_t flags)
{
    if (size > MIX_MAX_SAMPLES) size = MIX_MAX_SAMPLES;
    if (size == 0) { ms_pcm_stop(ch); return; }
    if (startAt >= size) startAt = size - 1;
    // A loop shorter than a sample, or starting past the end, is no loop at all.
    uint32_t loopQ = (loopStart != MIX_NOLOOP && loopStart < size) ? (loopStart << 12) : MIX_NOLOOP;
    // Reverse plays from just before startAt's mirror image down to zero; there is no reverse loop.
    uint32_t startQ = (flags & MIX_F_REVERSE) ? ((size - 1 - startAt) << 12) : (startAt << 12);

    MixChan *m = &g_ms_mix[ch];
    uint16_t s = IRQ_SAVE();
    m->data   = data;
    m->startQ = startQ;
    m->endQ   = size << 12;
    m->loopQ  = (flags & MIX_F_REVERSE) ? MIX_NOLOOP : loopQ;
    m->step   = step;
    m->volL   = volL;
    m->volR   = volR;
    m->flags  = flags;
    m->active = 1;
    m->gen    = m->gen + 1;
    IRQ_RESTORE(s);
}

void ms_pcm_stop(int ch)
{
    g_ms_mix[ch].active = 0;
}

// Ease a block's volume toward its target: half the remaining distance per block (~1 ms), so a
// 60 Hz envelope step or a fresh note lands as a 3-4 ms ramp rather than a click.
#define EASE(cur, tgt) ((cur) + (((int32_t)(tgt) - (cur) + 1) >> 1))

IWRAM_CODE void ms_pcm_render(int8_t *outL, int8_t *outR)
{
    int32_t *aL = s_accL, *aR = s_accR;

    // Start each block from the echo of what went out before it (or silence).
    uint32_t rev = g_ms_reverb;
    if (rev) {
        uint32_t p = s_echoPos;
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            int32_t d = s_echo[(p + i) & (ECHO_LEN - 1)] + s_echo[(p + i + ECHO_TAP2) & (ECHO_LEN - 1)];
            int32_t v = d * (int32_t)rev;               // (d * 2 * rev) >> 9, in accumulator units (<< 8)
            aL[i] = v;
            aR[i] = v;
        }
    } else {
        for (int i = 0; i < AUDIO_BLOCK; i++) { aL[i] = 0; aR[i] = 0; }
    }

    for (int c = 0; c < MIX_CHANNELS; c++) {
        MixChan *m = &g_ms_mix[c];
        if (!m->active) continue;

        uint32_t gen = m->gen;
        if (gen != m->seen) {
            m->seen = gen;
            m->pos  = (int32_t)m->startQ;
            m->curL = 0;
            m->curR = 0;
        }
        if (m->doneGen == gen) continue;

        int32_t vl = EASE(m->curL, m->volL);
        int32_t vr = EASE(m->curR, m->volR);
        m->curL = vl;
        m->curR = vr;

        const int8_t *d = m->data;
        int32_t  pos  = m->pos;
        uint32_t step = m->step;
        uint32_t endQ = m->endQ;
        uint32_t loopQ = m->loopQ;

        if (m->flags & MIX_F_REVERSE) {
            for (int i = 0; i < AUDIO_BLOCK; i++) {
                int32_t s = d[pos >> 12];
                aL[i] += s * vl;
                aR[i] += s * vr;
                pos -= (int32_t)step;
                if (pos < 0) { m->doneGen = gen; break; }
            }
            m->pos = pos;
            continue;
        }

        // A loop shorter than one step would need many subtractions per sample; clamp the step to
        // the loop instead. Only a mangled tone ever gets near this, and it was going to alias.
        uint32_t loopLen = (loopQ < endQ) ? endQ - loopQ : 0;
        if (loopLen && step > loopLen) step = loopLen;

        if ((vl | vr) == 0) {
            // Silent (muted, or between envelope steps at zero): keep the position moving so an
            // unmute lands mid-note where the note really is, but skip the arithmetic.
            uint32_t u = (uint32_t)pos + (step << 4);
            if (u >= endQ) {
                if (!loopLen) { m->doneGen = gen; continue; }
                do u -= loopLen; while (u >= endQ);
            }
            m->pos = (int32_t)u;
            continue;
        }

        uint32_t u = (uint32_t)pos;
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            uint32_t ix = u >> 12;
            int32_t a = d[ix];
            int32_t b = d[ix + 1];
            int32_t s = a + (((b - a) * (int32_t)((u >> 4) & 0xFFu)) >> 8);
            aL[i] += s * vl;
            aR[i] += s * vr;
            u += step;
            if (u >= endQ) {
                if (!loopLen) { m->doneGen = gen; break; }
                do u -= loopLen; while (u >= endQ);
            }
        }
        m->pos = (int32_t)u;
    }

    // Output: back to sample units, master gain, SATURATE (m4a wraps; we never do), then the crush.
    uint32_t gain = g_ms_gain, hold = g_ms_hold;
    int8_t   mask = (int8_t)g_ms_mask;
    uint32_t p = s_echoPos;
    for (int i = 0; i < AUDIO_BLOCK; i++) {
        int32_t l = ((aL[i] >> 8) * (int32_t)gain) >> 4;
        int32_t r = ((aR[i] >> 8) * (int32_t)gain) >> 4;
        if (l > 127) l = 127; else if (l < -128) l = -128;
        if (r > 127) r = 127; else if (r < -128) r = -128;
        // Written even with the reverb off: REVERB can be turned up mid-song, and its first echo
        // must be what just played, not whatever was left in the ring.
        s_echo[(p + i) & (ECHO_LEN - 1)] = (int8_t)((l + r) >> 1);

        if (hold > 1) {
            if (s_holdCount == 0) { s_holdL = (int8_t)l; s_holdR = (int8_t)r; s_holdCount = (uint8_t)hold; }
            s_holdCount--;
            l = s_holdL;
            r = s_holdR;
        }
        outL[i] = (int8_t)(l & mask);
        outR[i] = (int8_t)(r & mask);
    }
    s_echoPos = (p + AUDIO_BLOCK) & (ECHO_LEN - 1);
}
