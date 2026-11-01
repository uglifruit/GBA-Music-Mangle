// eng_chaos.c — see eng_chaos.h.
#include "eng_chaos.h"

#define SLOTS 12

typedef struct {
    uint32_t ratio;     // step multiplier, Q16 (65536 = as written)
    uint8_t  pan;       // 0 as written, 1 left only, 2 right only
    uint8_t  vol;       // volume multiplier /256 (255 = as written)
} Dice;

static Dice     s_dice[SLOTS];
static uint32_t s_seed = 0x2545F491u, s_rng = 0x2545F491u;
static uint8_t  s_amount, s_free;

// Interval jumps: an octave down, a fifth down, a fourth up, a fifth up, an octave up, an octave and
// a fifth up. Q16 ratios of equal temperament.
static const uint32_t kJump[6] = { 32768, 43691, 87480, 98193, 131072, 196415 };

static uint32_t next(void)
{
    uint32_t x = s_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return s_rng = x;
}

void eng_chaos_set(uint8_t amount, uint8_t free) { s_amount = amount; s_free = free; }
void eng_chaos_reseed(void) { if (!s_free) s_rng = s_seed; }
void eng_chaos_reroll(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    if (!s_seed) s_seed = 1;
    s_rng = s_seed;
}

void eng_chaos_note(unsigned i, EngVoice *v, const EngRom *rom)
{
    // Four draws per note, always, so the pattern cannot drift with the amount.
    uint32_t r1 = next(), r2 = next(), r3 = next(), r4 = next();
    if (i >= SLOTS) return;
    Dice *d = &s_dice[i];
    d->ratio = 65536; d->pan = 0; d->vol = 255;
    unsigned c = s_amount;
    if (!c) return;
    if ((r1 & 255) < (c >> 1)) d->ratio = kJump[((r1 >> 8) & 255) % 6];
    if (((r1 >> 16) & 255) < c / 3) d->pan = (uint8_t)(1 + ((r1 >> 24) & 1));
    if ((r2 & 255) < (c >> 2)) v->reverse ^= 1;
    if (((r2 >> 8) & 255) < c / 3 && v->size > 1) {
        uint32_t s = (v->size * ((r2 >> 16) & 255)) >> 8;
        v->start = s < v->size ? s : v->size - 1;
    }
    if ((r3 & 255) < (c >> 1)) d->vol = (uint8_t)(96 + ((r3 >> 8) & 159));
    // Above half: a chance of playing raw cart bytes, from a random place, for the sample's length.
    // The cart's size is a power of two; the engine re-checks the range after this hook.
    if (c > 128 && ((r4 & 255) < c - 128) && rom->size > v->size + 2) {
        uint32_t off = (r4 >> 8) & (rom->size - 1) & ~3u;
        if (off > rom->size - v->size - 2) off = rom->size - v->size - 2;
        v->data = rom->base + off;
        if (v->loop != UINT32_MAX && v->loop >= v->size) v->loop = UINT32_MAX;
    }
}

void eng_chaos_apply(unsigned i, uint32_t *step, uint16_t *left, uint16_t *right)
{
    if (i >= SLOTS || !s_amount) return;
    const Dice *d = &s_dice[i];
    if (d->ratio != 65536) {
        uint32_t s = (uint32_t)(((uint64_t)*step * d->ratio) >> 16);
        *step = s < 1 ? 1 : s > 0x200000 ? 0x200000 : s;
    }
    if (d->vol != 255) {
        *left  = (uint16_t)((*left  * d->vol) >> 8);
        *right = (uint16_t)((*right * d->vol) >> 8);
    }
    if (d->pan == 1) *right = 0;
    else if (d->pan == 2) *left = 0;
}
