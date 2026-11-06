# The GBA MusyX sound driver: a partial disassembly

Factor 5's MusyX for the Game Boy Advance, as its driver code in retail cartridges runs it. This was
worked out in November 2026 for GBA Music Mangle, by disassembling the driver in *Lady Sia* (Europe) and
confirming the layout in ten more carts. It is written for anyone building a player, ripper or
emulator for GBA MusyX music. GBA Music Mangle's implementation is `engines/musyx/musyx.c`; nothing
below depends on it.

**Confidence.** Everything here was read from the driver's own Thumb/ARM code, except where it says
*inferred* (from the data, or from Factor 5's 1999 N64/Game Boy manual). Field names are ours.
Behaviour was checked by rendering every song of eleven carts with zero unsupported commands, and the
data layouts were checked against the MIT-licensed BinarySerializer.Audio.GBA MusyX classes.

**Carts checked.** All carry the same 54-handler macro interpreter. Handler sizes match exactly in
Lady Sia, Go! Go! Beckham, BattleBots, Denki Blocks, Ace Lightning, Ready 2 Rumble 2 and Spider-Man. They
differ slightly in The Incredibles, Rayman: Hoodlums' Revenge and Star Wars: The New Droid Army. Harry
Potter and the Chamber of Secrets has a few larger handlers (0/1, 9, 15).

## 1. How to find the driver in a cart

- **The macro jump table** is 54 consecutive words. Each is an odd (Thumb) ROM address, and all of them
  lie within about 12 KB of each other. In Lady Sia it is at `0x083F4568`, with handlers at
  `0x0803229D`–`0x08033CBD`.
- **The dispatcher** is the only code that loads that table's address. In Lady Sia it is
  `0x08033568`. For each voice it calls `handler(voice, &state)` until a handler returns 0.
- **The per-frame update** is the dispatcher's caller (Lady Sia `0x0803142C`). The game's VBlank
  handler calls it once a frame, after re-enabling interrupts.
- **The driver's global state** is reached through a pointer in IWRAM (Lady Sia `0x03006ED8`).
  Useful offsets from it:

  | Offset | Holds |
  |---|---|
  | +0x08 | PCM voice count |
  | +0x0A | Mixing rate |
  | +0x14 / +0x16 | Music / effects master volume (0x100 = unity) |
  | +0x18 | Game Boy voice flags |
  | +0x94 | Game Boy envelopes |
  | +0xC8 | Mixer channels |
  | +0xCC | Mixer "playing" pointers |
  | +0xD0 | PCM voice records |
  | +0xD4 | Active-record pointers |
  | +0x118 | The bank root |
  | +0x11C | Macro states |

## 2. Data layout (the bank)

All offsets in the root are relative to the root. A *song group* is `songTable + 4 + 4n` → its
offset (relative to the root).

```
root +0   offset of the macro table          (u32 offsets, relative to the root; count = gap to first macro / 4)
root +4   offset of the table table          (u32 offsets of 8-byte ADSR tables; SETADSR indexes it)
root +24  offset of the song table           (u32 count, then u32 offsets of song groups)
root +28  offset of the sample table         (u32 offsets of sample headers)

sample header (16 bytes, then the data)
  +0  u32 size (bytes)       +4  s32 loop start (-1: none)
  +8  u16 rate (Hz)          +10 u8  root key (MIDI)     +11 u8 1 = a Game Boy wave-channel waveform
  +16 s8  data[size]         (signed 8-bit PCM; a waveform is 16 or 32 bytes of 4-bit pairs)

song group
  +0     u8  starting program for each of the 16 MIDI channels   (songs often send no program change)
  +16    sound list: 128 x { u16 macro, u8 priority, u8 (not read by the driver) }
  +528   drum list for MIDI channel 10 (track 9): 128 x 4 bytes, same layout
  +1040  the song

song (offsets relative to the song)
  +0  offset of the track table: 17 x u32 (16 note tracks, then the tempo track; 0 = absent)
  +4  offset of the pattern table: u32 offsets of patterns
  +8  u16 initial BPM (sequencer: 96 PPQN, advanced each frame by a fractional tick count)

track: 8-byte entries { s32 delta ticks, s32 pattern }  pattern -1 ends, -2 loops { s32 entry offset, s32 time }
pattern events: u16 delta, then
  velocity 0:          u8 0, u8 program                          (program change, 4 bytes)
  velocity > 0:        u8 velocity, u8 note [, u8 program if note bit 7], u16 duration   (6 or 8 bytes)
  end:                 0xFFFFFFFF
tempo track events: { u16 delta, u16 BPM }

macro: 4-byte header, then commands
  hdr +0  voice: 0-3 a Game Boy channel, 4+ a particular PCM voice, 255 any
  hdr +1  flags: bit 0 alternate the two squares; bit 7 "don't reset" (take the voice over as it is)
  hdr +2  1 = this is a KEYMAP, not code: u16 count at +4, then count x 8-byte entries at +6:
          { u16 sample, s16 transpose (1/256 semitone), s8 pan offset, u8 -, u8 unpitched, u8 - }
```

## 3. Timing: two clocks

- **The sequencer** runs at 96 PPQN, advanced each frame by tempo-dependent fractional ticks. It starts
  notes, and counts each note's duration down per voice. When the duration runs out it sends a key-off.
- **Everything else runs once per video frame (59.73 Hz):** the macros, ADSRs, fades, vibrato, pitch
  sweeps, slides and auto-pan. So **WAIT counts frames, and arpeggio and envelope speeds do not depend on
  tempo.** A player that runs macros on sequencer ticks plays them about three times too fast at 120 BPM.
- On a note-on, the sequencer allocates the voice and points its macro state at the code. The macro's
  first command runs at the next dispatcher pass, so every note in a tick has a voice before any of
  their macros runs. This matters: a one-shot macro (STARTSAMPLE, END) frees its voice at once (§5).

## 4. Voices

- **MIDI channels 0–3 always play on the Game Boy channels**: the macro header's voice when it is 0–3,
  otherwise the channel's own number. Header flag bit 0 alternates channels 0 and 1. A note there is
  refused when the channel is held by a higher-priority macro.
- **Channels 4–15 play on PCM voices.** A header voice of 0–3 counts as "any PCM voice" there.
- **A command meant for the other kind of voice does nothing.** STARTSAMPLE on a square does nothing.
  On the wave channel it loads a waveform, and only when the sample's +11 byte is 1. VOICE_ON acts only
  on the squares, SETNOISE only on the noise channel, and HARDENVELOPE on everything except the wave
  channel.
- **PCM allocation** scans from voice 0 for the first voice that is free *or whose macro has ended*.
  Failing that it takes the lowest priority (the sound list's byte), oldest first. The age is a counter
  starting at 0xC000 that each frame lowers by a per-voice speed. If even the lowest priority outranks
  the new note, the note is dropped.
- **At note-on** the level is full (0xFF00), the volume is velocity × master ÷ 256, the pan is centre
  (64), and the pitch is note × 256.

## 5. Envelopes, key-off and END

- **The level** (0..0xFF00) scales the volume. The final PCM volume is
  `(level>>8 (+1 if nonzero)) × (volume×2 (+1 if nonzero)) >> 8`.
- **SETADSR** (op 14) reads an 8-byte table `{attack, decay, sustain level, release}`. The rates are
  added each frame:
  - the level restarts at 0 and attack climbs to 0xFF00;
  - decay falls to the sustain level, which then holds;
  - on key-off the release falls to silence, then the envelope switches off.
  
  A key-off during attack or decay goes straight to the release. With SETADSR's flag set (hold), key-off
  does not release.
- **ENVELOPE** (op 21) is a linear fade. If no envelope is running, it first sets the level to full (fade
  out) or zero (fade in). The cart data pairs each fade with a WAIT of the matching length: 0xFF00 ÷ rate
  frames.
- **Key-off only marks the voice** (flag 0x100), and the mark stays. It also releases an ADSR (unless
  held), and sends the macro to its key-off trap (op 16) if one is set. *A WAIT with the key-off flag
  that runs after the key-off resumes at once.* So the common "fade; WAIT[key-off flag, n]; STOP"
  release is cut short by the next STOP: that is how the games sound.
- **END** (ops 0 and 1) stops the macro. The driver then lets go of a PCM voice: its record pointer
  is cleared, so its level, pan and pitch freeze and the sample plays on as it was. That voice is the
  first a new note takes. A Game Boy voice's hardware simply continues.

## 6. The mix

- **The PCM mixer** (ARM, around Lady Sia `0x08034F60`) is linear-interpolating. Per voice it adds
  `sample × (vol+1) >> 8` into 16-bit accumulators, then clips to signed 8 bits. Voice flag `+0x1A`
  inverts the right channel (surround, for a pan ≥ 128).
- **Pan** uses an equal-power law: 0.707 on each side at the centre (`0xB538`), and 1.0/0 at the edges.
  For pan p < 64, the near side is `0xB538 + (64−p)·0x4AC8/64` and the far side `p·0xB818/64`, both
  Q16. Pan 0 is taken to be left.
- **Init writes SOUNDCNT = `0x9A0D0000`**:
  - PCM channels A (left) and B (right) at 100%, Game Boy channels at **50%**;
  - NR50/NR51 = `0xFF77`.
- **Game Boy volume** is the level-scaled volume ÷ 8 as the NRx2 volume nibble. A nibble change
  retriggers the channel. The wave channel uses a 5-step volume table.
- **The mixing rate** is set at init from a value the game passes. Lady Sia and Beckham store 74 of
  their ~77 samples at 11397 Hz, so the mixing rate is very likely the same. An unpitched sample steps
  1:1 at that rate.
- **Pitch** is the sum of five parts, all in 1/256 semitone: `note×256` + the SETNOTE/ADDNOTE offset +
  sweep 1 + sweep 2 + vibrato + slide. The step is `rate × 2^((key−root)/12) ÷ mixRate` in Q12, from
  semitone tables interpolated by the fraction.

## 7. The 54 SMaL commands

The table gives each command's length in bytes, then its arguments after the opcode byte. "PSG" means
the Game Boy channels.

| Op | Len | Name (ours) | Arguments → what the driver does |
|---|---|---|---|
| 0, 1 | 1 | END | Stop the macro (§5). One handler serves both. |
| 2 | 2 | SETVOLUME | vol → the volume is vol × (music master + 1) ÷ 256. The velocity is set aside. |
| 3 | 2 | KEYON | flag → trigger the voice. On PSG this starts the note. On PCM the flag clears a per-voice "effects send" bit (used only when the game enables it). |
| 4 | 4 | WAIT | flags, u16 frames (65535 = for ever). Flag 1: resume once keyed off. Flag 4: resume when the sample has ended. Flag 2: a random time below the count. 0 frames: no wait. |
| 5 | 2 | STOP | flag → silence the voice (the macro carries on). With flag 1, first wait until an envelope has reached silence. |
| 6 | 6 | LOOP | flags, u16 passes, s16 back-offset → jump back until the passes run out (0 = no loop, 65535 = for ever). Flags as WAIT. |
| 7 | 4 | PANNING | pan, spread → pan ± a random spread. |
| 8 | 4 | SETNOTE | -, u16 pitch (Q8) → an absolute pitch. Not on the noise channel. |
| 9 | 4 | ADDNOTE | flag, s16 (Q8) → flag 1 sets the offset from the played note; flag 0 adds to the current offset. |
| 10 | 4 | VIBRATO | period, s16 step → a triangle: the step is added each frame and negated every period+1 frames (it starts half way). |
| 11 | 6 | PITCHSWEEP | n (0/1), s16 step, s16 limit → a sawtooth: the step is added each frame, and the sum wraps by the limit. |
| 12 | 6 | SLIDE | flag, s16 pitch, u16 frames → slide by (flag 1: to) a pitch over the given frames. Integer step a frame; it stops at the target. |
| 13 | 6 | GOTO | -, s32 offset → jump. It may land in another macro's code (shared tails). |
| 14 | 4 | SETADSR | hold, u16 table index (§5). |
| 15 | 8 | STARTSAMPLE | flags, u16 sample, u32 start. A start offset scales with velocity (flag 0x80 inverts that). Flag 1: unpitched. |
| 16 | 6 | TRAP_KEYOFF | -, s32 offset → where key-off sends the macro. |
| 17 | 2 | UNTRAP | Clear the trap. |
| 18 | 10 | SAMPLE RANGE | flags, u32 start, u32 length (0 = to the end) → play that range, looping over it if the sample loops or flag 0x80 is set. Flags 0x7F: backwards. |
| 19 | 6 | (wavetable scan) | PCM: steps a window through a built-in waveform. Not decoded further. |
| 20 | 4 | ? | Not decoded. |
| 21 | 4 | ENVELOPE | in/out, u16 rate (§5). |
| 22 | 6 | RNDNOTE | flags, u16 low, u16 high (Q8) → a random pitch. Flag 1: any fraction. Flag 2: about the middle of the range, from the note. |
| 23 | 6 | SPLITKEY | key, s32 offset → jump when note ≥ key. |
| 24 | 6 | SPLITVEL | velocity, s32 offset → jump when velocity ≥ the byte. |
| 25 | 6 | SPLITRND | n, s32 offset → jump when a random 0..254 ≥ n. |
| 26, 27, 28 | 2, 2, 4 | (portamento set-up) | Not decoded. |
| 29 | 6 | KEYMAP | -, u16 keymap macro, s16 key offset → play entry note+offset: its sample, pan offset, and transpose (unless unpitched). **The macro carries on.** A note outside the map plays nothing. |
| 30 | 8 | PLAYMACRO | -, flags (bit 0 don't reset), s8 key, priority, voice, u16 macro → start a macro on another voice at note+key, with this volume. That voice gets no sequencer key-off. |
| 31 | 2 | (no-op) | Advance only. |
| 32 | 2 | VOICE_ON | duty → square duty (written to NRx1). 255: volume >> 5. |
| 33 | 2 | SETNOISE | NR43 value. |
| 34 | 2 | KEYOFF | voice → key off that voice (0–3 PSG, 4+ PCM); 255 = itself. The handler then adds 2 to the macro pointer, even after a self-trap. |
| 35 | 4 | ? | Not decoded. |
| 36 | 2 | HARDENVELOPE | NRx2 low nibble (step + direction); not on the wave channel. |
| 37 | 2 | HARDENVELOPE_OFF | Clear it and retrigger. |
| 38 | 2 | (priority) | Set the priority's low byte. |
| 39 | 2 | ? | Not decoded. |
| 40, 41 | 4 | (age) | Set / add the allocator's age counter. |
| 42 | 4 | (game flag) | Set or clear a game-visible flag bit. |
| 43 | 6 | (if game flag) | Jump if a game-visible flag bit is set. |
| 44, 46, 48, 49, 53 | 2, 2, 8, 4, 6 | ? | Not decoded. |
| 45 | 6 | AUTOPAN | flag, low, high, s16 step → sweep the pan between low and high. Flag 1: bounce at the ends; otherwise stop. |
| 47 | 6 | SCALEVOLUME | flag, s8 add, -, u16 scale → (flag ? velocity : current volume) × scale ÷ 256 + add, clipped 0..127. |
| 50 | 4 | (portamento from last) | s16, u16 → a slide from the previous pitch. Not decoded further. |
| 51 | 4 | ? | Not decoded. |
| 52 | 2 | (age speed) | The age counter's speed per frame. |

## 8. Notes for implementers

- The commonest macro shapes:
  - Drums: STARTSAMPLE; KEYON; WAIT[flag 1, n]; KEYOFF[0]; STOP; END.
  - Melodic: (SETADSR | SETVOLUME) STARTSAMPLE / KEYMAP; KEYON; WAIT[key-off]; ENVELOPE; WAIT; STOP; END.
  - Game Boy voices: VOICE_ON / SETNOISE; KEYON; WAIT; HARDENVELOPE.
- `KEYOFF[0]` in sample macros keys off **Game Boy voice 0**, exactly as written. Whether that is a
  composing-tool default nobody noticed is unknown; the driver does it.
- The splits compare *value ≥ byte*. Reading them the other way round picks the wrong velocity layers
  and drum zones, and some shared layers live in other macros (Spider-Man macro 70 → 114).
- Not established: the mixing-rate value itself (it depends on the game), the left/right sense of pan,
  and the commands marked "?".
