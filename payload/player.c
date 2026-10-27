// player.c — bounded MP2K sequencer and voice control, externally clocked at 24 PPQN.
// Behaviour follows pret/pokeemerald m4a.c/m4a_1.s; PLAYER_SPEC.md defines departures.
#include "player.h"
#include "mixer.h"
#include "rom.h"
#include "scan.h"
#include "cgb.h"
#include "gba.h"

PlayerMods g_mods = { 0, 0, 0, 0, 0, 4096, 0, 0, 0x100, 0, 0, 0, 0, 0, 0, 0, 0, 22 };
Track g_tracks[PL_TRACKS];
Chan g_ds[PL_DS_MAX], g_cgb[PL_CGB];
uint32_t g_plHeader, g_plTicks;
uint8_t g_plTrackCount, g_plBeats, g_plLoops;
uint16_t g_plTempoD = 150;
uint8_t g_plMaxChans = 8;
uint16_t g_plGameRate = 13379;
uint8_t g_plRepeat = 1;

static uint32_t s_entry, s_tone, s_rng = 0x6D2B79F5u;
static uint8_t s_priority;
static int16_t s_pitch;
static uint16_t s_mute;
// EOT releases the newest matching note, as m4a's head-inserted channel chain does.
static uint32_t s_order, s_ds_order[PL_DS_MAX], s_cgb_order[PL_CGB];
static uint8_t s_mem[256];
// The sound hooks: the song's own reverb (the header's), and the global LFO's phase and outputs.
static uint8_t s_songReverb;
static uint8_t s_lfoPhase;
static int16_t s_lfoPitch;               // 1/256 semitone, added like m4a's MODT 0
static int16_t s_lfoVol;                 // volume x (128 + v) / 128, like m4a's MODT 1

// Volatile byte stores prevent GCC synthesizing libc calls in freestanding builds.
static void clear_bytes(void *dst, unsigned size)
{
    volatile uint8_t *p = (volatile uint8_t *)dst;
    for (unsigned i = 0; i < size; ++i) p[i] = 0;
}

static inline __attribute__((always_inline)) int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// x moved toward hi (v > 0) or lo (v < 0) by |v|/2048 of the way. x is clamped into [lo, hi] first:
// a mangled voice can carry any byte.
static unsigned bend_toward(unsigned x, int v, unsigned lo, unsigned hi)
{
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    if (v > 0) return x + (((hi - x) * (unsigned)v + 1024u) >> 11);      // rounded: full scale reaches hi
    if (v < 0) return x - (((x - lo) * (unsigned)-v + 1024u) >> 11);
    return x;
}

// The envelope hooks, applied where each rate is read. m4a's two envelope models run opposite ways:
//   DirectSound  attack is added per frame (255 = instant), decay and release multiply per frame
//                (/256, so 255 = slowest), sustain is a level 0..255.
//   PSG          attack, decay and release are step times 0..7 (0 = instant, 7 = slowest),
//                sustain a level 0..15.
// "Longer" therefore means toward 1 for a DirectSound attack and toward the top everywhere else.
unsigned pl_env(const Chan *c, int which)
{
    int cgb = (c->type & TONEDATA_TYPE_CGB) != 0;
    switch (which) {
    case PL_ENV_ATTACK:
        return cgb ? bend_toward(c->attack, g_mods.attack, 0, 7)
                   : bend_toward(c->attack, -g_mods.attack, 1, 255);
    case PL_ENV_DECAY:
        return bend_toward(c->decay, g_mods.decay, 0, cgb ? 7 : 255);
    case PL_ENV_SUSTAIN:
        return bend_toward(c->sustain, g_mods.sustain, 0, cgb ? 15 : 255);
    default:
        return bend_toward(c->release, g_mods.release, 0, cgb ? 7 : 255);
    }
}

// NOTE LENGTH: a gate time scaled x1/4 .. x4. 0 (a TIE, held until EOT) is left alone.
static unsigned gate_len(unsigned g)
{
    int v = g_mods.gate;
    if (!g || !v) return g;
    if (v > 0) g += (g * 3u * (unsigned)v) >> 11;
    else       g -= (g * 3u * (unsigned)-v) >> 13;
    return g < 1 ? 1 : g > 255 ? 255 : g;
}

static unsigned channel_cap(void)
{
    return g_plMaxChans > PL_DS_MAX ? PL_DS_MAX : g_plMaxChans;
}

static void ds_off(int i)
{
    g_ds[i].status = 0;
    g_ds[i].track = PL_NONE;
    mixer_stop(i);
}

// ply_fine / ClearChain (m4a_1.s): release tails survive track detachment.
static void track_stop(int n)
{
    for (int i = 0; i < PL_DS_MAX; ++i) if (g_ds[i].track == n) {
        if (g_ds[i].status & SF_ON) g_ds[i].status |= SF_STOP;
        g_ds[i].track = PL_NONE;
    }
    for (int i = 0; i < PL_CGB; ++i) if (g_cgb[i].track == n) {
        if (g_cgb[i].status & SF_ON) g_cgb[i].status |= SF_STOP;
        g_cgb[i].track = PL_NONE;
    }
    g_tracks[n].flags = 0;
    g_tracks[n].gate = 0;
}

// TrkVolPitSet (m4a.c). Use multiplication for signed semitone shifts (no signed-shift UB).
static void track_vol_pitch(Track *t)
{
    if (t->flags & MPT_FLG_VOLSET) {
        uint32_t x = (t->vol * t->volX) >> 5;
        if (t->modT == 1) x = (x * (t->modM + 128)) >> 7;
        if (s_lfoVol) { x = (x * (uint32_t)(128 + s_lfoVol)) >> 7; if (x > 255) x = 255; }
        int y = 2 * t->pan + t->panX;
        if (t->modT == 2) y += t->modM;
        y = clamp(y, -128, 127);
        t->volMR = ((y + 128) * x) >> 8;
        t->volML = ((127 - y) * x) >> 8;
    }
    if (t->flags & MPT_FLG_PITSET) {
        int x = (t->tune + t->bend * t->bendRange) * 4 +
                (t->keyShift + t->keyShiftX) * 256 + t->pitX;
        if (t->modT == 0) x += 16 * t->modM;
        x += s_lfoPitch;
        t->keyM = x >> 8;
        t->pitM = x;
    }
    t->flags &= ~(MPT_FLG_VOLSET | MPT_FLG_PITSET);
}

// ChnVolSetAsm (m4a_1.s).
static void channel_vol(Chan *c, const Track *t)
{
    unsigned r = (t->volMR * (128 + c->rhythmPan) * c->velocity) >> 14;
    unsigned l = (t->volML * (127 - c->rhythmPan) * c->velocity) >> 14;
    c->rightVolume = r > 255 ? 255 : r;
    c->leftVolume = l > 255 ? 255 : l;
}

static void ds_volume(int i)
{
    Chan *c = &g_ds[i];
    unsigned env = c->envVol;
    if (c->track < PL_TRACKS && (g_mods.mute & (1u << c->track))) env = 0;
    g_mix[i].volR = (c->rightVolume * env) >> 8;
    g_mix[i].volL = (c->leftVolume * env) >> 8;
}

// MidiKeyToFreq (m4a.c); the renderer needs Q12 step rather than Hz.
static uint32_t ds_step(const Chan *c, const Track *t)
{
    uint32_t step;
    if (!c->sFreq) step = g_plGameRate >> 2;
    else {
        int key = c->key + t->keyM;
        unsigned fine = t->pitM;
        if (key < 0) key = 0;
        if (key > 178) { key = 178; fine = 255; }
        unsigned a = gScaleTable[key], b = gScaleTable[key + 1];
        uint32_t v = gFreqTable[a & 15] >> (a >> 4);
        uint32_t w = gFreqTable[b & 15] >> (b >> 4);
        uint32_t ratio = v + (uint32_t)(((uint64_t)(w - v) * fine) >> 8);
        step = ((uint64_t)c->sFreq * ratio) >> 34;
    }
    if (step < 1) step = 1;
    if (step > 0x200000) step = 0x200000;
    return step;
}

// clear_modM (m4a_1.s).
static void clear_mod(Track *t)
{
    t->modM = 0; t->lfoSpeedC = 0;
    t->flags |= t->modT ? MPT_FLG_VOLCHG : MPT_FLG_PITCHG;
}

// MPlayMain tail / m4aMPlayPitchControl (m4a.c).
// IWRAM: clock_service() calls it every millisecond. The helpers it reaches only on a change
// (volume, pitch, mute) stay in EWRAM.
IWRAM_CODE void player_update(void)
{
    if (s_pitch != g_mods.pitch) {
        s_pitch = g_mods.pitch;
        for (int n = 0; n < PL_TRACKS; ++n) if (g_tracks[n].flags & MPT_FLG_EXIST) {
            g_tracks[n].keyShiftX = s_pitch >> 8;
            g_tracks[n].pitX = s_pitch & 255;
            g_tracks[n].flags |= MPT_FLG_PITCHG;
        }
    }
    if (s_mute != g_mods.mute) {
        s_mute = g_mods.mute;
        for (int i = 0; i < PL_DS_MAX; ++i) ds_volume(i);
        cgb_mute_changed();
    }
    for (unsigned i = channel_cap(); i < PL_DS_MAX; ++i)
        if (g_ds[i].status & SF_ON) ds_off(i);
    if (g_plHeader) g_mixReverb = (uint8_t)clamp(s_songReverb + g_mods.reverb, 0, 127);
    for (int n = 0; n < PL_TRACKS; ++n) {
        Track *t = &g_tracks[n];
        if (!(t->flags & MPT_FLG_EXIST) || !(t->flags & 15)) continue;
        track_vol_pitch(t);
        unsigned flags = t->flags;
        for (int i = 0; i < PL_DS_MAX; ++i) if (g_ds[i].track == n) {
            if (flags & MPT_FLG_VOLCHG) { channel_vol(&g_ds[i], t); ds_volume(i); }
            if (flags & MPT_FLG_PITCHG) g_mix[i].step = ds_step(&g_ds[i], t);
        }
        for (int i = 0; i < PL_CGB; ++i) if (g_cgb[i].track == n) {
            Chan *c = &g_cgb[i];
            if (flags & MPT_FLG_VOLCHG) { channel_vol(c, t); c->modify |= CGB_MO_VOL; }
            if (flags & MPT_FLG_PITCHG) {
                c->frequency = cgb_key_to_freq(i + 1, clamp(c->key + t->keyM, 0, 255), t->pitM);
                c->modify |= CGB_MO_PIT;
            }
        }
        t->flags &= 0xF0;
    }
}

// SoundMainRAM envelope section (m4a_1.s). No mixer position state is touched here.
static void ds_envelope(int i)
{
    Chan *c = &g_ds[i];
    unsigned env = c->envVol;
    if (c->status & SF_IEC) {
        if (!c->echoLen || --c->echoLen == 0) { ds_off(i); return; }
    } else if (c->status & SF_STOP) {
        env = (env * pl_env(c, PL_ENV_RELEASE)) >> 8;
        if (env <= c->echoVol) goto echo;
    } else if ((c->status & SF_ENV) == SF_DECAY) {
        unsigned sus = pl_env(c, PL_ENV_SUSTAIN);
        env = (env * pl_env(c, PL_ENV_DECAY)) >> 8;
        if (env <= sus) {
            env = sus;
            if (!env) goto echo;
            --c->status;
        }
    } else if ((c->status & SF_ENV) == SF_ATTACK) {
        env += pl_env(c, PL_ENV_ATTACK);
        if (env >= 255) { env = 255; --c->status; }
    }
    c->envVol = env;
    ds_volume(i);
    return;
echo:
    if (!c->echoVol) { ds_off(i); return; }
    c->envVol = c->echoVol;
    c->status |= SF_IEC;
    ds_volume(i);
}

// The global LFO (VIBRATO / TREMOLO), per frame so its rate does not follow the clock. A triangle,
// -128..126, scaled like m4a's (wave x MOD) >> 6. Tracks are flagged only when an output changes.
static void lfo_frame(void)
{
    int pv = 0, vv = 0;
    if (g_mods.vibrato || g_mods.tremolo) {
        s_lfoPhase = (uint8_t)(s_lfoPhase + g_mods.lfoSpeed);
        int tri = s_lfoPhase < 128 ? (int)s_lfoPhase - 64 : 191 - (int)s_lfoPhase;
        int wave = tri * 2;
        pv = 16 * ((wave * (g_mods.vibrato > 63 ? 63 : g_mods.vibrato)) >> 6);
        vv = (wave * (g_mods.tremolo > 63 ? 63 : g_mods.tremolo)) >> 6;
    }
    unsigned flags = (pv != s_lfoPitch ? MPT_FLG_PITCHG : 0) | (vv != s_lfoVol ? MPT_FLG_VOLCHG : 0);
    if (!flags) return;
    s_lfoPitch = (int16_t)pv;
    s_lfoVol   = (int16_t)vv;
    for (int n = 0; n < PL_TRACKS; ++n)
        if (g_tracks[n].flags & MPT_FLG_EXIST) g_tracks[n].flags |= (uint8_t)flags;
}

void player_frame(void)
{
    lfo_frame();
    for (int i = 0; i < PL_DS_MAX; ++i) if (g_ds[i].status & SF_ON) {
        if (mixer_done(i)) ds_off(i);
        else ds_envelope(i);
    }
    cgb_frame();
    for (int n = 0; n < PL_TRACKS; ++n) {
        unsigned a = g_tracks[n].act, d = (a >> 3) + 1;
        g_tracks[n].act = a > d ? a - d : 0;
    }
}

// MPlayStart / MPlayStop (m4a.c).
void player_stop(void)
{
    for (int i = 0; i < PL_DS_MAX; ++i) ds_off(i);
    for (int i = 0; i < PL_CGB; ++i) cgb_off(i);
    for (int n = 0; n < PL_TRACKS; ++n) { g_tracks[n].flags = 0; g_tracks[n].gate = 0; }
    g_plHeader = 0;
}

void player_init(void)
{
    clear_bytes(g_tracks, sizeof(g_tracks));
    clear_bytes(g_ds, sizeof(g_ds));
    clear_bytes(s_mem, sizeof(s_mem));
    cgb_init();
    player_stop();
    s_pitch = g_mods.pitch; s_mute = g_mods.mute;
}

int player_start(uint32_t songEntry)
{
    int tc;
    if (!rom_ok(songEntry, SONG_ENTRY_SIZE)) return 0;
    uint32_t hdr = rom_u32(songEntry);
    if (!scan_header_ok(hdr, &tc) || tc < 1 || tc > PL_TRACKS) return 0;
    player_stop();
    clear_bytes(g_tracks, sizeof(g_tracks));
    for (int n = 0; n < tc; ++n) {
        g_tracks[n].flags = MPT_FLG_EXIST | MPT_FLG_START;
        g_tracks[n].cmd = rom_u32(SH_PART(hdr, n));
    }
    s_entry = songEntry;
    s_tone = rom_u32(SH_TONE(hdr)); s_priority = rom_u8(SH_PRIORITY(hdr));
    g_plTempoD = 150; g_plTicks = 0;
    unsigned reverb = rom_u8(SH_REVERB(hdr));
    s_songReverb = reverb & 128 ? reverb & 127 : 0;
    g_mixReverb = (uint8_t)clamp(s_songReverb + g_mods.reverb, 0, 127);
    g_plHeader = hdr; g_plTrackCount = tc;
    return 1;
}

int player_running(void) { return g_plHeader != 0; }
uint32_t player_tempo_i(void) { return ((uint32_t)g_plTempoD * g_mods.tempoU) >> 8; }

static uint32_t tone_word(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_tone(uint8_t *tone, uint32_t addr)
{
    if (!rom_ok(addr, TONE_SIZE)) return 0;
    for (int j = 0; j < TONE_SIZE; ++j) tone[j] = rom_u8(addr + j);
    return 1;
}

// ply_note allocation (m4a_1.s): prefer free, then releasing, then priority/track order.
static int allocate_ds(unsigned priority, unsigned track)
{
    int best = -1, releasing = 0;
    unsigned owner = track, pri = priority;
    for (unsigned i = 0; i < channel_cap(); ++i) {
        Chan *c = &g_ds[i];
        if (mixer_done(i)) ds_off(i);
        if (!(c->status & SF_ON)) return i;
        if (c->status & SF_STOP) {
            if (!releasing) {
                releasing = 1; pri = c->priority; owner = c->track; best = i;
                continue;
            }
        } else if (releasing) continue;
        if (c->priority < pri || (c->priority == pri && c->track >= owner)) {
            pri = c->priority; owner = c->track; best = i;
        }
    }
    return best;
}

// WaveData validation and the deliberate raw-ROM/scrub departures in PLAYER_SPEC §5.7.
static int sample_resolve(Chan *c, unsigned type, uint32_t wav)
{
    uint32_t size = 0, data = 0, freq = 0, loop = MIX_NOLOOP;
    int valid = 0;
    if (!(type & TONEDATA_TYPE_CMP) && rom_ok(wav, 16) && rom_u16(wav) == 0) {
        size = rom_u32(WAV_SIZE(wav));
        if (size > MIX_MAX_SAMPLES) size = MIX_MAX_SAMPLES;
        data = WAV_DATA(wav); freq = rom_u32(WAV_FREQ(wav));
        valid = size && rom_ok(data, size + 1) && (freq || (type & TONEDATA_TYPE_FIX));
        if (rom_u8(WAV_FLAGS(wav)) & WAVE_DATA_FLAG_LOOP) loop = rom_u32(WAV_LOOP(wav));
        if (type & TONEDATA_TYPE_FIX) freq = 0;
    }
    if (!valid) {
        if (!(g_mods.tableOffset || g_mods.romScrub) || !rom_isptr(wav)) return 0;
        data = wav; size = g_romEnd - wav - 1;
        if (size > g_mods.scrubLen) size = g_mods.scrubLen;
        if (size > MIX_MAX_SAMPLES) size = MIX_MAX_SAMPLES;
        if (!size) return 0;
        loop = 0; freq = 13379u << 10;
    }
    if (g_mods.romScrub) {
        if (size + 1 > g_romSize) return 0;
        data += (uint32_t)g_mods.romScrub;
        if (!rom_ok(data, size + 1)) data = ROM_BASE + ((data - ROM_BASE) & (g_romSize - 1));
        if (!rom_ok(data, size + 1)) data = g_romEnd - size - 1;
    }
    if (!rom_ok(data, size + 1)) return 0;
    c->sData = data; c->sSize = size; c->sLoop = loop; c->sFreq = freq;
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    c->sFlags = (!!(type & TONEDATA_TYPE_REV) ^ ((uint8_t)s_rng < g_mods.reverseChance)) ? MIX_F_REVERSE : 0;
    return 1;
}

// ply_note (m4a_1.s). All follower updates precede fallible voice/sample resolution.
static void note_on(int n, unsigned cmd)
{
    Track *t = &g_tracks[n];
    t->gateTime = gClockTable[cmd - CMD_TIE];
    if (rom_u8(t->cmd) < 128) {
        t->key = rom_u8(t->cmd++);
        if (rom_u8(t->cmd) < 128) {
            t->velocity = rom_u8(t->cmd++);
            if (rom_u8(t->cmd) < 128) t->gateTime += rom_u8(t->cmd++);
        }
    }
    ++t->notes; t->lastKey = clamp(t->key + t->keyShift, 0, 127);
    unsigned gate = gate_len(t->gateTime);
    t->act = t->velocity; t->gate = 1; t->gateLeft = gate;
    uint8_t tone[TONE_SIZE];
    if (g_mods.voiceShift || g_mods.tableOffset) {
        uint32_t addr = s_tone + (int32_t)g_mods.tableOffset +
                        12u * ((t->voice + g_mods.voiceShift) & 127);
        if (!read_tone(tone, addr)) return;
    } else for (int j = 0; j < TONE_SIZE; ++j) tone[j] = t->tone[j];
    unsigned parent = tone[0], key = t->key;
    int pan = 0;
    if (parent & (TONEDATA_TYPE_SPL | TONEDATA_TYPE_RHY)) {
        unsigned ix = key;
        if (parent & TONEDATA_TYPE_SPL) {
            uint32_t split = tone_word(tone + 8);
            if (!rom_ok(split, key + 1)) return;
            ix = rom_u8(split + key);
        }
        uint32_t sub = tone_word(tone + 4);
        if (!rom_ok(sub, ix * 12 + TONE_SIZE) || !read_tone(tone, sub + ix * 12)) return;
        if (tone[0] & (TONEDATA_TYPE_SPL | TONEDATA_TYPE_RHY)) return;
        if (parent & TONEDATA_TYPE_RHY) {
            key = tone[1];
            if (tone[3] & 128) pan = (tone[3] - TONEDATA_P_S_PAN) * 2;
        }
    }
    unsigned type = tone[0], kind = type & TONEDATA_TYPE_CGB;
    uint32_t wav = tone_word(tone + 4);
    if (kind > PL_CGB || (type & TONEDATA_TYPE_CMP)) return;
    if (kind == 3 && !rom_ok(wav, 16)) return;
    unsigned priority = s_priority + t->priority;
    if (priority > 255) priority = 255;
    int i;
    Chan *c;
    // Validate a DS sample before stealing a live channel.
    Chan resolved;
    if (!kind) {
        if (!sample_resolve(&resolved, type, wav)) return;
        i = allocate_ds(priority, n);
        if (i < 0) return;
        c = &g_ds[i];
        ds_off(i);
        c->sData = resolved.sData; c->sSize = resolved.sSize;
        c->sLoop = resolved.sLoop; c->sFreq = resolved.sFreq; c->sFlags = resolved.sFlags;
        s_ds_order[i] = ++s_order;
    } else {
        i = kind - 1; c = &g_cgb[i];
        if ((c->status & SF_ON) && !(c->status & SF_STOP) &&
            (c->priority > priority || (c->priority == priority && c->track < n))) return;
        s_cgb_order[i] = ++s_order;
    }
    t->lfoDelayC = t->lfoDelay;
    if (t->lfoDelay) clear_mod(t);
    track_vol_pitch(t);
    c->track = n; c->priority = priority; c->key = key; c->midiKey = t->key;
    c->velocity = t->velocity; c->rhythmPan = pan; c->type = type; c->wav = wav;
    c->gateTime = gate;
    c->attack = tone[8]; c->decay = tone[9]; c->sustain = tone[10]; c->release = tone[11];
    c->echoVol = t->echoVol; c->echoLen = t->echoLen;
    channel_vol(c, t);
    if (kind) {
        c->length = tone[2]; c->sweep = tone[3];
        if ((c->sweep & 128) || !(c->sweep & 0x70)) c->sweep = 8;
        c->frequency = cgb_key_to_freq(kind, clamp(key + t->keyM, 0, 255), t->pitM);
        c->status = SF_START;
    } else {
        c->envVol = 0;
        c->status = SF_ATTACK | (c->sLoop < c->sSize ? SF_LOOP : 0);
        // START's first attack is immediate, before the first audio block.
        ds_envelope(i);
        uint32_t start = t->startOff;
        uint32_t offset = (c->sSize * g_mods.sampleStart) >> 8;
        // Saturate before handing startAt to the mixer's bounds clamp.
        if (start > c->sSize || offset > c->sSize - start) start = c->sSize;
        else start += offset;
        mixer_start(i, (const int8_t *)ROM_PTR(c->sData), c->sSize, c->sLoop, start,
                    ds_step(c, t), g_mix[i].volL, g_mix[i].volR, c->sFlags);
    }
    // Preserve pending propagation for older notes on this track until player_update.
}

// ply_endtie (m4a_1.s): the first matching entry in the original newest-first chain.
static void end_tie(int n)
{
    Track *t = &g_tracks[n];
    if (rom_u8(t->cmd) < 128) t->key = rom_u8(t->cmd++);
    Chan *best = 0; uint32_t order = 0;
    for (int i = 0; i < PL_DS_MAX + PL_CGB; ++i) {
        Chan *c = i < PL_DS_MAX ? &g_ds[i] : &g_cgb[i - PL_DS_MAX];
        uint32_t stamp = i < PL_DS_MAX ? s_ds_order[i] : s_cgb_order[i - PL_DS_MAX];
        if (c->track == n && (c->status & (SF_START | SF_ENV)) && !(c->status & SF_STOP) &&
            c->midiKey == t->key && (!best || (int32_t)(stamp - order) > 0)) {
            best = c; order = stamp;
        }
    }
    if (best) best->status |= SF_STOP;
    t->gate = 0;
}

// ply_memacc (m4a.c): only this private byte-indexed scratch area is writable.
static void memacc(Track *t)
{
    unsigned op = rom_u8(t->cmd++), ix = rom_u8(t->cmd++), data = rom_u8(t->cmd++);
    if (op < 6) {
        if (op >= 3) data = s_mem[data];
        switch (op % 3) {
        case 0: s_mem[ix] = data; break;
        case 1: s_mem[ix] += data; break;
        default: s_mem[ix] -= data; break;
        }
    } else if (op <= 17) {
        if (op >= 12) data = s_mem[data];
        int take;
        switch ((op - 6) % 6) {
        case 0: take = s_mem[ix] == data; break;
        case 1: take = s_mem[ix] != data; break;
        case 2: take = s_mem[ix] > data; break;
        case 3: take = s_mem[ix] >= data; break;
        case 4: take = s_mem[ix] <= data; break;
        default: take = s_mem[ix] < data; break;
        }
        uint32_t target = rom_u32(t->cmd);
        t->cmd = take ? target : t->cmd + 4;
    }
}

// ply_x* (m4a.c). xWAIT rewinds to the XCMD opcode and consumes another tick.
static void xcmd(int n, uint32_t command_addr)
{
    Track *t = &g_tracks[n];
    unsigned sub = rom_u8(t->cmd++);
    if (sub == 0 || sub == 3 || sub > 13) { track_stop(n); return; }
    if (sub == 1) {
        for (int j = 4; j < 8; ++j) t->tone[j] = rom_u8(t->cmd++);
    } else if (sub == 12) {
        unsigned len = rom_u16(t->cmd);
        if (t->timer < len) { ++t->timer; t->cmd = command_addr; t->wait = 1; }
        else { t->timer = 0; t->cmd += 2; }
    } else if (sub == 13) { t->startOff = rom_u32(t->cmd); t->cmd += 4; }
    else {
        unsigned v = rom_u8(t->cmd++);
        switch (sub) {
        case 2: t->tone[0] = v; break;
        case 4: case 5: case 6: case 7: t->tone[sub + 4] = v; break;
        case 8: t->echoVol = v; break;
        case 9: t->echoLen = v; break;
        case 10: t->tone[2] = v; break;
        case 11: t->tone[3] = v; break;
        }
    }
}

static void jump_track(Track *t, uint32_t from, int count_loop)
{
    uint32_t dest = rom_u32(t->cmd);
    if (count_loop && dest <= from) ++g_plLoops;
    t->cmd = dest;
}

// MPlayMain command dispatcher / ply_* (m4a_1.s).
static void command(int n, unsigned cmd, uint32_t from, int leader)
{
    Track *t = &g_tracks[n];
    if (cmd >= CMD_TIE) { note_on(n, cmd); return; }
    if (cmd <= 0xB0) { t->wait = gClockTable[cmd - CMD_WAIT0]; return; }
    switch (cmd) {
    case CMD_FINE: track_stop(n); break;
    case CMD_GOTO: jump_track(t, from, leader); break;
    case CMD_PATT:
        if (t->patternLevel >= 3) track_stop(n);
        else { t->stack[t->patternLevel++] = t->cmd + 4; jump_track(t, from, 0); }
        break;
    case CMD_PEND: if (t->patternLevel) t->cmd = t->stack[--t->patternLevel]; break;
    case CMD_REPT: {
        unsigned count = rom_u8(t->cmd++);
        if (!count) jump_track(t, from, leader);
        else if (++t->repN < count) jump_track(t, from, 0);
        else { t->repN = 0; t->cmd += 4; }
        break;
    }
    case CMD_MEMACC: memacc(t); break;
    case CMD_PRIO: t->priority = rom_u8(t->cmd++); break;
    case CMD_TEMPO: g_plTempoD = rom_u8(t->cmd++) * 2; break;
    case CMD_KEYSH: t->keyShift = rom_u8(t->cmd++); t->flags |= MPT_FLG_PITCHG; break;
    case CMD_VOICE:
        t->voice = rom_u8(t->cmd++);
        if (!read_tone(t->tone, s_tone + 12u * t->voice)) track_stop(n);
        break;
    case CMD_VOL: t->vol = rom_u8(t->cmd++); t->flags |= MPT_FLG_VOLCHG; break;
    case CMD_PAN: t->pan = rom_u8(t->cmd++) - C_V; t->flags |= MPT_FLG_VOLCHG; break;
    case CMD_BEND: t->bend = rom_u8(t->cmd++) - C_V; t->flags |= MPT_FLG_PITCHG; break;
    case CMD_BENDR: t->bendRange = rom_u8(t->cmd++); t->flags |= MPT_FLG_PITCHG; break;
    case CMD_LFOS: t->lfoSpeed = rom_u8(t->cmd++); if (!t->lfoSpeed) clear_mod(t); break;
    case CMD_LFODL: t->lfoDelay = rom_u8(t->cmd++); break;
    case CMD_MOD: t->mod = rom_u8(t->cmd++); if (!t->mod) clear_mod(t); break;
    case CMD_MODT: {
        unsigned v = rom_u8(t->cmd++);
        if (v != t->modT) { t->modT = v; t->flags |= MPT_FLG_VOLCHG | MPT_FLG_PITCHG; }
        break;
    }
    case CMD_TUNE: t->tune = rom_u8(t->cmd++) - C_V; t->flags |= MPT_FLG_PITCHG; break;
    case CMD_PORT: t->cmd += 2; break;
    case CMD_XCMD: xcmd(n, from); break;
    case CMD_EOT: end_tie(n); break;
    default: track_stop(n); break;
    }
}

// MPlayMain LFO triangle (m4a_1.s).
static void lfo_step(Track *t)
{
    if (!t->lfoSpeed || !t->mod) return;
    if (t->lfoDelayC) { --t->lfoDelayC; return; }
    // MPlayMain stores the wrapped phase, but retains the full sum for its triangle.
    unsigned phase = t->lfoSpeedC + t->lfoSpeed;
    t->lfoSpeedC = phase;
    int wave = (uint8_t)(phase - 64) < 128 ? 128 - (int)phase : (int8_t)phase;
    int8_t mod = (wave * t->mod) >> 6;
    if (mod != t->modM) {
        t->modM = mod;
        t->flags |= t->modT ? MPT_FLG_VOLCHG : MPT_FLG_PITCHG;
    }
}

// MPlayMain per-tick body only. The caller supplies tempo/external clock scheduling.
void player_tick(void)
{
    if (!g_plHeader) return;
    int leader = -1;
    for (int n = 0; n < PL_TRACKS; ++n) if (g_tracks[n].flags & MPT_FLG_EXIST) { leader = n; break; }
    if (leader < 0) {
        if (!g_plRepeat) { player_stop(); return; }
        if (!player_start(s_entry)) { player_stop(); return; }
        ++g_plLoops; leader = 0;
    }
    if (g_plTicks % 24 == 0) ++g_plBeats;
    for (int i = 0; i < PL_DS_MAX + PL_CGB; ++i) {
        Chan *c = i < PL_DS_MAX ? &g_ds[i] : &g_cgb[i - PL_DS_MAX];
        if ((c->status & SF_ON) && c->track < PL_TRACKS && c->gateTime && --c->gateTime == 0)
            c->status |= SF_STOP;
    }
    for (int n = 0; n < PL_TRACKS; ++n) {
        Track *t = &g_tracks[n];
        if (!(t->flags & MPT_FLG_EXIST)) continue;
        if (t->gate && t->gateLeft && --t->gateLeft == 0) t->gate = 0;
        if (t->flags & MPT_FLG_START) {
            uint32_t cmd = t->cmd, stack[3];
            for (int j = 0; j < 3; ++j) stack[j] = t->stack[j];
            clear_bytes(t, sizeof(*t));
            t->cmd = cmd;
            for (int j = 0; j < 3; ++j) t->stack[j] = stack[j];
            t->flags = MPT_FLG_EXIST; t->bendRange = 2; t->volX = 64;
            t->lfoSpeed = 22; t->tone[0] = 1;
            t->keyShiftX = g_mods.pitch >> 8; t->pitX = g_mods.pitch & 255;
            t->flags |= MPT_FLG_PITCHG;
        }
        unsigned budget = 64;
        while (!t->wait && (t->flags & MPT_FLG_EXIST)) {
            if (!budget-- || !rom_ok(t->cmd, 8)) { track_stop(n); break; }
            uint32_t from = t->cmd;
            unsigned cmd = rom_u8(t->cmd);
            if (cmd < 128) {
                cmd = t->runningStatus;
                if (cmd < CMD_VOICE) { track_stop(n); break; }
            } else {
                ++t->cmd;
                if (cmd >= CMD_VOICE) t->runningStatus = cmd;
            }
            command(n, cmd, from, n == leader);
        }
        if (!(t->flags & MPT_FLG_EXIST)) continue;
        if (t->wait) --t->wait;
        lfo_step(t);
    }
    ++g_plTicks;
    player_update();
    cgb_kick();
}
