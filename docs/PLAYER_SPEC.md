# Brief: the MP2K player for GBA Cart Player

You are writing **three files** for a GBA program that plays the music in an inserted Game Boy
Advance cartridge. The game's songs are in Nintendo's MP2K ("Sappy"/m4a) format. Everything around
your code already exists, or is being written against the interface below. Your files must drop into
the repo and compile with **no edits to any other file**.

## 1. What to deliver

| File | Status | What it is |
|---|---|---|
| `payload/player.c` | **new, yours** | Implements everything declared in `payload/player.h` |
| `payload/cgb.h` | **new, yours** | Exactly the interface in §6, nothing more and nothing less |
| `payload/cgb.c` | **new, yours** | The PSG ("CGB") voices: a port of m4a's `CgbSound` |

**Do not modify any other file.** These headers in particular are frozen: `player.h`, `mixer.h`,
`rom.h`, `m4a.h`, `scan.h`, `audio.h`, `gba.h`. If you believe one of them is wrong or missing
something, stop and ask. Don't work around it.

Read these first, in this order. They are short:
1. `payload/player.h` is the contract you implement. Every name, type and field in it is final.
2. `payload/mixer.h` is the sample renderer you drive. It is written; don't touch its internals.
3. `payload/rom.h` gives bounds-checked cartridge reads. You must use them (§4).
4. `payload/m4a.h` holds the data-format offsets, command bytes and table declarations.
5. `payload/m4a_tables.c` holds the tables (`gClockTable`, `gScaleTable`, `gFreqTable`, CGB tables).
6. `payload/scan.h`, for `scan_header_ok()`.

## 2. The reference you are porting

The behaviour is m4a's, as decompiled by pret. Use these files from
`https://github.com/pret/pokeemerald`, `master` branch:

- `src/m4a_1.s`: `MPlayMain`, `ply_note`, `ply_endtie`, `TrackStop`, `ChnVolSetAsm`, `ply_fine`,
  `ply_goto`, `ply_patt`, `ply_pend`, `ply_rept`, `ply_prio`, `ply_tempo`, `ply_keysh`,
  `ply_voice`, `ply_vol`, `ply_pan`, `ply_bend`, `ply_bendr`, `ply_lfos`, `ply_lfodl`, `ply_mod`,
  `ply_modt`, `ply_tune`, `ply_port`, `clear_modM`. Also `SoundMainRAM`, of which you need **only**
  the per-channel envelope section (from label `SoundMainRAM_ChanLoop` down to where the channel's
  `envelopeVolumeLeft` is stored). The PCM mixing loop after that is already done, in `mixer.c`.
- `src/m4a.c`: `MidiKeyToFreq`, `MPlayStart`, `TrkVolPitSet`, `ply_memacc`, `ply_xcmd` and every
  `ply_x*` sub-handler, `MidiKeyToCgbFreq`, `CgbOscOff`, `CgbModVol`, `CgbSound`,
  `m4aMPlayPitchControl`, `MPlayExtender` (register init only).
- `src/m4a_tables.c`: the jump-table layout (`gMPlayJumpTableTemplate`) tells you which command
  bytes are `ply_fine`. The tables are already transcribed into `payload/m4a_tables.c`. Use those
  and don't redefine them.

Where this brief and the pret source disagree, **this brief wins**. Every deliberate difference is
listed in §5.

## 3. Build environment (hard rules: break one and the file will not link)

The payload is freestanding C for the GBA's ARM7TDMI. It is compiled by `payload/build.sh` with:

```
arm-none-eabi-gcc -mcpu=arm7tdmi -mthumb-interwork -marm -O2 -ffreestanding -fno-builtin -Wall -Wextra -I..
```

It is linked with `-nostdlib`, so **libc and libgcc are absent**. That means:

1. **No division or modulo by a variable**, 32- or 64-bit. It becomes `__aeabi_uidiv`/`__aeabi_idiv`
   and fails at link time. That is deliberate in this repo, a tripwire. Divides and modulos by
   **constants** are fine, as are shifts and masks by powers of two. 64-bit **multiplies**
   (`(uint64_t)a * b`) and 64-bit shifts by a constant are fine.
2. **No `memcpy`, `memset`, `strlen` or any libc call, and nothing that makes GCC emit one
   implicitly.** That includes struct assignment (`a = b;` for structs), struct-valued returns,
   and zero-initialising large local arrays or structs. Copy and clear with explicit loops.
3. **No floating point.**
4. **No inline assembly, no GBA-specific intrinsics.** Hardware registers are reached **only**
   through the `R8()`, `R16()`, `R32()` macros in `gba.h`. That is what lets the same files compile
   for the PC test harness (rule 5).
5. **The files must also compile as host C** with `-DHOST` (`cc -std=gnu11 -DHOST -O2 -Wall -Wextra
   -c player.c`). Under `HOST`, `ROM_PTR()` maps cart addresses into a buffer and `R16()` etc. hit a
   plain array. Use `ROM_PTR`/`rom_u8`/`rom_u16`/`rom_u32` for every cart access and never cast a
   `0x08xxxxxx` number to a pointer yourself.
6. **Zero warnings** under the flags above, on both targets.
7. No `IWRAM_CODE` is needed. Everything you write runs from the main loop, not from an interrupt.

Compile check before you hand over (Windows, Pico SDK toolchain; adjust the path if yours differs):

```sh
cd payload
CC=~/.pico-sdk/toolchain/14_2_Rel1/bin/arm-none-eabi-gcc
NM=~/.pico-sdk/toolchain/14_2_Rel1/bin/arm-none-eabi-nm
F="-mcpu=arm7tdmi -mthumb-interwork -marm -O2 -ffreestanding -fno-builtin -Wall -Wextra -Werror -I.."
$CC $F -c player.c -o /tmp/player.o
$CC $F -c cgb.c    -o /tmp/cgb.o
$NM -u /tmp/player.o /tmp/cgb.o
```

`nm -u` (undefined symbols) **must list only names from this set**:
`g_romSize g_romEnd g_mix g_mixReverb mixer_start mixer_stop scan_header_ok gScaleTable
gFreqTable gCgbScaleTable gCgbFreqTable gNoiseTable gCgb3Vol gClockTable`, plus the names
`player.c` and `cgb.c` define for each other. If you see `__aeabi_*`, `memcpy`, `memset` or
anything else, fix it.

## 4. Safety rules (the program's whole point depends on these)

The user's CV deliberately makes the engine read voice tables and samples **from the wrong
addresses**. Garbage data must only ever cause wrong or absent sound. It must never cause a crash,
a hang, or a read outside the cartridge.

1. **Every cart address you follow is checked with `rom_ok(addr, len)` first**, for the whole
   length you will read. This covers song headers, track data, voicegroup entries, key-split tables,
   sub-voicegroups, WaveData headers, sample data and CGB wave tables.
2. **Before executing each track command**, check `rom_ok(t->cmd, 8)`. No command reads more than
   8 bytes. If the check fails, stop the track exactly as `ply_fine` would.
3. **At most 64 commands per track per tick** without reaching a wait. On the 65th, stop the track
   as `ply_fine` would. This catches `GOTO`-to-self loops and garbage.
4. **Running status:** if a byte below `0x80` arrives and the track's `runningStatus` is below
   `0xBD`, stop the track. The original indexes `gClockTable` out of bounds here.
5. Pattern call depth is 3, as in the original (`ply_patt` past depth 3 behaves as `ply_fine`).
6. Command bytes that are `ply_fine` in m4a's jump table (`0xB6`–`0xB8`, `0xC6`, `0xC7`,
   `0xC9`–`0xCB`) stop the track. So do `XCMD` sub-commands 0, 3, and anything above `0x0D`.
7. **`PORT` (`0xCC`) consumes its 2 argument bytes and writes nothing.** Song data must never write
   hardware registers.
8. `MEMACC` uses a private **256-byte** scratch area. Its index is a byte, so it can never escape.
9. Read multi-byte values from the cart with `rom_u16`/`rom_u32`, which are byte-wise and
   alignment-safe, never by casting.

## 5. Departures from m4a (implement these exactly)

### 5.1 Clocking: `player_tick()` is exactly one tick

m4a accumulates `tempoC += tempoI` per video frame and runs one tick per 150. **Do not do this.**
`player_tick()` runs **exactly one** iteration of `MPlayMain`'s per-tick body:
1. the gate-time countdown on each channel;
2. track `START` initialisation;
3. the command loop while `wait == 0`;
4. `wait--`;
5. the LFO step.

The caller (`clock.c`, not yours) decides when ticks happen. `player_tempo_i()` returns
`(g_plTempoD * g_mods.tempoU) >> 8`, which is m4a's `tempoI`. `TEMPO` sets `g_plTempoD = arg * 2`.

At the **start** of each tick, if `g_plTicks % 24 == 0`, increment `g_plBeats` (so tick 0 is a
beat). Increment `g_plTicks` at the end of the tick.

### 5.2 Notes start at their tick, not at the next frame

In m4a, `ply_note` sets `SOUND_CHANNEL_SF_START` and the start is processed up to a frame later.
Here:
- **DirectSound:** in note-on, after a channel is obtained and its fields set, do immediately what
  `SoundMainRAM` does for a `START` channel: status becomes `SF_ATTACK` (plus `SF_LOOP` if the sample
  loops), envelope volume starts at 0, and the first attack step is applied (`env += attack`, capped
  at 255 with a move to decay). Then call `mixer_start()` (§7). The channel never sits in `SF_START`.
- **CGB:** note-on sets `SF_START` as m4a does. At the **end of `player_tick()`**, call
  `cgb_kick()` (§6) once.

### 5.3 Envelopes and the frame

`player_frame()` is called by the caller at about 59.73 Hz. It does three things:
- runs the DirectSound per-channel envelope logic from `SoundMainRAM` (attack/decay/sustain,
  release on `SF_STOP`, pseudo-echo on `SF_IEC`, channel off at the end), for every DirectSound
  channel with `status & SF_ON`;
- calls `cgb_frame()` once;
- decays each track's `act`: `act -= (act >> 3) + 1`, floored at 0.

Before running the envelope on DirectSound channel `i`, check `mixer_done(i)`. If it's true, the
one-shot sample has ended: turn the channel off.

A DirectSound channel is **off** when `status = 0`, `track = PL_NONE` and `mixer_stop(i)` has been
called.

### 5.4 Volume and pitch propagation: `player_update()`

`player_update()` is called very often (every main-loop iteration, including while the clock is
stopped). It must be cheap when nothing changed. In order:
1. **Global pitch.** If `g_mods.pitch` differs from the value last applied, apply it to every
   existing track as `m4aMPlayPitchControl` does: `keyShiftX = pitch >> 8`, `pitX = pitch & 0xFF`,
   `flags |= MPT_FLG_PITCHG`. A track initialised by `START` also gets the current value.
2. **Mute.** If `g_mods.mute` differs from the value last applied: recompute every DirectSound
   channel's output volume (§7.3), and call `cgb_mute_changed()`.
3. **Channel cap.** Turn off any DirectSound channel with index `>= g_plMaxChans`.
4. **The tail of `MPlayMain`.** For each existing track with `flags & (MPT_FLG_VOLCHG |
   MPT_FLG_PITCHG)`: call `TrkVolPitSet`. Then for each channel belonging to that track (§5.5):
   - on `VOLCHG`: `ChnVolSetAsm`, plus §7.3 for DirectSound or `modify |= CGB_MO_VOL` for CGB;
   - on `PITCHG`: recompute the pitch. For DirectSound, write `g_mix[i].step` (§7.2). For CGB,
     `frequency = cgb_key_to_freq(...)` and `modify |= CGB_MO_PIT`.

   Finally `flags &= 0xF0`.

### 5.5 No linked lists

m4a chains channels to tracks with pointers. Here a channel's owner is `Chan.track` (the track
index 0–15, or `PL_NONE`). "The track's channels" means every entry of `g_ds[0..PL_DS_MAX-1]` and
`g_cgb[0..3]` whose `track` matches. "Clear chain" or "detach" means `track = PL_NONE`. Where m4a
compares track **pointers** (voice stealing), compare track **indices**, with `PL_NONE` greater
than any index.

### 5.6 Voice allocation

Port `ply_note`'s allocation exactly, with these changes:
- DirectSound scans only channels `0 .. g_plMaxChans-1`. Before scanning, treat any channel for
  which `mixer_done(i)` is true as free.
- CGB uses `g_cgb[(type & 7) - 1]`, with the original's priority/track comparison.
- If no channel is available, the note is dropped. The §5.8 follower fields are still updated.

### 5.7 The mangle hooks (`g_mods`; written by someone else, only read by you)

| Field | Exact behaviour |
|---|---|
| `voiceShift`, `tableOffset` | If **either** is non-zero, at **every note-on** re-read the tone from the cart at `s_tone + (int32_t)tableOffset + 12 * ((t->voice + voiceShift) & 0x7F)` (`rom_ok` 12 bytes, else drop the note), instead of using `t->tone`. When both are zero, use `t->tone` (set by `VOICE`, edited by `XCMD`). Then the normal key-split and rhythm resolution runs on the result, every pointer checked. `t->voice` is the raw `VOICE` argument. Store it in `VOICE`. |
| `sampleStart` | DirectSound start position = `t->startOff + ((size * sampleStart) >> 8)` samples. (`startOff` is set by `XCMD 0x0D`, m4a's `unk_3C`.) |
| `reverseChance` | Per DirectSound note: reversed = (`type & TONEDATA_TYPE_REV` ≠ 0) **XOR** (an 8-bit pseudo-random value `< reverseChance`). If reversed, pass `MIX_F_REVERSE`. Any cheap PRNG (xorshift32) is fine. |
| `romScrub` | If non-zero, add it to the sample data address. If `[data, data + size + 1)` is then outside the cart, wrap the offset: `data = ROM_BASE + ((data - ROM_BASE) & (g_romSize - 1))`. If it is still outside, use `data = g_romEnd - size - 1`. If `size + 1 > g_romSize`, drop the note. |
| `scrubLen` | Length of a raw-ROM voice (next row). |
| (raw-ROM fallback) | If a DirectSound tone's WaveData fails validation (§7.1) **and** (`tableOffset ≠ 0` or `romScrub ≠ 0`) **and** the tone's `wav` field `rom_isptr()`: play raw bytes instead. `data = wav`, `size = min(scrubLen, g_romEnd - wav - 1)`, loop from sample 0, `sFreq = 13379 << 10`. Otherwise the note is silent (dropped). |
| `mute` | Bit *n* silences track *n*. Muted tracks **keep playing**: notes start, envelopes run, positions advance. Only output is zeroed. DirectSound: zero volume in §7.3. CGB: mask the channel's bits out of `NR51` (§6). |
| `pitch` | §5.4.1. |
| `tempoU` | Used only in `player_tempo_i()`. |

**Added after delivery (2026-10-27).** The hooks below were added to the delivered player, by us,
not by the original developer. They are part of the contract now.

| Field | Exact behaviour |
|---|---|
| `attack`, `decay`, `sustain`, `release` | −2048..2047, 0 = as written. Read **live**, wherever an envelope reads its rate: `ds_envelope()` and every read in `cgb.c` go through `pl_env(c, PL_ENV_*)`, so a change reaches notes already sounding. `pl_env` moves the channel's value \|v\|/2048 of the way to one end of its range, rounded so full scale reaches it. DirectSound: attack toward 1 (slower) for v > 0, toward 255 (instant) for v < 0; decay, release 0..255 and sustain 0..255 toward 255 for v > 0, toward 0 for v < 0. PSG: attack, decay and release 0..7 and sustain 0..15, toward the top for v > 0. |
| `gate` | Note length at note-on, −2048..2047: ×(1 + 3v/2048) up to ×4, or ×(1 − 3\|v\|/8192) down to ×¼; clamped 1..255. A gate time of 0 (a `TIE`) is left alone. It applies to the channel's `gateTime` and to the follower's `gateLeft`. |
| `reverb` | −127..127, added to the song header's reverb (`s_songReverb`, 0..127). `g_mixReverb` = the clamped sum, set at `player_start()` and on every `player_update()`. The mixer writes its echo ring even when the reverb is 0, so turning it up live starts from what just played. |
| `vibrato`, `tremolo`, `lfoSpeed` | One global LFO, stepped in `player_frame()` (so its rate does not follow the clock): phase += `lfoSpeed`, a triangle −128..126. Vibrato adds `16 × ((wave × vibrato) >> 6)` to every track's pitch sum in `track_vol_pitch()` (m4a's MODT 0 scale). Tremolo scales the volume `x` by `(128 + ((wave × tremolo) >> 6)) / 128`, clamped to 255 (MODT 1). Depths are 0..63, and tracks are flagged only when an output changes. |

### 5.8 Follower fields (for the UI and the CV output)

At **every note command**, whether or not a channel was obtained:
- `notes++`;
- `lastKey = clamp(t->key + t->keyShift, 0, 127)`;
- `act = velocity`;
- `gate = 1`;
- `gateLeft = gateTime` (after any gate-time extension byte; 0 means a tie).

At the **start of each tick**, for each existing track: if `gate && gateLeft && --gateLeft == 0`,
then `gate = 0`.

`EOT` and track stop (`FINE` or a safety stop) set `gate = 0`.

### 5.9 Songs, end and repeat

`player_start(songEntry)`:
- `songEntry` is the address of a song-table entry. Read its header (`u32` at the entry). Reject it
  unless `scan_header_ok(hdr, &tc)` is true and `tc >= 1`, returning 0.
- Then do what `MPlayStart` does:
  - stop every channel, DirectSound and CGB (`cgb_off`);
  - clear all 16 tracks;
  - give tracks `0..tc-1` `flags = MPT_FLG_EXIST | MPT_FLG_START` and `cmd` = their part pointer;
  - remember the voicegroup (`SH_TONE`) and priority (`SH_PRIORITY`);
  - set `g_plTempoD = 150` and `g_plTicks = 0`.
- Set `g_mixReverb = (reverb & 0x80) ? (reverb & 0x7F) : 0` from `SH_REVERB`.
- `g_plHeader = hdr`, `g_plTrackCount = tc`. Return 1.
- Do **not** reset `g_plBeats` or `g_plLoops`. They are wrapping counters the host works in deltas
  on.

Track `START` initialisation (inside the tick) follows `MPlayMain`: zero everything except `cmd` and
`stack[]`. Then set:
- `flags = MPT_FLG_EXIST`;
- `bendRange = 2`;
- `volX = 64`;
- `lfoSpeed = 22`;
- `tone[0] = 1`;
- the current global pitch (§5.4.1).

**Loops.** When the lowest-numbered existing track executes a `GOTO`, or a `REPT` with count 0,
whose target address is ≤ the address of that command byte, increment `g_plLoops`.

**Song end.** If, at the start of a tick, no track has `MPT_FLG_EXIST`:
- if `g_plRepeat` is set, call `player_start()` on the same entry and increment `g_plLoops`;
- otherwise call `player_stop()`.

`player_stop()`: every channel off, every track's `flags = 0`, `g_plHeader = 0`.
`player_running()` returns `g_plHeader != 0`.

### 5.10 Not supported (on purpose)

- `TONEDATA_TYPE_CMP` (compressed) tones are silent.
- WaveData whose `u16` type field is non-zero is silent, unless the raw-ROM fallback applies.
- m4a's music-player priorities between players, fades, Pokémon-cry functions, and the
  `MPlayMain` callback (`o_MusicPlayerInfo_func`) are not ported.
- Mono mode (`soundInfo->mode & 1`) is always off.

## 6. `cgb.h`: the exact interface

```c
// cgb.h — the four PSG ("CGB") voices m4a songs use, driven like m4a's CgbSound.
#pragma once
#include <stdint.h>

void     cgb_init(void);         // register init (MPlayExtender's) + channel init
void     cgb_frame(void);        // CgbSound: once per player_frame()
void     cgb_kick(void);         // process channels with SF_START now (see below)
void     cgb_off(int ch);        // ch 0..3: CgbOscOff(ch + 1), status = 0, track = PL_NONE
void     cgb_mute_changed(void); // g_mods.mute changed: rewrite NR51 at the next cgb_frame
uint32_t cgb_key_to_freq(int chanNum, int key, int fine);   // MidiKeyToCgbFreq; chanNum 1..4
```

- The channel state is `g_cgb[4]`, declared in `player.h` and defined in `player.c`. `cgb.c` uses
  it via `#include "player.h"`. The `Chan` struct has every `CgbChannel` field `CgbSound` needs,
  under these names: `status`, `type`, `rightVolume`, `leftVolume`, `attack`, `decay`, `sustain`,
  `release`, `key`, `envVol` (envelopeVolume), `envGoal`, `envCounter`, `echoVol`, `echoLen`,
  `gateTime`, `midiKey`, `velocity`, `priority`, `rhythmPan`, `track`, `sustainGoal`, `n4`, `pan`,
  `panMask`, `modify`, `length`, `sweep`, `frequency`, `wav` (wavePointer), `curWave`
  (currentPointer).
- `cgb_init()`:
  - writes what `MPlayExtender` writes to `NR12`, `NR22`, `NR42`, `NR14`, `NR24`, `NR44`, `NR30`
    and `NR50`, and sets `NR51 = 0`;
  - sets `g_cgb[i].type = i + 1`, `panMask` = `0x11`/`0x22`/`0x44`/`0x88`, `status = 0`,
    `track = PL_NONE`.

  It must **not** write `SOUNDCNT_X` (0x84), `SOUNDCNT_H` (0x82), `SOUNDBIAS` (0x88), DMA or timer
  registers. `audio_init()` owns those and has already set the master enable.
- `cgb_frame()` is a port of `CgbSound` with:
  - `soundInfo->c15` kept as a static in `cgb.c`;
  - `soundInfo->mode & 1` treated as 0;
  - `REG_SOUNDBIAS_H` read as `R8(0x89)`.
- `cgb_kick()` runs `CgbSound`'s per-channel body only for channels with `status & SF_START`,
  **without** advancing `c15`, and with `prevC15` treated as non-zero (no double step).
- **Mute.** Wherever `CgbSound` writes `NR51` for a channel, first clear that channel's `panMask`
  bits from the value written if `track < 16 && (g_mods.mute >> track) & 1`. `cgb_mute_changed()`
  sets `modify |= CGB_MO_VOL` on every channel with `status & SF_ON`, so the next `cgb_frame()`
  rewrites `NR51`.
- **The wave channel.** `wav` is a cart address of a 16-byte, 4-bit wave table. `player.c` must
  `rom_ok(wav, 16)` it at note-on and drop the note if that fails. `cgb.c` loads wave RAM with
  `rom_u32()` (four reads), not by dereferencing.
- Registers, as `R8()`/`R16()`/`R32()` offsets from `0x04000000`:
  - `NR10` 0x60, `NR11` 0x62, `NR12` 0x63, `NR13` 0x64, `NR14` 0x65;
  - `NR21` 0x68, `NR22` 0x69, `NR23` 0x6C, `NR24` 0x6D;
  - `NR30` 0x70, `NR31` 0x72, `NR32` 0x73, `NR33` 0x74, `NR34` 0x75;
  - `NR41` 0x78, `NR42` 0x79, `NR43` 0x7C, `NR44` 0x7D;
  - `NR50` 0x80, `NR51` 0x81;
  - `WAVE_RAM0..3` 0x90/0x94/0x98/0x9C (32-bit).

  The "unused" `nrx0` slots `CgbSound` uses for channels 2 and 4 are 0x61 and 0x71.

## 7. Driving the mixer (`mixer.h`)

### 7.1 WaveData validation (DirectSound, at note-on)

A tone's sample is valid only if **all** of these hold:
- `rom_ok(wav, 16)`;
- `rom_u16(WAV_TYPE(wav)) == 0`;
- `!(tone type & TONEDATA_TYPE_CMP)`;
- `size = rom_u32(WAV_SIZE(wav))` is non-zero;
- after clamping `size` to `MIX_MAX_SAMPLES`, `rom_ok(WAV_DATA(wav), size + 1)` (one extra byte,
  because the mixer interpolates one sample ahead);
- `rom_u32(WAV_FREQ(wav)) != 0`, unless the tone type has `TONEDATA_TYPE_FIX`.

When valid:
- data = `WAV_DATA(wav)`;
- loop start = `rom_u32(WAV_LOOP(wav))` if `rom_u8(WAV_FLAGS(wav)) & WAVE_DATA_FLAG_LOOP`, else
  `MIX_NOLOOP`;
- `sFreq` = the freq field, or 0 for `TONEDATA_TYPE_FIX`.

Store the result in the channel's `sData`, `sSize`, `sLoop`, `sFreq` and `sFlags`.

### 7.2 Pitch → step

`step` is Q12 samples per output sample at 16384 Hz:
- **normal:** `step = ((uint64_t)sFreq * ratio) >> 34`, where `ratio` is `MidiKeyToFreq`'s
  interpolated table value for `key + keyM` (clamped ≥ 0) and `pitM`, using its key-178 clamp. That
  is `val1 + umul3232H32(val2 - val1, fine << 24)`. Do not multiply by `wav->freq` inside the ratio:
  the `>> 34` already accounts for it;
- **fixed rate** (`sFreq == 0`): `step = g_plGameRate >> 2`;
- clamp to `1 .. 0x200000`.

### 7.3 Output volume

`g_mix[i].volR = (rightVolume * env) >> 8` and `g_mix[i].volL = (leftVolume * env) >> 8`, where
`env` is the channel's envelope volume, or 0 if its track is muted. Write these after every
envelope step, every `VOLCHG`, and every mute change.

### 7.4 Rules

- **Start a sample only with `mixer_start(i, (const int8_t *)ROM_PTR(data), size, loop, startAt,
  step, volL, volR, flags)`.** (The cast is needed: `ROM_PTR` yields `const uint8_t *`.) Never write `g_mix[i].data`, `startQ`, `endQ`, `loopQ`, `gen` or `active` yourself.
  `mixer_start` makes the change atomic with respect to the audio interrupt. It also clamps `size`
  and `startAt`, and ignores loops at or past the end.
- You may write `g_mix[i].step`, `g_mix[i].volL` and `g_mix[i].volR` directly at any time.
- Stop a sample with `mixer_stop(i)`. Detect a finished one-shot with `mixer_done(i)`.
- DirectSound channel `i` of `g_ds[]` **is** mixer channel `i`.

## 8. Conventions (so it reads like the rest of the repo)

- Every file opens with a comment saying what it is and why it is shaped the way it is.
- Comments explain non-obvious decisions, not the obvious code.
- Above each ported function, name the m4a original (e.g. `// TrkVolPitSet (m4a.c).`) so a reader
  can check it side by side.
- Naming: `snake_case` functions. Globals are `g_` and defined exactly as declared in `player.h`.
  File-scope statics are `s_`.
- Define every global declared in `player.h`, with these initial values:
  - `g_mods = { 0, 0, 0, 0, 0, 4096, 0, 0, 0x100 }` (in field order: `voiceShift`, `tableOffset`,
    `sampleStart`, `reverseChance`, `romScrub`, `scrubLen`, `mute`, `pitch`, `tempoU`);
  - `g_plTempoD = 150`, `g_plMaxChans = 8`, `g_plGameRate = 13379`, `g_plRepeat = 1`;
  - everything else 0.
- `player_init()` turns every channel off, clears the tracks, and calls `cgb_init()`. It is called
  once, **after** `audio_init()`.

## 9. Who calls what (for context; you don't write these)

- `main.c`: `audio_init()`, then `player_init()`.
- `clock.c`: `player_tick()` zero or more times per main-loop iteration (from the song tempo via
  `player_tempo_i()`, or from an external clock), `player_frame()` at ~59.73 Hz, and
  `player_update()` on every iteration.
- `mod.c`: writes `g_mods.*`, `g_plMaxChans`, `g_plGameRate` and `g_plRepeat`. Calls
  `player_start()` with `scan_song_entry(n)`. Reads `g_tracks[].lastKey`, `.gate`, `.notes` and
  `.act`, plus `g_plBeats`, `g_plLoops` and `g_plTicks`.

## 10. Acceptance

1. The §3 compile check passes for both files, with `-Werror`. `nm -u` lists only the allowed
   symbols. The host compile (`-DHOST`) passes with no warnings.
2. No edits to any file other than the three delivered.
3. Walk through these against the pret source:
   - The command loop handles every byte `0x80`–`0xFF` (waits, the jump table, notes `0xCF`–`0xFF`
     with optional key, velocity and gate-extension bytes).
   - Running status is stored only for commands ≥ `0xBD`.
   - `REPT`, `PATT`/`PEND`, `MEMACC`'s conditional jumps, `XCMD 0x0C`'s re-execution, `EOT`'s
     key match.
   - LFO maths, `TrkVolPitSet`, `ChnVolSetAsm`, the DirectSound envelope, and `CgbSound`'s
     envelope.
4. With random bytes as track data and random `g_mods` values, the engine never reads outside the
   cart and never spends more than 64 commands per track per tick. If you can, test this on the PC
   under `-DHOST` with a fuzz loop.
5. A short note on anything you were unsure of, and what you chose.
