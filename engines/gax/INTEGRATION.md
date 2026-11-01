# GAX developer handoff

Only `engines/gax/` is added. Production integration is deliberately **not applied**. This is a
new runtime interface, not a replacement definition of the existing `player_*` functions.

## Components and build selection

| File | Responsibility |
|---|---|
| `gax.h/.c` | Bounded GAX 3.05-style scanner, sequencer, instruments/performance/envelopes |
| `pcm_mixer.h/.c` | Separate unsigned PCM renderer; `gx_pcm_*`, `g_gx_*` symbols/state |
| `mangle_adapter.h/.c` | Voice callbacks plus existing PlayerMods/Track type mapping |
| `gaxplay.c` | Host scanner/WAV tool; exclude from firmware |
| `tests/` | Host acceptance/fuzz/independent serializer fixture; exclude from firmware |
| `trial/` | Distinct GBA entry point/UI, RP2040 CMake build, verified UF2 artifact |

Compile the three runtime `.c` files in an opt-in experimental object list. The production
`payload/build.sh` sees none of them automatically. Both existing renderers can remain linked;
an eventual engine dispatcher must choose exactly one audio renderer per block. Don't link a
second `main` or copy definitions of Sappy globals to pretend this is already interchangeable.

## Minimal main-loop setup

```c
#include "engines/gax/mangle_adapter.h"
static GxPlayer player;

GxRom rom = { ROM_PTR(ROM_BASE), ROM_BASE, g_romSize };
GxHooks hooks;
gx_mixer_hooks(&hooks, &player);
gx_init(&player, rom, &hooks);
gx_pcm_init(); // audio stopped during initialization
if (!gx_start(&player, selected_song_header)) { /* show invalid/unsupported data */ }
```

ROM size detection and wait-state configuration remain application-owned. The ROM buffer and
player context must outlive playback. GAX header addresses point directly to songs; there is no
MP2K song-table-entry indirection and no separately guessed bank association. `gx_scan` accepts a
bounded slice and stores candidates up to the caller's capacity; full headers may extend past a
slice boundary but are always validated against the complete ROM.

## Timing and application dispatch points

| Role | Hook |
|---|---|
| Start/select | `gx_start(player, song_header)` |
| Stop/running | `gx_stop(player)` / `player->running` |
| Advance time | `gx_frame(player)`: one native GAX control frame |
| Internal rate | `gx_frame_hz_q16(player)`, nominally 59.7275 Hz times `mods.tempo / 256` |
| Main-loop controls | `gx_mangle_sync(player, &g_mods, g_tracks)`; inspect its result |
| DMA render | `gx_pcm_render(outL, outR)` only when the GAX engine is selected |
| Restart | `gx_restart(player)` |
| Navigate | `gx_seek(player, order, row)` |
| Song end | `player->repeat`; 0 stops, 1 jumps to header restart order |
| UI/CV followers | `tracks[n].note/gate/notes/activity` or bridge's Track output |
| Diagnostics | `error`, `unsupported[3]`, and event callbacks |

**A GAX frame is not a 24-PPQN tick.** Native performance lists/envelopes run per GBA frame and
sequence rows run every speed frames. The 7xy command alternates row speeds. A future external
clock mode must specify how musical pulses schedule frames/rows; do not feed 24-PPQN pulses into
`gx_frame` and label the result equivalent to Sappy clocking. The trial uses native internal time.
A separate production `player_frame` call must not advance this engine a second time.

Seek decodes prior rows to locate the stream but does not replay their effects/instrument history.
It silences voices and preserves remaining effect history; it is not a complete bar snapshot.
Restart resets track/tempo state but preserves controls and repeat policy. Quantized restarts,
loop/bar snapshots, hold-bar and musically meaningful beat pulses require explicit dispatcher work.
The trial sends track pitch/gate but does not fabricate beat pulses from frame counts.

## Controls, voices and ISR ownership

The main loop owns GAX state. Existing serial/ADC handlers should produce a consistent control
snapshot, then the main loop applies it between frames. The engine never reads SPI itself.
The renderer owns sample positions and completion generations; its starts use the existing short
IME-off atomic update pattern. Init/reset are not ISR-safe. Stop/disable audio before reinitializing
or changing renderer/context; continue servicing the serial link while scanning/drawing.

The bridge maps mute, Q8 pitch, tempo multiplier, voice shift, byte offset into the **instrument
pointer table**, sample start/end, reverse/drop probability and scrub. Out-of-range selections drop;
there is no raw-ROM fallback/wrap. `scrubLen` is unmapped. Reverb goes to the new renderer's positive
echo setting, not GAX's original effect. Crush/gain use `g_gx_hold`, `g_gx_mask`, `g_gx_gain`.

The returned `GX_UNMAPPED_*` mask identifies nonzero ADSR overrides, performance vibrato/tremolo,
chaos, gate scaling and nondefault raw length. Disable those UI routes or show their limitation.
Don't silently claim the full production modulator works. The 16-track public bridge cannot expose
all 32 native track mutes; use native `mods.mute` and a larger UI when supporting them.

Custom `GxHooks` can replace start/update/stop/done for a renderer/allocator. Channel IDs are logical
tracks. `note` can alter a proposed initial sample/loop/start/reverse; the complete range and step
are checked afterwards. Gain/pitch overrides there are initial only; future updates recompute them.
A custom `update` callback can apply performance LFOs/live gain/routing. Keep callback user/context
ownership consistent when combining custom hooks with the supplied mixer bridge.

Callbacks execute on the caller's thread: keep them bounded, don't block or recurse into transport,
and don't retain argument pointers past the call. Events report rows, loops, stops, unsupported
features/effects and fatal bad pattern data. Invalid individual instruments/waves drop safely;
not every rejected note is a fatal error. The default bridge has 12 physical voices and drops
higher channels; a later allocator can steal voices while retaining logical channel state.

## Verification before production integration

The provided host/ARM tests and format fixture cover the implemented subset. Hardware clock/audio
load, cartridge revision compatibility, accurate synthesis of real GAX instruments and complete
production UI/save-slot/engine selection remain separate work. The trial's isolated build is an
example of audio/link reuse, not evidence those production integration tasks have been completed.
