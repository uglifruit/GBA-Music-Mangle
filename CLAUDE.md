# GBA Cart Player — Claude working notes

GBA Multiboot link for the Music Thing Modular **Workshop Computer**. The Workshop boots a GBA that
has a **commercial cartridge inserted**. The payload scans the cart for Nintendo's MP2K
(Sappy/m4a) song data, and plays it through its own engine, clocked and mangled by the modular.

Read this before editing. **THIS FILE DOES NOT GO INTO A RELEASE.** It is working notes addressed to
an assistant.

## What this is

- **The third sibling** of card 108 "GBA PSG Voice" (`uglifruit/89_GBA`, local
  `..\WorkshopGBA`) and "GBA FM Voice" (`uglifruit/GBA_FM`, local `..\WorkshopGBA-fm`). It was seeded
  from the FM tree on 2026-10-25. **Read the FM tree's CLAUDE.md for everything about the shared
  layers** (multiboot, the serial link, the inverted pulse pins, the inter-word gap, the protocol
  rules, the DMA-ring audio design, IWRAM placement, the `-nostdlib` tripwire, the UI rules, the HAL
  fixes, `info.yaml` and the validator). None of it is repeated here, and all of it applies.
- **Repo: `uglifruit/GBA_CartPlayer`** (private, created 2026-10-26). No card number yet.
- **The player (`payload/player.c`, `cgb.c`, `cgb.h`) was written by an outside developer** against
  `docs/PLAYER_SPEC.md`, on branch `codex/mp2k-player`, merged 2026-10-26. It passed the spec's checks.
  **The spec is the contract**: `player.h` is its interface. Changing a hook's meaning means
  changing the spec too.
- **Not yet tested on hardware** (as of 2026-10-26). The engine is verified on the PC only (see
  "Testing").

## OPEN RISKS, in the order the first hardware session should settle them

1. **Multiboot with a cart inserted needs START+SELECT held at power-on.** FIX94's
   gba-link-cable-dumper relies on this, but it hasn't been seen on this console. If it doesn't
   work, nothing else matters.
2. **The FM card's DMA-IRQ assumption** (both sound DMAs raise an IRQ after every FIFO block) is
   inherited unchanged, at 16384 Hz. A click at 64 Hz (16384/256), or silence, means it does not
   hold.
3. **Wait states.** `rom.c` sets 3/1 with prefetch (what most games use). If a cart misreads (scan
   finds nothing on a known MP2K game, or samples crackle), try SET > CART SPEED SAFE (4/2).
4. **Cart reads while the mixer runs.** Samples are read from the cart inside the DMA2 IRQ. Watch
   the DSP peak on the CAL page with 12 voices.

## The engine (decisions, and why)

- **pret's pokeemerald decompilation is the authority for m4a behaviour.** The tables in
  `m4a_tables.c` and every constant in `m4a.h` came from it, not from memory. saptapper supplied the
  SelectSong signature: 4-aligned, ≤ 8 of 30 bytes may differ, song table pointer at +40.
- **The sequencer is clocked from outside** (`player_tick()` = exactly one 24-PPQN tick). Envelopes
  (`player_frame()`, ~59.73 Hz) and the mixer keep running, so a stopped clock holds notes.
- **Notes start at their tick, not at the next frame** (m4a defers by up to 16.7 ms; a clocked
  instrument can't).
- **16384 Hz output** makes a note's step `sFreq × ratio >> 34` exactly, with no divide.
  Fixed-rate samples use the game's own mixing rate, found by the scan's sound-mode heuristic
  (default 13379).
- **Mute happens after the envelope.** Muted tracks keep playing silently, so an unmute lands
  mid-note. On the PSG, mute masks NR51.
- **Nothing from the cart is trusted.** Every pointer goes through `rom_ok`. There are 64 commands
  per track per tick at most, `PORT` writes nothing, and `MEMACC` has a private 256-byte area. The
  mangles exist to read from wrong places, and a bad read must only ever be a wrong noise.
- **The scan's sound-mode heuristic skips the SelectSong routine's own 48 bytes.** Its word at +16
  (`c9 18 89 00`) passes every field test, and was found every time before the exclusion.
- **`scan.c` and `tools/m4a_scan.py` are line-for-line mirrors.** Fix both together.

## Clock (`clock.c`)

- Timing is in Timer 2 counts (16384 Hz, prescaler 1024, the FM card's control clock).
- **EXT:** the first tick plays on the pulse. The rest are spread over the smoothed period. The
  next pulse **flushes** any ticks still owed, so playback never drifts and never runs ahead. A
  change of more than 25 % in period is taken at once, not smoothed.
- **A reset within 10 ms after a pulse belongs to that pulse**, so the restarted song's tick 0 lands
  on it. Mod processes reset edges before clock edges in the same update.
- `clock_service()` keeps its own timestamp (`s_svcLast`). `clock_pulse()` also advances the shared
  time base, and sharing one "last" made envelopes and INT tempo run slow whenever a clock was
  patched (caught in review before the first build).
- INT accumulator headroom: `dt` is capped at 2048 counts, so `dt × tempoI × 408` stays under 2^31
  for tempoI ≤ 2040. `mod.c` caps tempoU at ×4 to keep it so.

## Song tables and SFX (`scan.c`, added 2026-10-26 after `milestone-1`)

- **The signature pass collects EVERY SelectSong match**, because a cart can hold more than one
  library copy (a two-in-one). The heuristic keeps the longest 8 runs, and runs only when no
  signature matched. Tables with no playable song are dropped. `g_scan` is a macro for
  `g_tables[g_tableIx]`, so the older readers work unchanged.
- **SFX are songs on music player ≠ 0** (the entry's `ms` field), in the same table. SET > SONG
  LIST filters ALL/MUSIC/SFX, and `g_songPlayer[]` records each song's player.
- An effect with SONG END = REPEAT restarts as soon as it ends. **Every restart counts a beat and
  a loop**, so the pulse outputs fire per hit. That's deliberate, but it surprises on first sight.

## Modulator (`mod.c`)

- **Three ways in, summed:** MAP (Workshop sources × depth), BTN (the GBA's buttons in PERFORM
  mode: HOLD/LATCH add `2047 × amount` like a full-scale source, STEP edits the BASE value, an EDGE
  destination fires), and BASE (each destination's resting value **in its own units**: programs,
  bytes, semitones, cents, tracks; ranges in `s_baseMin/Max`). Bypass zeroes all three.
- **PERFORM mode turns off auto-repeat.** A held button is a HOLD, and a repeat would turn one STEP
  press into a stream. `modtest` checks this.
- `tools/pcplay/modtest` drives `mod_update()` with stubbed link inputs and simulated keys
  (`g_hostIo[0x130]`, active low). 22 checks, all passing 2026-10-26. Run it after touching
  `mod.c`.

- The FM card's division of labour: raw inputs arrive, and all meaning is in `g_patch.mod[]`.
- Destination kinds: **EDGE** (clock/reset/next), **POS** (only the positive half acts, so an
  unpatched input or a centred knob never mangles anything), **BIP**. The POS choice was made so the
  factory map is inert at rest: MAIN → LAYERS strips tracks only when turned *up*.
- Transpose is 1V/oct at **depth 63**: a semitone is 1792 in v×depth units.
- The follower drops the gate for 3 ms on a new note while the gate is high, so legato notes
  retrigger downstream envelopes.
- **Patch magic `'MP'` (0x4D50), `PATCH_VERSION 2`.** The FM card's rules apply: bump the version
  for any change of layout *or* meaning, and prefer appending + migrating. v2 APPENDED table, list,
  BASE and BTN; `mod_patch_accept()` migrates a zero-padded v1 by giving it the factory buttons.
  The scene (table, list, song, mutes) restores only when the game code matches. Patch is 82 bytes,
  and a `_Static_assert` guards the 256-byte slot.
- **A loaded patch is copied out of `g_patchBuf` before validation.** The buffer has no alignment
  guarantee, and the ARM7 rotates a misaligned halfword read rather than faulting.

## Firmware

- The FM firmware plus `GBA_CAP_UP_PULSE` and `GBA_UP_PULSE` (0x6074). Audio Out 1/2 give a 5 ms
  pulse per counter increment, and a jump of more than 4 is a resync, not a burst. Unknown upstream
  tags count as `rxBad`, so an older host would treat PULSE words as bad (harmless, but noted).
- **The built firmware goes in as `UF2/gba_link.uf2`.** Rebuild and re-copy it whenever the payload
  or the firmware changes.
- **NEVER COPY A `build/` DIRECTORY BETWEEN TREES** (see the FM notes; it compiles the other tree's
  sources).

## Testing (`tools/`)

- `m4a_scan.py ROM` mirrors the scan.
- `pcplay/build.sh` builds the payload's engine for the PC under `-DHOST`, with zig's clang
  (`pip install ziglang`; this machine has no other native compiler). Every cart read goes through
  `host_rom_ptr`, which reports anything outside the image.
- `pcplay/mktestrom.py` builds a synthetic MP2K cart: a saw melody (A4/C5/E5 at "120 BPM"), drums,
  key split, a PSG voice, and 24 junk songs for fuzzing.
- Verified 2026-10-26:
  - pitch within 0.3 %;
  - transpose ±;
  - tempo ×0.5;
  - loop count every 48 ticks;
  - 191 ticks in 4 s at 120 BPM (m4a's true rate is 47.78 ticks/s, because 59.7275/150);
  - scrub and its wrap;
  - mute;
  - fuzz: 27 songs × 6 s with random mangles, **0 out-of-bounds reads**.
- **pcplay does not render the PSG.** A silent result from a PSG voice is expected.
- When measuring pitch by autocorrelation, take the first strong peak. Multiples of the period
  correlate as well, and an 880 Hz note was once "measured" as 293 Hz.

## Payload facts

- 51.6 KB, about 7.8 s to upload. IWRAM holds about 5 KB: the DMA handlers, the dispatcher,
  `mixer_render`, the scan loop, the rings, the echo line and the accumulators.
- Title `GBACARTPLAY`, code `CGMP`.
- `gfx_idle()` runs the link, `mod_update()` and `clock_service()`, but only once the engine is up.
  During the boot scan it services only the link.
