# Integration handoff

No existing application files have been edited. Integration is an explicit future coding task;
including this directory in a checkout changes neither the firmware nor its default playback.

## Files

| File | Responsibility |
|---|---|
| `krawall.h/.c` | ROM validation, candidate scan, sequencer, tracker state, mangle controls |
| `pcm_mixer.h/.c` | Separate unsigned PCM renderer, 12 voices, atomic start/generation protocol |
| `mangle_adapter.h/.c` | Binds voice callbacks; maps current PlayerMods and Track follower types |
| `integration_example.h/.c` | Compilable opt-in facade; defines no `player_*` or Sappy globals |
| `krawplay.c` | Host-only scanner and WAV renderer; exclude from firmware |
| `tests/` | Synthetic data and safety tests; exclude from firmware |

## Initialization and ownership

Call initialization from the main loop with audio stopped. `KrPlayer` owns all its state and
requires no heap. `KrRom.data` must remain valid for the entire playback: on GBA use a byte pointer
to `ROM_BASE`, `.base = ROM_BASE`, and `.size = g_romSize` after the existing cartridge detection.
On host, use a loaded ROM buffer. All song/table/pattern/sample addresses remain GBA bus addresses.
The format selector and explicit bank counts are mandatory. Each song may have a different bank.

```c
#include "engines/krawall/integration_example.h"

KrRom rom = { ROM_PTR(ROM_BASE), ROM_BASE, g_romSize };
KrSong song = { module_address, sample_table, instrument_table,
                sample_count, instrument_count, KR_MODERN };
if (!kr_example_start(rom, &song)) { /* show invalid configuration */ }
```

The application owns link decoding, UI, cartridge wait states, timers, DMA and IRQ dispatch.
The player does not read SPI or alter those registers. Apply a completed CV/control snapshot from
the main loop, not the serial ISR. Only call the renderer in the audio path. If switching engines,
stop both renderers before replacing the selected pointer/context; do not reinitialize while the
DMA interrupt can render it. `kr_pcm_start` protects its multi-field update with the same short
IME window used by the existing mixer; the renderer owns positions/generation completion.

## Clock and transport mapping

| Existing role | Krawall hook |
|---|---|
| One sequencer tick | `kr_tick(player)` / `kr_example_tick()` |
| Live main-loop update | `kr_mangle_sync(player, &g_mods, g_tracks)` |
| Internal tick scheduling | `kr_tick_hz_q16(player)`; integer/fraction accumulator supplied by caller |
| DMA block render | `kr_pcm_render(outL, outR)` when Krawall is selected |
| Start/select song | `kr_start(player, &song)` with native module and banks |
| Stop/running | `kr_stop(player)` / `player->running` |
| Song reset | `kr_restart(player)` |
| Section/order navigation | `kr_seek(player, order, row)` |
| Playback end/repeat | `player->repeat` (0 stops, 1 restarts at module songRestart) |
| Beat/loop/tick outputs | `player->beats`, `player->loops`, `player->ticks` |
| Per-track CV/gate/activity | `player->tracks[n].note/gate/notes/activity` or bridge followers |

At BPM 125 the native rate is 50 tracker ticks/sec; six ticks per row, normally 24 per quarter
note. An external 24-PPQN clock can drive `kr_tick` directly. Speed commands change ticks per row,
not ticks per second. The existing internal clock's m4a tempo formula is slightly different:
use the explicit native tick-rate getter in a future engine dispatcher rather than assuming
`player_tempo_i()` has the same units. No frame call is required: native tracker envelopes advance
on tracker ticks; PCM playback continues when the clock stops. Activity currently decays per tick.

`kr_seek` releases existing voices and does not reconstruct earlier effect/instrument history.
`kr_restart` resets tracks/tempo while keeping controls and repeat policy. A true bar snapshot,
intro-skipping loop restart, queued/quantized restart and hold-bar policy belong in the future
engine dispatcher; don't map them to seek and claim equivalent Sappy behaviour.

## Controls and callbacks

The bridge returns a bitmask when controls cannot yet be mapped. Do not discard it silently: disable
unsupported UI destinations or show the limitation. Native controls are ordinary main-loop fields;
copy a consistent snapshot between ticks if another execution context produces them.

| PlayerMods input | Implemented mapping |
|---|---|
| mute | Post-envelope silence; sample position and sequencing continue; bridge covers tracks 0–15 |
| pitch | Live Q8 semitone transpose |
| tempoU | Q8 multiplier used by native internal-rate getter; caller must schedule from it |
| voiceShift | Add to 1-based pattern instrument/sample selection; out-of-range notes drop |
| tableOffset | Byte offset applied to pointer-table lookup; distinct from MP2K's 12-byte tones |
| sampleStart / sampleEnd | Fractions /256 of source sample; safe trim/start clamp |
| reverseChance / dropChance | Per-note random decision; followers still count dropped notes |
| romScrub | Signed sample address offset; invalid ranges drop rather than wrap |
| reverb | Positive value routes to new renderer's echo; no native song reverb subtraction |
| attack/decay/sustain/release | Not mapped: native envelopes differ; bridge returns ENVELOPE bit |
| vibrato/tremolo/lfoSpeed | Not mapped as performance LFOs; LFO bit when depth nonzero |
| gate | Not mapped; GATE bit |
| chaos | Not mapped; CHAOS bit |
| scrubLen/raw fallback | No raw voice fallback; RAW bit for nondefault scrubLen/tableOffset |

Output-stage crush can use `g_kr_hold`/`g_kr_mask`; gain uses `g_kr_gain`. These are separate from
the Sappy output controls. To support all 32-track mutes use the native `mods.mute` field directly.
Do not let the 16-track bridge overwrite its upper bits if maintaining a separate UI for them.

For custom integration replace any `KrHooks` callback. `start/update/stop/done` let another renderer
or voice allocator drive physical voices; callback channel IDs are logical tracks 0–31. `note`
can change a proposed sample/loop/start/reverse/initial gain at note-on; its complete range and
step are checked before starting. `update` receives engine-computed live pitch/volume: apply custom
vibrato, envelope scale, chaos or routing there. Preserve its user/context dispatch when combining
custom callbacks with the supplied bridge. Note-hook pitch/gain edits are initial only and will be
recomputed by later updates. Don't retain pointers to callback arguments beyond the call.

`event` reports rows, loops, stops, marks, malformed data and unsupported effects. Callbacks execute
on the calling thread: keep them bounded, do not call start/seek/tick recursively, and don't render
or perform blocking UI/I/O work there. Inspect `error` and `unsupported[2]` after playback. Effects
outside 0–63 (including encoded volume-column commands) are available through the event callback.

## Future opt-in build change (not applied)

Add the three runtime `.c` files and optionally `integration_example.c` to an experimental payload
object list, then introduce an engine dispatcher at scan/start, clock, service, transport, follower
and audio-render boundaries. The current `payload/build.sh` compiles only files directly in
`payload/`, so these new sources are intentionally not picked up automatically. Keep both mixers
linked and choose exactly one renderer per block. Select no host CLI or test sources in firmware.

The default 12 physical voices cannot cover a 32-channel module. A later renderer/allocator can
steal/drop voices while keeping all logical track effects running; the current bridge simply
leaves channels 12–31 inaudible. Existing 16-track UI and save-slot records will also need an
explicit versioned extension if exposing all channels or caching native bank configuration.

## Separate audition firmware

`trial/` demonstrates the audio and scan/start wiring as a distinct firmware build. Its CMake
project copies the RP2040 entry point into its own build directory and supplies the experimental
payload there. Its GBA build reads the existing shared source files without editing them. See
[trial/README.md](trial/README.md). This is deliberately smaller than a production engine dispatcher:
it does not connect the normal modulator, UI or save-slot format.
