# Other engines: how Krawall, GAX and MusyX would join the MP2K player

Status, 2026-10-29: **MusyX is in the main build** (steps 2 and 3 of section 8, and MusyX); Krawall
and GAX are not yet. This document says what the engines share, what has to happen when a cart
turns out to use one of them, and how the card stays one instrument with one UI whichever engine
is playing. "As built" below records where the build differs from the design.

## GBAModPlay (Logik State; added 2026-11-06)

**Read from Super Puzzle Fighter II Turbo** (credit "GBAModPlay (C) Logik State 2002").
- **Where else it is:** loveemu's lists put the driver in 46 games, among them Need for Speed
  Underground 2 / Most Wanted / Carbon, Driv3r, Medal of Honor: Infiltrator, Stuntman, Lego Knights'
  Kingdom, Bionicle, Asterix & Obelix XXL and Premier Manager 2004-05 / 2005-06.
- **Files:** the player is `engines/logik/lsplay.c` (our code, MIT); the glue is
  `payload/eng_logik.c`; engine id 4, name MODPLAY.

- **Driver routines (Puzzle Fighter):**
  - init 0x08348AFC: buffer, music bank, second bank, rate index (4 = 21024 Hz);
  - play a module 0x08348688; module set-up 0x08348A24;
  - row reader 0x0834902C, cell processor 0x08349ACC (row-effect jump table 0x087E6578);
  - tick effects 0x08349848 (kind table 0x087E654C);
  - pitch 0x08348488 (linear) and 0x08348444 (frequency), the Amiga path inline;
  - render loop 0x08349124, ARM mixers 0x08349FB8 (music) and 0x0834A0D8 (4-bit ADPCM, effects).
- **Module "GBAMOD30":**
  - +0x28 channels; +0x38 orders; +0x1B1C patterns; +0x1B20 rows;
  - +0x1B24 Amiga periods (0 = linear); +0x1B28 BPM; +0x1B2C speed;
  - +0x1B30 the order list (256 bytes); +0x1C30 each pattern's size (u32); patterns from +0x2034.
  - A pattern is one block per channel: {+4 volume-stream offset, +8 effect-stream offset, +0x10 the
    next block, from +0x14}.
  - Each block holds three run-length streams of {rows, value} pairs:
    - note+instrument, u16 big-endian (note = v >> 7, 0x1FF none; instrument v & 127);
    - volume column, u8 (0 none, else volume + 1);
    - effect, u16 big-endian (effect, parameter).
- **The sample bank is separate** and nothing in a module points at it: 256 x 12 bytes {length,
  finetune, volume, loop start, loop length, relative note}, then the samples back to back (signed
  8-bit). Discovery wants a bank that has every instrument the modules play: first right after a
  module (where Puzzle Fighter's is: 0x080E4B00, 39 sounds), else within 1 MB of them.
- **Pitch (linear mode, all Puzzle Fighter modules):** period = 7616 - (rel + note - 2) x 64 -
  finetune / 2 + porta; frequency = T[x mod 768] x 4 >> (7 - x / 768), x = 7680 - period, T[i] =
  16726 x 2^(i/768). That is FastTracker 2's linear table; generated, it matches the driver's 768
  entries. Note 98 (with no instrument) shifts out to nothing: a key-off.
  - **Amiga mode:** the period of a 1/8-semitone table, then 7093789 / (period x 2). The table is
    generated here; ProTracker's own is hand-tuned and differs by up to 3.
- **Level:** volume x the game's music volume (Puzzle Fighter's default 90) >> 6. The mixer adds
  sample x level >> 8, mono, into an 8-bit buffer that wraps (we clip).
- **Timing:** ticks/s = BPM x 2 / 5, a row every `speed` ticks. Puzzle Fighter's 24 modules are all
  150 BPM at speed 1, so 24 rows to a beat. One driver tick is one 24-PPQN tick, as with Krawall.
- **Effects (Puzzle Fighter uses only 1, 2 (all of them Ex/Fx fine slides), B and D):**
  - 1/2: XM linear slides, with E/F fine on the row; Amiga slides per tick;
  - 3 tone porta, 4 vibrato (it works in Amiga mode only: the linear path never adds it);
  - 5/6/A volume slide; B position jump; C volume; D pattern break; E6x pattern loop; F speed/BPM;
  - 0 arpeggio is stored but never played; 7, 8, 9 and the other Ex do nothing.
- **Driver quirks kept:**
  - tick effects run on channels 0-3 only;
  - the first tick after a row sees counter 0, so it skips its volume slide;
  - the volume column overrides the volume after the effects.
- **Changed from the driver:**
  - D into a row, and E6x, seek. The driver reads on in the old streams, which would play the
    wrong rows.
  - Tone porta in linear mode glides as XM does. The driver's never recomputes the pitch. No cart
    seen uses it.
- **Song kinds:** a module whose jumps lead back to an order already played loops (MUSIC); else
  its length decides. Puzzle Fighter: 15 music, 4 jingles, 5 effects.
- **Checks:**
  - modtest's 8th cart is mkengines.py's lsplay.gba (226 checks);
  - fuzz finds 0 bad reads;
  - every other dump scans as before, and MP2K WAVs are byte-identical.
  - Payload 159.6 KB (+15 KB).
  - **Not heard on hardware or by ear yet.**
- **Not supported:** the effect bank (bank 2, 4-bit ADPCM, at 0x08132C00 in Puzzle Fighter).

### Version 2: the 2003 driver (Driv3r; added 2026-11-06)

Driv3r's credit is "LS_Play (C) Logik State 2003" (now in the sniff). Its modules keep the
"GBAMOD30" magic, but the layout is new. `ls_module_valid` returns the version: 1 if +0x38 is an
order count, else 2.

- **Driv3r routines:**
  - module set-up 0x087D9310, bank set-up 0x087D93D0, init 0x087D9484;
  - row reader 0x087D9998, cell processor 0x087DAB18 (row table 0x087DAD2C);
  - tick effects 0x087DA800 area (kind table 0x087DA868);
  - linear pitch 0x087D89AC, frequency 0x087D8968, ARM mixer 0x087E4278 (unchanged).
  - The game calls the driver through ARM veneers at 0x087E4934..0x087E49DC. Init gets a config
    block at 0x0877A104 {..., bank 0x087AEA94, ...}, and "play song n" reads 10 module pointers at
    0x0877A0D0.
- **Module:**
  - +0x28 channels (the driver plays 8 at most); +0x34 orders; +0x38 a 256-byte identity table (unused);
  - +0x13C rows; +0x140 Amiga; +0x144 BPM; +0x148 speed;
  - +0x14C the order list (256); +0x24C each pattern's offset (u32) from +0x650, where the patterns are.
  - There is no pattern count (the order list's largest entry + 1) and no pattern size.
  - Patterns and the streams are as in version 1.
- **Bank:** 256 × 16 bytes {length u32, finetune, volume, loop start, loop length, relative note
  u32 (signed: Driv3r has -6)}, the samples from +0x1000. It follows the last module, as in version 1.
- **Changed in the driver, and followed:**
  - tick effects on every channel (version 1: four);
  - vibrato works in linear mode: (half-sine[pos >> 2] × depth) >> 5, negative for pos > 31,
    pos += (speed + 4) >> 1. It is a pitch offset that only a new note clears.
  - 4xy sets speed and depth separately and restarts the sine; a row with 4 keeps the vibrato going;
  - the sample's end is no longer length - 1;
  - all three volume stages are 64 (Driv3r's init), so a voice's level is its volume.
- **Songs end on a silent pattern that jumps to itself** (Bxx to its own order, no notes): Driv3r's
  converter ends every non-looping song that way.
  - lsplay treats it as the song's end: it stops, or restarts under SONG END = REPEAT.
  - The song is classified by the length before it, so it isn't counted as a loop every few rows.
- **Driv3r:** 10 modules (1-6 channels, 102-162 BPM, speed 6-9, effects A and D), 16 sounds;
  6 music, 3 jingles, 1 effect. Fuzz finds 0 bad reads; Puzzle Fighter's renders are unchanged.
  modtest's 9th cart is mkengines.py's lsplay2.gba (244 checks).
- **Need for Speed Underground 2 and Carbon (2026-11-06):** the same 2003 driver code, both credited
  2003 (Underground 2 as "GBAModPlay (C) Logik State 2003", Carbon as "LS_Play"). What they added:
  - **The bank format varies independently of the module layout:** Carbon's has 16-byte entries
    (as Driv3r's), Underground 2's 12-byte entries (as Puzzle Fighter's) with version 2 modules.
    - `LsPlayer.bank_format` is separate from `version`. `ls_bank_format()` tries 16 then 12 (for
      version 2 modules), checked against the modules' instruments.
    - Discovery passes the format it found to `ls_start()`, which would otherwise re-detect it
      without them.
  - **Samples can be long:** Carbon's first is 295 KB. LS_MAX_SAMPLE is now the mixer's 512 KB.
  - **The bank can be far away:** Underground 2's is 3 MB from its modules. The fallback search runs
    forward from the modules' end, round the whole cart.
  - **Order count 255 with a 255-filled list (Carbon).** The first 255 now ends a version 2 order
    list. The driver has no such check (it compares with the count, and would play on), so the
    games must stop those songs themselves.
  - **Song end:** version 2's driver goes round to the start at the order list's end unless the game
    turned that off (an API call, not data). So a version 2 song that runs off its end is music.
    Explicit endings (the silent self-jump) stay jingles and effects. This made two of Driv3r's
    short songs music.
  - **The layout test** checks version 2's fields first (rows, Amiga, BPM, speed at +0x13C..+0x148).
    One Carbon module passed the version 1 test.
  - Underground 2: 9 modules (88 sounds), all music. Carbon: 5 modules (26 sounds). Several of
    Carbon's are one channel playing a long recorded sample. 0 bad reads, 0 skipped effects; both
    quiet (-24 to -28 dB).
- **Not seen yet:** the 2004-06 driver ("4.0" on Logik State's page). Underground 2 (2004) and
  Carbon (2006) still carry the 2003 driver.

## GAX 2 (feasibility 2026-11-03, implemented 2026-11-05)

**Implemented:** gax.c has a V2 mode (`gx_v2_entry`, `gx_start_v2`; channel pattern tables in
`GxPlayer.chan[]`, the V2 instrument offsets); eng_gax.c finds song entries when the version text
says 2.x (`gx2_words`, an IWRAM loop that tests only a count 4..40 followed by a cart pointer), keeps
the entries in `s_songs`, and plays them with signed samples. A boot from a saved slot runs the sniff
first so the version is known. Found: Tang Tang 10, Spider-Man: Mysterio's Menace 22, Tony Hawk 3 14,
Bruce Lee 12. Andy judged scratch renders of all four "right" by ear (2026-11-05). Levels: at the
payload's half volume Tony Hawk 3 is the hottest cart (0.13 % of samples at full scale on its worst
song, against 12 % in the trial player); GAX 2 stays at GAX 3's level. Still unread: the second
envelope (+128) and the +36 word. GAX 1.99 carts are not covered.

**The replayer, read (2026-11-06).** Tang Tang's GAX 2.01a code was disassembled. The routines are
the row reader 0x08026B34, note 0x08026CF4, instrument load 0x08026D38, perf-list step 0x08026D8C,
per frame 0x08026F54 / envelope 0x0802735C / vibrato 0x08027428 / modulation 0x08027468, and voice
setup 0x08026FF4. The ARM mixer is 0x08027C58 (voices) and 0x08027A60 (output). gax.c now does what
they do. shinen-gax-python (MIT) was checked for GAX 3 where they overlap:
- **Notes:** a note without an instrument only sets the pitch (legato). An instrument restarts the
  envelope, perf list, vibrato and step volume. In GAX 2 it does not stop the sounding wave: the perf
  list's next wave trigger replaces it.
- **Pattern effects:**
  - 3xx is tone portamento over xx frames (the note's own pitch is not set);
  - 7xy plays x frames on its row, then y (high nibble first; shinen-gax-python agrees);
  - EDx delays the row's note by x frames;
  - 4, 5, 6, 8, 9 and other Ex do nothing in the driver.
- **Perf list:**
  - 6 sets a loop count and 5 jumps until it runs out;
  - the perf volume resets only on a row that triggers a wave;
  - a perf speed of 0 never steps.
- **Instrument vibrato** (+8 wait, +9 depth, +10 speed): a 64-step sine, (sine × depth) >> 8 in
  1/32 semitones, every frame.
- **Envelope:** the stored slope (+6 of each point) × frames since the previous point, >> 8.
  - It holds at the sustain point's exact time, and jumps from loop end to loop start until released.
  - A last point of 0 with no loop ending there silences the voice.
  - (GAX 3 keeps the interpolation it had: shinen-gax-python ignores the slope.)
- **Waves:**
  - Each of four slots is a 28-byte block at +12 + slot × 28: {modulation flag, -, -, -, start,
    loop start, loop end, modulation size, step, speed u16, fine tune s16}. The sample is +1 + slot.
  - It loops when loop start < loop end, else plays to the sample's end.
  - The fine tune is in 1/32 semitones. The pitch table puts note 48 at 8372 Hz (GAX 3: 8363).
  - The byte after the modulation flag is never read: the "ping-pong" flag was a guess, now dropped.
- **Wave modulation:** the loop is a window of the modulation size. Every speed frames it moves by
  its step, bouncing between loop start and loop end, and the play position moves with it.
  `mixer_window()` (payload/mixer.c) applies each move. The window does not move on a reversed
  voice, or on one that CHAOS gave another sample.
- **Level:**
  - envelope × perf volume × step volume × song volume, where 255 is unity at each stage;
  - each voice adds (sample × level) >> 8;
  - the sum is × (channels + 8) / 16 (unity for one channel) and clipped to 8 bits.
  - That is the payload mixer at shift 0, so GAX 2 plays at the driver's own level
    (`GX2_VOL_SHIFT 0`). Loud songs clip as they do on the GBA: Tony Hawk 3 and Bruce Lee on
    0.4-1.1 % of samples. The card's GAIN setting trims it.
- **Not reproduced:** the driver's echo and output filter (0x08027B7C, 0x08027AB4) and its
  nearest-sample mixing.
- **Checks:**
  - the U masks are now empty on Tang Tang, Tony Hawk 3, Bruce Lee and Spider-Man MM (they had shown
    tone porta, modulation and ping-pong);
  - fuzz finds 0 bad reads;
  - MP2K WAVs are unchanged.
- **Test changes:** mkengines.py's gax2.gba instrument now has perf speed 1. The GAX tests expect
  7xy's high nibble first and use effect 4 as the unknown one.

### The study

Studied on dumps of Tang Tang (2.01a), Spider-Man: Mysterio's Menace (2.01b), Tony Hawk's Pro Skater 3
(2.11) and Bruce Lee: Return of the Legend (2.35), against loveemu's research note (V2 section).
**Feasible: GAX 2 is GAX 3's sequencer with a different wrapping and instrument layout.**

- **Top level, the same in all four versions:** a song entry is a count n >= 4 and n pointers to 7-word
  handlers {three Thumb code pointers, a count, a pointer, a number, a data pointer}. Handler 1's data
  is the song header; handlers 3.. are the channels, each one's data a pattern table. Found by
  structure: 10 / 22 / 14 / 12 song entries.
- **Header:** V3's first 26 bytes (channels .. mixing rate, FX slots), no channel pointers (they come
  from the channel handlers), a trailing pointer from 2.3 on.
- **Pattern tables and sequence data are V3's:** with a V3 header built from the pieces, the V3 decoder
  reads whole songs with no error, and the notes are plausible (Bruce Lee song 1: channel 3 plays 100
  notes on one instrument, channel 0 mostly instruments 1 and 11, a clean loop after 12 orders).
- **Instruments are V3's fields at other offsets, one wave:** sample +1, vibrato +8..10, start / loop /
  end +16 / +20 / +24, envelope pointer +124 (V3 envelope format), a second envelope +128 (one point
  in the samples seen; V3 has none), performance speed / count +132 / +133, performance rows +136 (V3's
  8-byte rows). +36 is a word that is sometimes a pointer, meaning unknown. Mapped in a scratch copy of
  gax.c, all four versions play with no decoder error.
- **Samples are SIGNED** (mean step 8-28 read signed against 37-83 unsigned; Finding Nemo's GAX 3 is
  the reverse). The note's comment that samples are unsigned from GAX 2 on is wrong for these. The
  sample table's entry 0 can be empty (address 0), which V3 discovery refuses.
- **What real support needed (done 2026-11-05, above):** discovery by handler structure (it needs Thumb pointers, so it is
  its own pass), a V2 mode in gax.c (channel table pointers, the instrument offsets, the second
  envelope and +36 understood or ignored), signed samples for V2 tables, and the levels (the scratch
  renders run hot: Tony Hawk 3 at full scale on 12 % of samples, before the payload's half volume).

## Real carts (2026-11-03)

Dumps of four of Andy's carts settled what the hardware reports could not:

- **MusyX (BattleBots, Denki Blocks): notes but no sound.** Each song group starts with 16 bytes, the
  starting program of each MIDI channel; tracks started on program 0, and these songs send no program
  changes, so every part played the first sound (on Denki, a PSG macro). Fixed in `ms_start`. Still
  silent then: the parts written for the Game Boy channels (VOICE_ON / SETNOISE / HARDENVELOPE; the
  CAL row shows `PSG`), which now play too (musyx.c keeps the four channels' state, eng_musyx.c writes
  the registers, pcplay emulates them). Keymaps (drum kits, multisampled instruments) now play.
  Harry Potter: Chamber of Secrets was hidden by reading the sample root key as two bytes; it is one.
- **MusyX follows the cart's own driver (2026-11-06).** Lady Sia and Go! Go! Beckham sounded unlike
  the games, so the driver in the dumps was disassembled (its macro interpreter is a 54-entry table
  of Thumb handlers, the same in all eleven MusyX carts). musyx.c's "the voices" now does what it
  does: macros, ADSRs (op 14), fades (op 21) and pitch modulators run once a video frame (WAIT
  counts frames; they ran on the 96-PPQN tick, about three times fast); tracks 0-3 play on the
  Game Boy channels and 4-15 on PCM voices; key-off only marks a voice and END leaves its sample
  playing; op 29 plays a keymap entry and the macro carries on (it was taken for PLAYMACRO and ended
  the macro, losing the strings' volume and release); the splits were inverted. The mix is the
  driver's: its pan law, our mixer at full gain, and the Game Boy channels at 50 % (SOUNDCNT_H
  0x9A0D), which had been about 9 dB too strong against the samples. CLAUDE.md has the details.
- **Krawall (Lemony Snicket): not found.** The cart has no "Krawall" text, only the player's CVS line
  `$Id: player.c,v 1.4 2003/03/19 ... seb Exp`, now a signature when "seb" (the author) follows. Its
  20 modules then play. Discovery was also brought in line with UnkrawerterGBA (banks judged on
  their first four pointers; envelope counts of 255, junk sample-map bytes and wide pans accepted),
  a module is read in one format only, and the songs inside a module (split by 254) are listed.
- **Krawall now runs Krawall's own player logic (2026-11-06).** `engines/krawall/krawall.c` is a port
  of Sebastian Kienzl's lib/player.c and player_effects.h (LGPL-2.1, kept in its own files with
  `COPYING.LGPL`) over bounds-checked reads and our mixer: every effect including the volume column,
  Krawall's period tables, envelope stepping, fadeout and auto-vibrato, its levels (volShift 0), and
  its quirks. It fixed the trial engine's Gxx (it overwrote the global volume), decimal Dxx breaks,
  and volume resets on notes without an instrument. Lemony Snicket and Narnia now report no
  unsupported effects. docs/TRACKERS.md has the reasoning and the plan for Engine Software.
- **GAX (Tang Tang): GAX Sound Engine v2.01a**, which this player cannot read; the result screen now
  says so.
- **The scan:** each pass checks a word with two or three aligned loads in IWRAM and runs its full
  validator only where they allow a structure; carts naming GAX or Krawall skip the MP2K heuristic
  and MusyX passes.

## As built (2026-11-01): all four engines

- **GAX and Krawall are in** (`payload/eng_gax.c`, `eng_krawall.c`), MusyX moved to `eng_musyx.c`, and
  all three run on ONE shared transport in `engine.c` (`EngCore` in `eng_core.h`: start, tick,
  snapshot, lanes, sync, tempo; the transport does the queue, REPEAT BAR, clock out, counters).
- **The scan finds every engine on a cart.** The sniff moved into the signature pass (the IWRAM
  word loop), so GAX/Krawall text is known whether or not MP2K is there; their discovery runs when
  their text is found, MusyX's when nothing else turned up, all of them on SET > RESCAN = FULL.
  Each engine's songs are their own table(s). The progress bar is shared among the passes that run.
- **GAX discovery follows gaxtapper's v3 rules** (pointers, alignment, reserved +0x1E, sample
  table's second word 0). The first validator also gated on the mixing rate, volume, loop point and
  every pattern's first byte; those are gone from discovery (a bad loop point now loops to the top;
  a bad pattern still stops the song at playback). The CAL row counts what the old checks would have
  kept (`OLD`), the zero-channel FX banks (`FX`), and the commonest reason a near miss failed. A GAX
  2.x version string gets no table (its header layout differs; the "songs" would be chance matches).
- **Krawall's banks:** the largest sample and instrument bank found, overridable on the CAL page
  (`g_krBankWant`, saved as patch v8's `krSampleBank` / `krInstrBank`).
- **Songs, jingles, effects by content** (`g_songKind`): each engine reports length and looping;
  MP2K's player number is only a tie-break for a short loop. The LIST filters use the kind.
- **CHAOS on every engine** (`engines/common/eng_chaos.c`), in the engines' `note` hook, applied after
  the engine on every start and update (`eng_mixer.c`); reseeded at starts and counted loops.
- **Boot from a slot:** GAX and Krawall tables rerun that engine's discovery only (their song lists
  live in their discovery); MusyX and MP2K install directly.
- **Size:** payload 114 KB (about 17.3 s to upload; `-Os` for the engines is not an option, it
  turns constant divides into libgcc calls). IWRAM about 12.7 KB.

## As built (2026-10-29)

- **One mixer:** `MIX_F_UNSIGNED` is in `payload/mixer.c` (one EOR per fetch; MP2K's WAVs are
  byte-identical). `engines/common/eng.h` holds `EngRom`/`EngVoice`/`EngHooks`; the three engine
  headers typedef their own names to them (each engine's own tests still pass).
  `engines/common/eng_mixer.c` is the one adapter, with a volume shift per engine (MusyX: 0 since
  2026-11-06, when musyx.c took its driver's own levels and pan law; 1 before). The engines' `pcm_mixer.c` copies are not linked.
- **The dispatcher** is `payload/engine.h/.c`, a table of functions per engine, reached through
  `g_eng`. **The view is not a new `EngView` struct**: it is the transport state mod.c and ui.c
  already read (`g_tracks` lanes, `g_plTicks`, `g_plLoops`, `g_plStarts`, `g_plClockOut`,
  `g_plQueued`, `g_plHoldBar`...), which every engine fills, so neither file needed more than its
  calls changed. MP2K keeps its own section code in player.c (tested, and spec'd); the other
  engines share one copy of it in engine.c, in the same order as `player_tick()`.
- **The scan:** `ScanResult.engine`. When neither MP2K pass finds a table, every 64 KB is offered
  to `eng_discover_slice()` (MusyX: `ms_scan`, kept only if a song resolves); `scan_select()` asks
  `eng_table_songs()` for another engine's list. `tools/m4a_scan.py` mirrors only the MP2K passes
  and the sniff: pcplay links the engines' own discovery code, so the PC predicts the GBA with the
  same C. Patch v7 appends `tableEngine[8]`, so boot-from-slot skips the scan on a MusyX cart too.
- **MusyX:** four native ticks per tick; tempoI from `ms_tick_hz_q16` (x 41147 >> 32). Its bar
  snapshot holds the tracks, tick count, tempo and dice only: voices and mods stay live (the
  stale-transpose lesson from MP2K's REPEAT BAR). RESTART LOOP restarts the song. Unsupported, so
  dimmed "N/A" on MAP/BTN/BASE: ATTACK, DECAY, SUSTAIN, RELEASE, LENGTH, VIBRATO, TREMOLO, LFO,
  CHAOS, REROLL. CAL's last row is `eng_diag()`. **Not done:** the SET row for STRICT/PREVIEW
  (it plays PREVIEW, as the trial did by default).
- **Size:** payload 82.5 KB (about 12.5 s to upload), IWRAM about 12 KB.

## What the branches contain

| Branch | Directory | Engine | Native time | Code (ARM, -O2) | Player state |
|---|---|---|---|---|---|
| `codex/krawall-poc` | `engines/krawall/` | Krawall (XM/S3M-style tracker) | tracker ticks, 6 per row | 8.7 KB + 1.4 KB discovery | 4.0 KB |
| `codex/gax-poc` | `engines/gax/` | Shin'en GAX 3.05-style | 59.73 Hz frames; a row every `speed` frames | 8.5 KB | 3.2 KB |
| `codex/musyx-poc` | `engines/musyx/` | MusyX (PCM subset) | 96 PPQN | 9.9 KB | 1.7 KB |

The branches are stacked: musyx contains gax, which contains krawall. None of them touches
`payload/`. Each has a host test suite, an ARM build check, and a separate trial UF2 with its own GBA
`main` (buttons only: no MAP, no patches, internal clock). **First hardware result (2026-10-29):
the MusyX trial found music on Lady Sia and played it, and it sounded accurate.** Krawall and GAX
have not been heard yet. Testing the rest of the trial UF2s on real carts comes before any of the
work below.

All three were built the same way, which is what makes integration tractable:

- **One interface shape, four times.** Each has a ROM view (`KrRom` / `GxRom` / `MsRom`: data
  pointer, bus base, size), a voice (`KrVoice` / `GxVoice` / `MsPcm`: data, size, loop, start, Q12
  step at 16384 Hz, left, right, reverse), hooks (start, update, stop, done, note, event) and mods
  (mute, pitch, voice shift, table offset, scrub, tempo, sample start/end, reverse/drop). The
  fields are the same; only the prefix differs.
- **The same adapter, three times.** Each `mangle_adapter.c` / `mixer_adapter.c` binds the hooks to
  a mixer and copies `PlayerMods` into the engine's mods, then fills our `Track` followers.
- **The same mixer, three times.** Each `pcm_mixer.c` is `payload/mixer.c` renamed (`kr_pcm_*`,
  `gx_pcm_*`, `ms_pcm_*`). The only change is the sample fetch: Krawall and GAX samples are
  **unsigned** (`(uint8_t)d - 128`); MusyX's are signed, like MP2K's, so its copy is byte for byte
  ours. Each copy's renderer is 2.8 KB of IWRAM code.
- **The same trial skeleton, three times**: a service loop accumulating `*_tick_hz_q16()` against
  Timer 2, posting the follower and the pulses over the link.

## 1. Common code: factor once, delete the copies

| Shared piece | Where | What it replaces |
|---|---|---|
| `MIX_F_UNSIGNED` | `payload/mixer.c`: one XOR of 0x80 on the fetched byte when the flag is set | three renamed mixers (saves ~8 KB of IWRAM and keeps one renderer per audio block) |
| `EngRom`, `EngVoice`, `EngHooks`, `EngMods` | `engines/common/eng.h` | the prefixed twins in each engine header |
| `eng_mixer_hooks()` | `engines/common/eng_mixer.c` | three identical start/update/stop/done sets |
| `eng_mods_sync()` | `engines/common/eng_mods.c`: `PlayerMods` to `EngMods`, plus the unsupported mask from the engine's capability bits (section 5) | three near-identical `*_mangle_sync()` |

Everything the modular touches is **already engine-neutral** and stays as it is:

- the link and the host firmware, and the follower and beat/loop pulses (`link_post_note`,
  `link_post_pulse`);
- `clock.c`: INT/EXT, PPQN, smoothing, flush, reset alignment;
- `mod.c`: MAP, BTN, BASE, edges, cue and PLAY, CLEAR, the switch;
- the output stage in the mixer (crush, echo/reverb, gain);
- the UI pages, patch slots, boot-from-slot.

What stays engine-specific is only the sequencer core, its discovery, and its native-time
conversion.

## 2. The dispatcher

A small table of function pointers per engine, in `payload/engine.h`:

```c
typedef struct {
    const char *name;                         // "MP2K", "KRAWALL", "GAX", "MUSYX"
    uint64_t    destMask;                     // destinations it honours (section 5)
    int  (*discover)(uint32_t from, uint32_t to);   // one slice; adds songs to the common list
    int  (*start)(const EngSong *s);          // 0 = unplayable
    void (*stop)(void);
    void (*tick)(void);                       // ONE 24-PPQN tick; converts to native time inside
    void (*frame)(void);                      // ~60 Hz housekeeping (MP2K envelopes; else no-op)
    uint32_t (*tick_hz_q16)(void);            // INT tempo, for clock.c
    uint32_t (*sync)(const PlayerMods *m);    // apply mangles; returns what it could not honour
    void (*restart_loop)(void);
    void (*restart_bar)(void);
    void (*view)(EngView *v);                 // what the UI and the follower read
} Engine;

typedef struct {                              // the neutral view
    uint8_t  trackCount;                      // lanes shown: min(native, 16)
    struct { uint8_t exists, note, gate, act; uint32_t notes; } lane[16];
    uint32_t ticks;                           // 24-PPQN ticks since start (BAR/BEAT display)
    uint8_t  beats, loops, running, error;
    uint16_t bpm;
} EngView;
```

**MP2K goes behind it first, with no behaviour change.** The check is pcplay: every test song,
rendered before and after, must give byte-identical WAVs. Then the call sites switch from
`player_*` to `g_eng->`:

- `mod.c`: `start_song`, `mod_stop`, the restarts, the queued PLAY, the follower;
- `clock.c`: `player_tick()` and `player_tempo_i()`;
- `main.c`: `player_frame()`;
- `ui.c`: `g_tracks`, `g_plTrackCount`, `g_plTicks` (via the view).

The queued restarts and REPEAT BAR (`g_plQueued`, `g_plHoldBar`, the section counter) move out of
player.c into the dispatcher, so every engine gets them for free.

## 3. What runs when a cart is recognised

The scan keeps its order, and only the end changes:

1. **MP2K signature pass** (SelectSong). Found: MP2K, as now. MP2K always wins.
2. **The heuristic pass**, which already runs the engine sniff (`scan.c` `kSniff`: "GAX Sound
   Engine", "GAX ENGINE V", "Krawall", "Extended Module: ").
3. **New: each sniffed engine's discovery**, in preference order, sliced with `gfx_idle()` so the
   link and the progress bar stay alive:
   - Krawall: `kr_discover_slice()` (modules, plus sample and instrument bank candidates);
   - GAX: `gx_scan()` (song headers point straight at their banks);
   - MusyX: `ms_scan()` + `ms_catalogue()`. It has no confirmed text signature, so its structural
     scan runs only when nothing else matched (the sniff list takes only signatures confirmed at a
     primary source).
4. Every playable song from every engine goes into **one common song list** of descriptors:
   `EngSong { engine, addr, aux[3] }`. For MP2K that is the table entry; Krawall, the module and
   its banks; GAX, the header; MusyX, root and index. **One engine per table**: a cart with two
   engines (rare) shows them as two SONG TABLEs, and switching table switches engine (it already
   stops playback and cues item 1).

**Krawall needs one human decision.** Its discovery cannot prove which sample and instrument bank
belongs to which module (the trial cycles candidates by hand). So for Krawall only:

- SET grows two rows, SAMPLE BANK and INSTR BANK, shown only on a Krawall table;
- the choice is a **cart profile**, saved in the patch beside the song tables that v4 already
  caches. Patch v7 appends an engine id and the aux words per table, and `scan_install()` learns
  to install them. Boot-from-slot then skips discovery for every engine, as it does for MP2K now.

## 4. Clock: everything speaks 24-PPQN ticks

`clock.c` stays the only clock. It calls `g_eng->tick()` once per 24-PPQN tick (INT or EXT) and
asks `g_eng->tick_hz_q16()` for the INT rate. Each engine converts:

| Engine | One 24-PPQN tick = | Notes |
|---|---|---|
| MP2K | one `player_tick()` | as now |
| Krawall | one `kr_tick()` | 6 ticks/row × 4 rows/beat = 24 per beat: native. Speed commands change ticks per row, so a "beat" follows the module. |
| MusyX | four `ms_tick()` | 96 PPQN native; its macros and envelopes run on `ms_frame()` (59.73 Hz), as its driver's do |
| GAX | `speed × rowsPerBeat ÷ 24` `gx_frame()`s, accumulated in Q8 | GAX is frame-based (envelopes and performance lists run per frame). INT simply runs frames at 59.73 Hz × tempo. EXT schedules frames so that rows land on the clock, assuming 4 rows a beat. **Labelled approximate**; the GAX branch itself warns against pretending otherwise. |

A stopped clock holds notes for every engine: the mixer keeps running whatever the sequencer does.

## 5. Mangles and sections, one UI

Each engine declares `destMask`, one bit per destination it honours.

- **Common to all four** (the engines' own mods, or the shared mixer): mute / layers / solo,
  transpose, fine pitch, tempo, voice shift, table offset, start / end, reverse / drop, ROM scrub,
  crush, reverb, and every edge (clock, PLAY, STOP, RESET, NEXT, SONG, CLEAR...).
- **Can become common** through the engines' `note` and `update` hooks, in shared code:
  - **CHAOS**: the `note` hook can jump, pan, swap and reverse a voice;
  - **the LFO** (vibrato / trill, tremolo / gate): the `update` hook can scale step and volume.
  Written once in `engines/common/`, they then work on every engine.
- **MP2K only, for now**: ATTACK / DECAY / SUSTAIN / RELEASE and NOTE LENGTH (each engine's native
  envelopes differ).

On MAP, BTN and BASE, a destination outside the engine's mask is **shown dimmed, with "N/A ON
GAX"** (or whichever engine), and its value is ignored. Nothing about the pages changes per
engine, so a patch moves between carts and engines and just does less on some.

Sections:

- **RESTART BAR** becomes engine-neutral. The engine players are plain structs with no outside
  pointers (Krawall 4.0 KB, GAX 3.2 KB, MusyX 1.7 KB), so a bar snapshot is a copy of the whole
  struct at each section line, taken by the dispatcher. MP2K keeps its own (it also has the
  channels).
- **RESTART LOOP** is `seek(restart order)` for the trackers, `restart` for MusyX.
- **AT BAR END** and **REPEAT BAR** work on the dispatcher's tick count, so they come free.

## 6. The UI, the same everywhere

- **PLAY page:** reads only `EngView`. Lanes, notes, gates, mutes, the cue / PLAY / STOP, the LIST
  and SONG rows are unchanged. The header gains the engine's name. A tracker with more than 16
  channels shows its first 16 (lane paging can come later; mutes beyond 16 would need a wider
  mask).
- **SET page:** engine-specific options appear only as extra rows when that engine's table is
  selected (Krawall's banks; MusyX's STRICT / PREVIEW).
- **CAL page:** the engine's diagnostics (`unsupported` masks, `error`, `warnings`) in one row, so
  a song that sounds wrong can be reported with its numbers.
- **Patches:** the same slot format; the scene restores only on the same cart, as now.

## 7. Budget

- Payload today: 65 KB, about 9.9 s to upload at multiboot's ~6.5 KB/s.
- The three cores and discovery add about 29 KB, plus ~2 KB of shared adapters: **about 96 KB and
  15 s**. EWRAM holds 256 KB, so it fits.
- Loading an engine after boot instead (the host sends only the one the cart needs) does not pay:
  a link transfer runs at about 0.85 KB/s (slot loads are a byte per link word), so 10 KB would
  take 12 s on top of the boot.
- IWRAM (now ~11 KB of 32 KB) gains nothing if the mixers are merged; keep the engines' sequencers
  in EWRAM (they run once per tick, not per sample) and measure.

## 8. Order of work

1. **Test the three trial UF2s on real carts** and record what plays, per game, before anything
   else.
2. `MIX_F_UNSIGNED` and the common types and adapters. No behaviour change; host tests stay green.
3. The dispatcher with MP2K behind it. pcplay WAVs byte-identical.
4. **Krawall**: closest to our model (24 ticks a beat, tracks map to lanes), but needs the bank
   profile UI.
5. **GAX**: banks come from the header (no profile), but its clock mapping is approximate.
6. **MusyX**: preview-quality playback, and needs real dumps to verify its layout first.

Each step is a separate, testable change, and the card keeps working on MP2K carts throughout.
