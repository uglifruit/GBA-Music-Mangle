# Other engines: how Krawall, GAX and MusyX would join the MP2K player

Status, 2026-10-29: **MusyX is in the main build** (steps 2 and 3 of section 8, and MusyX); Krawall
and GAX are not yet. This document says what the engines share, what has to happen when a cart
turns out to use one of them, and how the card stays one instrument with one UI whichever engine
is playing. "As built" below records where the build differs from the design.

## Real carts (2026-11-03)

Dumps of four of Andy's carts settled what the hardware reports could not:

- **MusyX (BattleBots, Denki Blocks): notes but no sound.** Each song group starts with 16 bytes, the
  starting program of each MIDI channel; tracks started on program 0, and these songs send no program
  changes, so every part played the first sound (on Denki, a PSG macro). Fixed in `ms_start`. Still
  silent: the parts written for the Game Boy channels (VOICE_ON / SETNOISE / HARDENVELOPE; the CAL
  row shows `PSG`). Keymaps (drum kits, multisampled instruments) now play: every one is reached
  through PLAYMACRO, whose key offset says where the map starts, and its entries transpose each note
  (see musyx.c, KEYMAPS). Harry Potter: Chamber of Secrets was hidden by reading the sample root key
  as two bytes; it is one.
- **Krawall (Lemony Snicket): not found.** The cart has no "Krawall" text, only the player's CVS line
  `$Id: player.c,v 1.4 2003/03/19 ... seb Exp`, now a signature when "seb" (the author) follows. Its
  20 modules then play. Discovery was also brought in line with UnkrawerterGBA (banks judged on
  their first four pointers; envelope counts of 255, junk sample-map bytes and wide pans accepted),
  a module is read in one format only, and the songs inside a module (split by 254) are listed.
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
  `engines/common/eng_mixer.c` is the one adapter, with a volume shift per engine (MusyX: 1, as
  its hardware-tested trial ran at half gain). The engines' `pcm_mixer.c` copies are not linked.
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
| MusyX | four `ms_tick()` | 96 PPQN native |
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
