# Krawall and Engine Software: one tracker player for both?

An investigation (2026-11-06) into supporting Engine Software's GBA replayer alongside Krawall, ideally
through one shared tracker playback core. Sources studied:

- **Krawall**, Sebastian Kienzl's original source (github.com/sebknzl/krawall, LGPL-2.1): `lib/`
  (player, effects, mixer), `krawerter/` (the XM/S3M converter), and its three manuals.
- **UnkrawerterGBA** (MCJack123, MIT): Krawall data as found in retail carts, versions 2003-09-01 to
  2005-04-21.
- **engine-software-gba-tools** (lunasorcery): the Engine Software bank format, from static reverse
  engineering. Its author asks that exports not be called accurate until its assumptions are verified.

## Summary

- **Engine Software's songs are XM data in a different container.** Cells are XM's five bytes (note,
  instrument, volume column, effect, parameter), and effect numbers are XM's: gba2xm copies them into an
  XM unchanged. Instruments are XM instruments with one sample each, and their envelopes are XM's
  12-point arrays.
- **Krawall's songs are XM/S3M converted by krawerter**: its own effect numbering (1–50), its own
  run-length-coded patterns, and envelopes precomputed into nodes. Its player is a full XM/S3M player.
- So **both formats feed the same musical model, XM semantics, and a shared player is the right design.**
  Each format gets a front end (its pattern decoder and instrument/sample readers); they share one core
  for rows, effects, envelopes, periods and voices, built on our mixer.
- **Running Krawall's original engine is not the way, for Engine Software or for Krawall itself.** Its
  sample structure needs the header in front of the data, so Engine Software samples would have to be
  copied into RAM, which multiboot cannot hold. Its mixer also takes the same timers and DMA channels as
  our audio, with its own buffers. What should be taken from it is the *player logic*: `player.c` and
  `player_effects.h`, about 2,100 lines.
- **This would improve Krawall now.** Our current Krawall engine (from the trial branch) leaves out:
  - portamento (up, down and to-note);
  - vibrato, tremolo, arpeggio, tremor, retrigger;
  - pattern loop, note delay, pattern delay;
  - every volume-column effect.
  
  It also builds pitch from 8363 Hz semitone ratios, not Krawall's linear/Amiga period tables, and
  computes envelope levels its own way. **Andy reported odd levels on some Lemony Snicket tunes
  (2026-11-06)**; the envelope and volume code is the first suspect.
- **Engine Software playback is unverified until we have a cart.** None of the 19 dumps on hand holds a
  bank: `gbafind`'s rules found none. Comment-sourced lists (loveemu's GBA sound driver list) name
  Premier Manager 2003-04, Rocky, The Muppets: On with the Show, Terminator 3, Star Wars: Flight of the
  Falcon, CT Special Forces 3, Spy Muppets, Wade Hixton's Counter Punch, James Pond: Codename RoboCod
  and others. **Andy's Premier Manager cart (no MP2K found) is the likeliest Engine Software cart on
  hand.** As with MusyX, the replayer's own code would then settle the effect and frequency details.

## 1. The two formats side by side

| | Engine Software (bank v0x0121) | Krawall (from `mtypes.h` and krawerter) |
|---|---|---|
| Container | One bank: header `{u16 0x0121, u8 instruments, u8 songs}`, song offsets, instruments, row data, songs. All offsets relative to the bank. | Separate ROM symbols: a global sample list, a global instrument list, and one Module per song. Bare pointers (the cart's address space). |
| Song header | channels, length, loop point, pattern count, speed, tempo | channels, numOrders, songRestart, `order[256]`, `channelPan[32]`, `songIndex[64]` (sub-songs, split by 254 markers), global volume, initial speed and BPM, flags (instrument-based, linear slides, S3M volume slides, volume optimisation, Amiga limits) |
| Order list | `u8 order[length]`, entries below the pattern count | `u8 order[256]`; 254 marks a sub-song, 255 the end |
| Pattern | u16 rows, then **one u32 offset per row** (0 = empty row) | `u16 index[16]` (byte offset of every 4th row), u16 rows (u8 before 2004), then RLE data |
| Row encoding | A bitmask of channels × 5 bits (MSB first), then one byte per set bit, in the order note, instrument, volume, effect, parameter | Per used channel, a "follow" byte (channel \| 32 note+instrument \| 64 volume \| 128 effect), then the bytes; 0 ends the row. Instrument numbers above 255 take a third byte (note bit 7). |
| Note | XM: 1–96, key-off presumably 97 (unverified) | 1–96 (+ the sample's relative note at play); key-off is 127 |
| Instrument column | 1-based instrument | 1-based instrument (instrument-based songs) or sample (S3M-style) |
| Volume column | XM's (0x10–0x50 volume, 0x60–0xFF commands) | XM's encoding, all ten command groups implemented |
| Effects | XM numbers 0–35 with E/X subcommands (raw) | Krawall's own numbers 1–50 (`effects.h`), converted from XM/S3M by krawerter |
| Instrument | A 124-byte header **directly followed by its one sample**: sample length/loop start/loop length, volume, panning (0–255), finetune, relative note, fadeout, 2 unknown bytes, then volume and pan envelopes | `samples[96]` (a sample per note: multisampling), volume and pan envelopes, fadeout, auto-vibrato (type, sweep, depth, rate) |
| Envelope | XM's raw points: `{u8 count, sustain?, loop start?, loop end?, {u16 x, u16 y}[12]}`. Which byte is which is a guess, with 0xFF = off. | Precomputed nodes `{u16 coord = y<<9 \| x, s16 slope = Δy·256/Δx}[12]`, then `{max, sustain, loop start, flags}` |
| Sample | **Signed** 8-bit, inside the instrument | Header `{loop length, end pointer, c2Freq, finetune, relative note, volume, pan (−64..63), loop flag, hq flag}` followed by its data. Data is **unsigned** (+128), loops shorter than a minimum are unrolled, and it is padded past the end. |
| Auto-vibrato | No field: the 2 unknown bytes are probably padding (unverified) | type/sweep/depth/rate. The player uses depth and rate; sweep and type are not implemented. |
| Frequency table | Not stored: gba2xm assumes Amiga (unverified) | A module flag: linear or Amiga periods, from tables |
| Default channel pan | None (XM has none) | Per channel, for S3M-derived songs |

**Verified similarities:** the cell model; XM note numbering; the volume-column encoding; one envelope
model (XM's 12-point envelopes with sustain and loop, then fadeout); the XM sample parameters (volume,
finetune, relative note, loop); and speed/BPM with the tracker tick = 2.5 ÷ BPM s.

**Incompatibilities:**
- Effect numbering (fixed by krawerter's `eff_conv` tables, which are mechanical).
- Pattern packing.
- Envelope storage (raw points against slopes; also mechanical: krawerter's `recalcEnvelope`).
- Signed against unsigned samples (our mixer already plays both).
- Inline sample headers.
- Engine Software has no sample map (always one sample), no frequency-table flag, no global volume, and
  no auto-vibrato.
- Engine Software's envelope control bytes are unconfirmed.

## 2. Could Engine Software songs become Krawall modules?

- **Offline, broadly yes.** gba2xm already turns a bank into XM files, and krawerter turns XM into
  Krawall data. The chain loses only what the guesses lose: the frequency table, the envelope control
  bytes, and anything Engine Software's replayer does differently from XM. krawerter also drops a few XM
  effects (below).
- **On the card at run time, no.**
  - Krawall's `Sample` must be a header followed by its data, so every sample would need copying. A
    bank's samples run to hundreds of KB; multiboot leaves about 120 KB of EWRAM after our 129 KB
    payload.
  - Patterns would have to be re-encoded into RAM.
  - Krawall's original engine needs Timers 0/1 and DMA 1/2 with its own mixer and buffers, and our audio
    uses Timer 0 and DMA 1/2 with one mixer for every engine (and the mangles live in that mixer).
- So **translate at the decoder, not the data**: an Engine Software front end turns each row into
  Krawall-numbered effects as it reads (through `eff_conv`), and reads envelopes and samples where they
  lie.

## 3. Effect semantics (XM → Krawall, from `ModXM.cpp` and `player.c`)

Implemented by Krawall's player, and so by a core built from it:
- `0` arpeggio.
- `1`/`2` portamento up/down. In Amiga mode they are swapped at conversion, because periods run the
  other way.
- `3` tone portamento, `4` vibrato, `5`/`6` slides combined with portamento/vibrato, `7` tremolo.
- `8` pan (0–255 → 0–128), `9` offset.
- `A` volume slide, `B` jump, `C` volume, `D` break (binary row).
- `F` speed/BPM.
- `G`/`H` global volume and its slide.
- `P` pan slide, `R` retrigger, `T` tremor.
- `X1`/`X2` extra-fine portamento, `X5` panbrello waveform.
- `E1`/`E2` fine portamento, `E3` glissando, `E4`/`E7` vibrato and tremolo waveforms, `E6` pattern
  loop, `E9` retrigger, `EA`/`EB` fine volume slides, `EC` note cut, `ED` note delay.
- Panbrello, the `MARK` callback, and every volume-column command.

Not implemented: `K` key-off (dropped by krawerter), `L` envelope position (converted, but the player's
table entry is empty), `E5` finetune, `E8`, `EE` pattern delay (converted, empty entry), `EF`,
`X6` fine pattern delay, `X9` sound control, and the offset high byte. An Engine Software front end
would map these to "unsupported" and count them, as the MusyX engine does.

**Engine Software's own semantics are unknown.** Its replayer may differ from XM: the frequency table,
porta memory, volume-column behaviour, envelope tick timing, interpolation and mixing rate. Only its
code can say; that would be the same job as the MusyX disassembly (docs/MUSYX_DRIVER.md).

## 4. Memory and hardware (multiboot)

- **Krawall's own figures** (Developer's Handbook, default 16 kHz "medium" build): ROM 32 KB, EWRAM
  8.6 KB, IWRAM 6.6 KB. It uses Timers 0/1, DMA 1/2 and the Direct Sound hardware, plus a Timer 1 IRQ
  every 4th or 8th frame to restart DMA. Mixing is `kramWorker()` once a frame into IWRAM buffers. Up to
  32 channels; under 18 % CPU peak for eight. Pan positions cost differently (hard left/right is
  cheapest), and "hq" samples are interpolated.
- **A shared core on our mixer needs none of the mixer half.**
  - Player state is `Player` with 20 `MChannel`s, about 2 KB, plus a 2 KB bar snapshot.
  - The code (`player.c` + effects + tables) is about 12–15 KB of Thumb in EWRAM.
  - Our current Krawall engine is about 10 KB, so the net cost is small.
  - The payload is 129 KB of the 256 KB EWRAM; IWRAM use (about 11 KB) does not change.
- **Timing:** Krawall advances its tick from the tempo (BPM × 2/5 Hz), and envelopes, fadeout and
  auto-vibrato step on that tick. Our clocked transport already gives Krawall one native tick per
  24-PPQN tick (6 ticks/row × 4 rows/beat), so envelopes follow the external clock, as XM's would.

## 5. Where Music Mangle's live modifications fit

The places below are in Krawall's `player.c`, which a shared core keeps:

| Mangle | Where |
|---|---|
| Transpose / fine pitch | The single period → frequency step (`calcFinalFreq`, and `calcFinalFreqFromNote` for arpeggio): add semitones × 64 to a linear period, or scale an Amiga frequency. Applied last, so slides and vibrato stay intact. |
| Instrument substitution (VOICE SHIFT, TABLE OFFSET) | Row decode, where `instrument` indexes `instruments[]` / `samples[]`, before the sample-map lookup. |
| Sample start / end / scrub / reverse | Note start (`playChannelNote`: `c->pos`, the sample pointer and length). The format's own `9xx` offset adds to it. |
| Envelopes (ATTACK, DECAY, SUSTAIN, RELEASE) | `processInstruments()`: scale the envelope's per-tick slope (attack/decay), clamp at the sustain node, scale `volFade` (release). Instruments without an envelope get a synthetic one. |
| Pattern jumping (RESTART LOOP / BAR, "jump to a random order") | `advanceRow()`'s `pGotoOrder` / `pGotoRow`, which `Bxx`/`Dxx` already use. A bar snapshot is the `Player` copy (Krawall already keeps `playerBackup` for jingles). |
| Tempo | Speed and BPM (`player.speed`, `setTempo`), or, in our clocked design, the transport's tick rate. |
| Probability (DROP, CHAOS) | Note start in `processRow()` (the `playnote` decision), plus chaos dice on the note, period and position. |
| Mute / solo | Channel volume (`cvolume`) or the voice level, after envelopes, so an unmute lands mid-note. |

## 6. Recommendation

1. **Build one tracker core from Krawall's player logic**, outputting through `eng_mixer` as the other
   engines do, and replace `engines/krawall/krawall.c` with it plus a Krawall front end (the RLE
   decoder, Module/Instrument/Sample readers for the 2003 and 2005 layouts, and our existing discovery).
   This fixes Krawall's missing effects and period handling, and is the place to check Lemony Snicket's
   levels.
   - **Licence:** Krawall is LGPL-2.1 and this project is MIT. A port keeps those files under the LGPL
     with their notices. That is compatible here because the source is published and the payload can
     be rebuilt from it. Re-implementing from the manuals and behaviour is the alternative. This is
     Andy's decision.
2. **Add an Engine Software front end** once there is a dump. It needs:
   - the bank finder (`gbafind`'s rules);
   - the bitmask row decoder;
   - `eff_conv` applied as rows are read;
   - envelopes read as raw points (one sample per instrument; samples signed, read in place).
   
   Then disassemble its replayer, as for MusyX, to settle the frequency table, envelope bytes and effect
   quirks before calling it faithful.
3. **What is still needed from Andy:** an Engine Software cart dump (Premier Manager 2003-04 first), and
   a Lemony Snicket song or two where the levels sound wrong, to compare against the core.
