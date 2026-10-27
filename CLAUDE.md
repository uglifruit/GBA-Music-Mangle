# GBA Music Mangle — Claude working notes

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
- **Repo: `uglifruit/GBA-Music-Mangle`** (private, created 2026-10-26 as `GBA_CartPlayer`, renamed
  2026-10-27; GitHub redirects the old URL). Code comments still say "GBA Cart Player": the same
  card. No card number yet.
- **The player (`payload/player.c`, `cgb.c`, `cgb.h`) was written by an outside developer** against
  `docs/PLAYER_SPEC.md`, on branch `codex/mp2k-player`, merged 2026-10-26. It passed the spec's checks.
  **The spec is the contract**: `player.h` is its interface. Changing a hook's meaning means
  changing the spec too.
- **`info.yaml`** (added 2026-10-27, `draft: true`, version 0.1.0) follows FM's layout. It passes
  the monorepo validator from the local clone: `cd ..\Workshop_Computer && node
  tools/sitegen/src/validate/cli.js <path to this info.yaml>`. Keep its panel, knob and switch
  descriptions in step with the factory map (mod.h) and README.
- **First hardware report 2026-10-26:** START+SELECT multiboot with a cart inserted WORKS, and the
  scan found 416 songs on Pokémon Ruby (AXVE). Playback "didn't sound like songs": the list then
  started on ALL (Ruby's first ~250 are effects), and an unpatched CV1 reading a count below 0 shifted
  every voice by -1 (see Modulator). Both fixed the same day; not yet re-tested.
- No MP2K data on Go Go Beckham, Premier Manager, or Hoyle's: European third-party titles, which are
  likely on other engines (GAX, Krawall, ...). The engine sniff (below) will say which, if their
  engine leaves a signature.

## OPEN RISKS, in the order the first hardware session should settle them

1. ~~Multiboot with a cart inserted needs START+SELECT held at power-on.~~ **Settled 2026-10-26:
   it works.** Audio also comes out, so the DMA design is at least running.
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
- **The sound mode (the game's mixing rate) comes from m4aSoundInit's literal pool**, found by
  saptapper's route: m4aSoundMain is the PUSH {LR} ≤ 0x20 before SelectSong, and m4aSoundInit one of
  two prologues ≤ 0x100 before that. The last word in [init, main) that passes the field tests wins.
  It replaced (2026-10-27) "the first passing word in the 16 KB around SelectSong", which chance
  code/data words pass. A wrong rate plays every FIXED-RATE sample (most drum kits) at the wrong
  speed. Suspected cause of Ruby's percussion sounding like white noise; not yet confirmed on
  hardware (check CAL > RATE, which should read 13379 for Ruby). mktestrom's decoy word guards it.
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
- **The scan starts on the MUSIC list** whenever the table has any player-0 song (ALL otherwise).
  A saved scene from the same game still restores its own list.
- An effect with SONG END = REPEAT restarts as soon as it ends. **Every restart counts a beat and
  a loop**, so the pulse outputs fire per hit. That's deliberate, but it surprises on first sight.

## Other engines (`scan.c` sniff, added 2026-10-26)

- **The heuristic pass also looks for other engines' text**, so a cart with no MP2K says what it
  uses instead. Only signatures confirmed at a primary source are in: GAX ("GAX Sound Engine",
  "GAX ENGINE V"; TCRF game pages and loveemu's gaxtapper), Krawall ("Krawall": retail builds have
  "$Id: Krawall ...", and the open-sourced lib/general.c has "$Krawall v1.0.0"), and whole XM
  modules ("Extended Module: "). **Apex Audio System was checked and leaves no string.** Don't
  add an engine from memory.
- Every signature is ≥ 7 bytes, so one of its first four 4-byte windows is always word-aligned. The
  pass hashes each word it already reads into a 1024-bit filter, and does a full compare only on a
  hit. It costs a multiply and a bit test per word, and runs only when MP2K wasn't found.
- **The heuristic now also runs when the signature matched but every table it found was empty.**
  Before, that case reported NONE without trying the heuristic.
- `tools/pcplay/mkenginerom.py` builds a test cart with every signature at a different alignment,
  one ending on the image's last byte, plus a near miss. C and Python both report
  `GAX KRAWALL XM x2`.
- With no music, the result screen stays up until a button is pressed and released
  (`hold_result()` in main.c).
- Go Go Beckham, Premier Manager and Hoyle's all reported no MP2K on the first hardware session.
  Re-scan them with this build to name their engines.

## Booting from a saved slot (patch v4, 2026-10-27)

- **A save records the cart** (`cartTag` = `rom_cart_tag()`, plus `romVersion`/`romCheck` beside
  `game[4]`) **and its song tables** (address + rate, up to 8). At boot, `boot_from_slot()` (main.c)
  waits ≤ 0.75 s for every slot's tag, loads the LOWEST-numbered matching slot, checks the full
  identity (`mod_patch_is_this_cart`), and installs the tables with `scan_install()`, which drops any
  table that no longer holds a playable song. Anything short of that falls through to the real scan.
  B held skips it, and SET > RESCAN CART always scans (`scan_cart(0)`).
- **Why tags rather than loading every slot:** a load is a byte per link word, ~0.3 s a slot. The
  host announces each slot's tag (`GBA_OP_SLOT_TAG` 0x0A, every 16 words for the first 2048, then
  every 64), read from flash at `GBA_PATCH_TAG_OFFSET` (108). `mod.c` asserts `offsetof(Patch,
  cartTag)` against it: **that field must never move.** A CONTROL word has no check bits, so a tag
  only picks the slot; the identity check is what trusts it.
- The booted patch is applied after `mod_init()` and the first `mod_song_select(0)`, through
  `mod_patch_loaded()`, so the scene (table, list, song, mutes) comes back too. `mod_patch_loaded`
  now also sets `g_plGameRate` when it switches table (it didn't, unlike `mod_set_list`).
- Scan method `SCAN_SAVED` (3) marks installed tables; CAL shows SAVED SLOT.

## Modulator (`mod.c`)

- **Three ways in, summed:** MAP (Workshop sources × depth), BTN (the GBA's buttons in PERFORM
  mode: HOLD/LATCH add `2047 × amount` like a full-scale source, STEP edits the BASE value, an EDGE
  destination fires), and BASE (each destination's resting value **in its own units**: programs,
  bytes, semitones, cents, tracks; ranges in `s_baseMin/Max`). Bypass zeroes all three.
- **The knobs have a ±200-count detent at noon** (about 15°), with the rest of the travel stretched
  to full scale (`knob_value()`). The first hardware report had songs "weirdly slow" with X near
  noon: the tempo curve is an octave per 1024 counts, so 15° off noon was about 16 % slow.
- **A dead band of ±24 counts on every value source** (not PU2), not offset outside it, so 1V/oct
  stays exact. Quantised destinations **divide, never shift**: `dv >> 5` floors, and a CV1 reading
  of -1 gave a voice shift of -1 (every instrument swapped). This was the first hardware bug.
- **PERFORM mode turns off auto-repeat.** A held button is a HOLD, and a repeat would turn one STEP
  press into a stream. `modtest` checks this.
- `tools/pcplay/modtest` drives `mod_update()` with stubbed link inputs and simulated keys
  (`g_hostIo[0x130]`, active low). 98 checks, all passing 2026-10-27. Run it after touching
  `mod.c`.

- The FM card's division of labour: raw inputs arrive, and all meaning is in `g_patch.mod[]`.
  (The factory map's description here predates the performance set; mod.h has the current one.)
- Destination kinds: **EDGE** (clock/reset/next), **POS** (only the positive half acts, so an
  unpatched input or a centred knob never mangles anything), **BIP**. The POS choice was made so the
  factory map is inert at rest: MAIN → CHAOS does nothing in the noon detent (left is free chaos, right the repeating kind).
- Transpose is 1V/oct at **depth 63**: a semitone is 1792 in v×depth units.
- The follower drops the gate for 3 ms on a new note while the gate is high, so legato notes
  retrigger downstream envelopes.
- **Patch magic `'MP'` (0x4D50), `PATCH_VERSION 5`.** The FM card's rules apply: bump the version
  for any change of layout *or* meaning, and prefer appending + migrating. v2 APPENDED table, list,
  BASE and BTN; `mod_patch_accept()` migrates a zero-padded v1 by giving it the factory buttons.
  **v3 (2026-10-27) added ten destinations.** BASE couldn't grow in place (BTN follows it), so v2's
  array is `baseV2[16]` (migrated, then zeroed) and a full `base[DEST_COUNT]` is appended. The
  scene (table, list, song, mutes) restores only when the game code matches. v4 appended the cart
  and its tables (next section above). Patch is 164 bytes, and a `_Static_assert` guards the
  256-byte slot. (v5: 212 bytes.)
- **The sound destinations (v3):** ATTACK DECAY SUSTAIN RELEASE (live, through `pl_env()` in
  player.c, which cgb.c also reads through), NOTE LENGTH (at note-on), REVERB (added to the song
  header's), VIBRATO/TREMOLO/LFO SPEED (one global LFO in `player_frame()`, m4a's MODT scales).
  They are in PLAYER_SPEC §5.7's "added after delivery" table: the spec is still the contract.
- **CHAOS (v5)**: per-note dice in `note_on()` from a private xorshift, reseeded from
  `s_chaosSeed` at song start and on every counted loop, so it is a repeating pattern (measured:
  loop-to-loop correlation 0.80 with chaos 120, against 0.25 with no chaos). FOUR draws per note
  whatever happens, or the pattern drifts. REROLL CHAOS (EDGE) changes the seed. Dice and ranges
  are in PLAYER_SPEC §5.7. Watch the tripwire: the interval pick was a `%` by a variable at first.
- **RESTART LOOP / RESTART BAR (v5, EDGE):** `player_restart_loop()` sends each track to the loop
  point `find_loop()` read ahead at `player_start()` (first backward GOTO; forward ones followed,
  PATT stepped over; 0 if unsure, and then the song restarts). `player_restart_bar()` restores the
  copy of `g_tracks` (plus ticks, tempo, chaos dice) taken every `g_plBarTicks` at the top of
  `player_tick()`. Both release TIEd notes (gate 0 would hang without its EOT) and are followed by
  `clock_reset()` in mod.c, as RESET is. The section length is `Patch.barLenIx` (v5, in what was
  padding; older patches get one bar). The bar counter `s_barPos` is used instead of `g_plTicks %`
  because `g_plBarTicks` is a variable (tripwire). The fuzz test fires both at random.
- **Every value destination is BIP now** (2026-10-27): the one-sided ones got meanings below zero.
  START → END (`sampleEnd`, a tail cut), LAYERS → from the bottom, SOLO → MUTE one track,
  REVERSE → DROP (`dropChance`), CRUSH + rate only / − bits only (it used to be both on +),
  VIBRATO → square (trill), TREMOLO → square (gate), CHAOS → FREE (`chaosFree`, never reseeded).
  The factory MAIN → CHAOS is therefore free chaos left of noon. KIND_POS remains, unused.
- **Switch UP = REPEAT BAR** (`SWU_REPEAT`, the factory setting since 2026-10-27; BYPASS before):
  mod sets `g_plHoldBar` while up, and `player_tick()` restores the section's copy on every line
  that ends it, so the section playing loops. Older patches keep their own SWITCH UP.
- **SET > RESTARTS = AT BAR END** (`Patch.restartQ`, v5, padding before): RESET, RESTART LOOP and
  RESTART BAR (and switch-down RESET) go to `player_queue_restart()`, and `player_tick()` does
  them at the top of the first tick on the next section line (`s_barPos == 0`), with no
  `clock_reset()`: it is already on the grid. A new song clears the queue. PLAY shows QUEUED.
- **The factory map and buttons are a performance set** (2026-10-27, Andy asked for it): MAIN →
  CHAOS, AU2 → RESTART BAR; A RESTART BAR, B REROLL, L/R hold REVERSE/CRUSH, UP/DOWN TRANSPOSE,
  LEFT CLEAR, RIGHT step CHAOS. Layers, sample start and the voice-shift/table buttons left the
  defaults. modtest now sets up the mappings each test uses instead of relying on the factory.
- **Patch v5 moved BASE again, to `base[DEST_MAX = 48]` at the very end**: `cartTag` follows v3's
  array at the host's fixed offset, so it could not grow in place. There is room up to 48
  destinations now without another move (a `_Static_assert` says when not).
- **CLEAR MANGLES** (EDGE) zeroes BASE and the BTN latches, except TEMPO, TRANSPOSE and FINE PITCH
  (the user asked to keep tempo and transpose; fine pitch counts as tuning, so it stays too).
  Latches drop on the next update (the BTN sum is taken before the edges), BASE at once.
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
- **Patches live in the THIRD-FROM-LAST flash sector** (`patch_store.cpp`, 2026-10-27). 108 has the
  last and FM the second-to-last. A UF2 flash never erases them, and a save rewrites the whole
  sector, so sharing one let each program's saves wipe the other's patches. This tree was seeded
  before FM fixed that, and was writing 108's sector.
- **`slot_valid()` only asks "erased or not"** (FM's fix): the seed still checked for 108's `'GB'`
  magic, so every `'MP'` save showed as an empty slot. The GBA judges the patch (`mod_patch_accept`).

## Testing (`tools/`)

- `m4a_scan.py ROM` mirrors the scan, including the engine sniff.
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
- **pcplay does not render the PSG.** A silent result from a PSG voice is expected. cgb.c has
  therefore never been HEARD off hardware; it was re-checked line by line against pret's CgbSound
  and ply_note on 2026-10-27 (registers, noise table, drum key, envelope paths) and matches.
- When measuring pitch by autocorrelation, take the first strong peak. Multiples of the period
  correlate as well, and an 880 Hz note was once "measured" as 293 Hz.

## Payload facts

- 62.6 KB, about 9.5 s to upload. IWRAM holds about 11 KB: the DMA handlers, the dispatcher,
  `mixer_render`, the scan loop, the drawing primitives, `mod_update`, `clock_service`,
  `player_update`, the rings, the echo line and the accumulators.
- **Drawing speed (2026-10-27, FM's `SPEEDUPSCREEN.md`):** `rect`/`line`/`glyph`/`text` are FM's
  IWRAM versions (word-wide fills, direct VRAM text). The bigger win here was **rate-limiting the
  service**: `gfx_idle()` ran `mod_update()` + `clock_service()` after every character drawn,
  from EWRAM. They now run at most once per 16 Timer 2 counts (~1 ms), from IWRAM, with their
  per-call helpers forced inline. The CAL row SVC is the meter (wall time ÷ 1 ms, Timer 3). The
  external BPM's `udiv32` moved from the service to `clock_pulse()`. Not yet measured on hardware.
- Title `GBACARTPLAY`, code `CGMP`.
- `gfx_idle()` runs the link on every call, and `mod_update()` + `clock_service()` at most once a
  millisecond, but only once the engine is up. During the boot scan it services only the link.
