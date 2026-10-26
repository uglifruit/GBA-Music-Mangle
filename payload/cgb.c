// cgb.c — m4a PSG envelopes and register updates; audio.c retains DMA/master ownership.
// Ported from pret/pokeemerald src/m4a.c, CgbSound/MidiKeyToCgbFreq.
#include "cgb.h"
#include "player.h"
#include "rom.h"
#include "gba.h"

static uint8_t s_c15;

// MidiKeyToCgbFreq (m4a.c).
uint32_t cgb_key_to_freq(int chanNum, int key, int fine)
{
    if (chanNum == 4) {
        key = key <= 20 ? 0 : key - 21;
        if (key > 59) key = 59;
        return gNoiseTable[key];
    }
    if (key <= 35) { key = 0; fine = 0; }
    else { key -= 36; if (key > 130) { key = 130; fine = 255; } }
    unsigned a = gCgbScaleTable[key], b = gCgbScaleTable[key + 1];
    int v = gCgbFreqTable[a & 15] >> (a >> 4);
    int w = gCgbFreqTable[b & 15] >> (b >> 4);
    return v + ((fine * (w - v)) >> 8) + 2048;
}

// CgbOscOff (m4a.c). Detachment also makes the software channel available.
void cgb_off(int ch)
{
    switch (ch) {
    case 0: R8(0x63) = 8; R8(0x65) = 0x80; break;
    case 1: R8(0x69) = 8; R8(0x6D) = 0x80; break;
    case 2: R8(0x70) = 0; break;
    default: R8(0x79) = 8; R8(0x7D) = 0x80; break;
    }
    R8(0x81) &= ~g_cgb[ch].panMask;
    g_cgb[ch].status = 0;
    g_cgb[ch].track = PL_NONE;
}

// MPlayExtender register initialization (m4a.c); master sound belongs to audio_init.
void cgb_init(void)
{
    s_c15 = 0;
    R8(0x63) = R8(0x69) = R8(0x79) = 8;
    R8(0x65) = R8(0x6D) = R8(0x7D) = 0x80;
    R8(0x70) = 0; R8(0x80) = 0x77; R8(0x81) = 0;
    for (int i = 0; i < PL_CGB; ++i) {
        volatile uint8_t *p = (volatile uint8_t *)&g_cgb[i];
        for (unsigned j = 0; j < sizeof(Chan); ++j) p[j] = 0;
        g_cgb[i].type = i + 1;
        g_cgb[i].panMask = 0x11u << i;
        g_cgb[i].track = PL_NONE;
    }
}

void cgb_mute_changed(void)
{
    for (int i = 0; i < PL_CGB; ++i)
        if (g_cgb[i].status & SF_ON) g_cgb[i].modify |= CGB_MO_VOL;
}

// CgbPan/CgbModVol (m4a.c). Hardware envelopes have four volume bits.
static void cgb_mod_vol(Chan *c)
{
    unsigned r = c->rightVolume, l = c->leftVolume;
    c->pan = r >= l && r / 2 >= l ? 0x0F :
             l > r && l / 2 >= r ? 0xF0 : 0xFF;
    c->envGoal = (r + l) >> 4;
    if (c->envGoal > 15) c->envGoal = 15;
    c->sustainGoal = (c->envGoal * c->sustain + 15) >> 4;
    c->pan &= c->panMask;
}

// CgbSound (m4a.c), per-channel body. Kick supplies a nonzero correction phase.
static void cgb_step(int ch, int prevC15)
{
    Chan *c = &g_cgb[ch - 1];
    unsigned nrx0, nrx1, nrx2, nrx3, nrx4;
    int envelopeStepTimeAndDir;
    const int mask = 255;
    if (!(c->status & SF_ON)) return;
    if (ch == 3 && (c->status & SF_START) && !rom_ok(c->wav, 16)) {
        cgb_off(ch - 1); return;
    }
        /* 1. determine hardware channel registers */
        switch (ch)
        {
        case 1:
            nrx0 = 0x60;
            nrx1 = 0x62;
            nrx2 = 0x63;
            nrx3 = 0x64;
            nrx4 = 0x65;
            break;
        case 2:
            nrx0 = 0x61;
            nrx1 = 0x68;
            nrx2 = 0x69;
            nrx3 = 0x6C;
            nrx4 = 0x6D;
            break;
        case 3:
            nrx0 = 0x70;
            nrx1 = 0x72;
            nrx2 = 0x73;
            nrx3 = 0x74;
            nrx4 = 0x75;
            break;
        default:
            nrx0 = 0x71;
            nrx1 = 0x78;
            nrx2 = 0x79;
            nrx3 = 0x7C;
            nrx4 = 0x7D;
            break;
        }

        /* The caller supplies the frame correction phase. */
        envelopeStepTimeAndDir = R8(nrx2);

        /* 2. calculate envelope volume */
        if (c->status & SF_START)
        {
            if (!(c->status & SF_STOP))
            {
                c->status = SF_ATTACK;
                c->modify = CGB_MO_PIT | CGB_MO_VOL;
                cgb_mod_vol(c);
                switch (ch)
                {
                case 1:
                    R8(nrx0) = c->sweep;
                    // fallthrough
                case 2:
                    R8(nrx1) = ((uint32_t)c->wav << 6) + c->length;
                    goto init_env_step_time_dir;
                case 3:
                    if (c->wav != c->curWave)
                    {
                        R8(nrx0) = 0x40;
                        R32(0x90) = rom_u32(c->wav + 0);
                        R32(0x94) = rom_u32(c->wav + 4);
                        R32(0x98) = rom_u32(c->wav + 8);
                        R32(0x9C) = rom_u32(c->wav + 12);
                        c->curWave = c->wav;
                    }
                    R8(nrx0) = 0;
                    R8(nrx1) = c->length;
                    if (c->length)
                        c->n4 = 0xC0;
                    else
                        c->n4 = 0x80;
                    break;
                default:
                    R8(nrx1) = c->length;
                    R8(nrx3) = (uint32_t)c->wav << 3;
                init_env_step_time_dir:
                    envelopeStepTimeAndDir = c->attack + 8;
                    if (c->length)
                        c->n4 = 0x40;
                    else
                        c->n4 = 0x00;
                    break;
                }
                c->envCounter = c->attack;
                if ((int8_t)(c->attack & mask))
                {
                    c->envVol = 0;
                    goto envelope_step_complete;
                }
                else
                {
                    // skip attack phase if attack is instantaneous (=0)
                    goto envelope_decay_start;
                }
            }
            else
            {
                goto oscillator_off;
            }
        }
        else if (c->status & SF_IEC)
        {
            c->echoLen--;
            if ((int8_t)(c->echoLen & mask) <= 0)
            {
            oscillator_off:
                cgb_off(ch - 1);
                c->status = 0;
                goto channel_complete;
            }
            goto envelope_complete;
        }
        else if ((c->status & SF_STOP) && (c->status & SF_ENV))
        {
            c->status &= ~SF_ENV;
            c->envCounter = c->release;
            if ((int8_t)(c->release & mask))
            {
                c->modify |= CGB_MO_VOL;
                if (ch != 3)
                    envelopeStepTimeAndDir = c->release | 0;
                goto envelope_step_complete;
            }
            else
            {
                goto envelope_pseudoecho_start;
            }
        }
        else
        {
        envelope_step_repeat:
            if (c->envCounter == 0)
            {
                if (ch == 3)
                    c->modify |= CGB_MO_VOL;

                cgb_mod_vol(c);
                if ((c->status & SF_ENV) == SF_RELEASE)
                {
                    c->envVol--;
                    if ((int8_t)(c->envVol & mask) <= 0)
                    {
                    envelope_pseudoecho_start:
                        c->envVol = ((c->envGoal * c->echoVol) + 0xFF) >> 8;
                        if (c->envVol)
                        {
                            c->status |= SF_IEC;
                            c->modify |= CGB_MO_VOL;
                            if (ch != 3)
                                envelopeStepTimeAndDir = 0 | 8;
                            goto envelope_complete;
                        }
                        else
                        {
                            goto oscillator_off;
                        }
                    }
                    else
                    {
                        c->envCounter = c->release;
                    }
                }
                else if ((c->status & SF_ENV) == SF_SUSTAIN)
                {
                envelope_sustain:
                    c->envVol = c->sustainGoal;
                    c->envCounter = 7;
                }
                else if ((c->status & SF_ENV) == SF_DECAY)
                {
                    int envVol, sustainGoal;

                    c->envVol--;
                    envVol = (int8_t)(c->envVol & mask);
                    sustainGoal = (int8_t)(c->sustainGoal);
                    if (envVol <= sustainGoal)
                    {
                    envelope_sustain_start:
                        if (c->sustain == 0)
                        {
                            c->status &= ~SF_ENV;
                            goto envelope_pseudoecho_start;
                        }
                        else
                        {
                            c->status--;
                            c->modify |= CGB_MO_VOL;
                            if (ch != 3)
                                envelopeStepTimeAndDir = 0 | 8;
                            goto envelope_sustain;
                        }
                    }
                    else
                    {
                        c->envCounter = c->decay;
                    }
                }
                else
                {
                    c->envVol++;
                    if ((uint8_t)(c->envVol & mask) >= c->envGoal)
                    {
                    envelope_decay_start:
                        c->status--;
                        c->envCounter = c->decay;
                        if ((uint8_t)(c->envCounter & mask))
                        {
                            c->modify |= CGB_MO_VOL;
                            c->envVol = c->envGoal;
                            if (ch != 3)
                                envelopeStepTimeAndDir = c->decay | 0;
                        }
                        else
                        {
                            goto envelope_sustain_start;
                        }
                    }
                    else
                    {
                        c->envCounter = c->attack;
                    }
                }
            }
        }

    envelope_step_complete:
        // every 15 frames, envelope calculation has to be done twice
        // to keep up with the hardware envelope rate (1/64 s)
        c->envCounter--;
        if (prevC15 == 0)
        {
            prevC15--;
            goto envelope_step_repeat;
        }

    envelope_complete:
        /* 3. apply pitch to HW registers */
        if (c->modify & CGB_MO_PIT)
        {
            if (ch < 4 && (c->type & TONEDATA_TYPE_FIX))
            {
                int dac_pwm_rate = R8(0x89);

                if (dac_pwm_rate < 0x40)        // if PWM rate = 32768 Hz
                    c->frequency = (c->frequency + 2) & 0x7fc;
                else if (dac_pwm_rate < 0x80)   // if PWM rate = 65536 Hz
                    c->frequency = (c->frequency + 1) & 0x7fe;
            }

            if (ch != 4)
                R8(nrx3) = c->frequency;
            else
                R8(nrx3) = (R8(nrx3) & 0x08) | c->frequency;
            c->n4 = (c->n4 & 0xC0) + (c->frequency >> 8);
            R8(nrx4) = (int8_t)(c->n4 & mask);
        }

        /* 4. apply envelope & volume to HW registers */
        if (c->modify & CGB_MO_VOL)
        {
            R8(0x81) = (R8(0x81) & ~c->panMask) |
                ((c->track < PL_TRACKS && (g_mods.mute & (1u << c->track))) ? 0 : c->pan);
            if (ch == 3)
            {
                R8(nrx2) = gCgb3Vol[c->envVol & 15];
                if (c->n4 & 0x80)
                {
                    R8(nrx0) = 0x80;
                    R8(nrx4) = c->n4;
                    c->n4 &= 0x7f;
                }
            }
            else
            {
                uint32_t envMask = 0xF;
                R8(nrx2) = (envelopeStepTimeAndDir & envMask) + (c->envVol << 4);
                R8(nrx4) = c->n4 | 0x80;
                if (ch == 1 && !(R8(nrx0) & 0x08))
                    R8(nrx4) = c->n4 | 0x80;
            }
        }

    channel_complete:
        c->modify = 0;
}

void cgb_frame(void)
{
    s_c15 = s_c15 ? s_c15 - 1 : 14;
    for (int i = 1; i <= PL_CGB; ++i) cgb_step(i, s_c15);
}

void cgb_kick(void)
{
    for (int i = 1; i <= PL_CGB; ++i)
        if (g_cgb[i - 1].status & SF_START) cgb_step(i, 1);
}
