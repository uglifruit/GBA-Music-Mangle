# MusyX integration handoff

This experiment adds only `engines/musyx/`. It defines no `player_*` functions, no Sappy globals,
and no production engine dispatcher. The normal payload build intentionally picks up none of it.

## Components

| File | Role |
|---|---|
| `musyx.h/.c` | Bounded root/song/sample discovery, 96-PPQN sequence, polyphonic PCM macro subset |
| `macro_lengths.h` | Byte lengths of the 54 described GBA SMaL opcodes |
| `pcm_mixer.h/.c` | Separate signed PCM state/rendering (`ms_pcm_*`, `g_ms_*`) |
| `mixer_adapter.h/.c` | Atomic PCM callbacks for the twelve voice slots |
| `musyxplay.c` | Host-only scanner and WAV renderer |
| `demo_data.h`, `tests/make_demo.py` | Authored native-layout fixture embedded for hardware testing |
| `trial/` | Separate entry point/UI, explicit payload build and RP2040 UF2 project |

Link the first four runtime components explicitly in a future experimental firmware target.
Don't link the host main or the trial main into the production main. Both existing engines/mixers
can stay present; a later dispatcher must select one renderer for each audio block.

## Initialization

With audio stopped, call `ms_mixer_hooks`, `ms_init` and `ms_pcm_init`. An MsRom contains a persistent
byte pointer, its logical bus base, and its size. On GBA use `ROM_PTR(ROM_BASE)`, `ROM_BASE`, and the
size from the existing cart detector. On host use a buffer with the same logical base. The built-in
demo uses a different buffer with logical base 0x08000000, which is why core reads are mapped rather
than literal cartridge dereferences. Samples passed to the adapter use actual mapped buffer pointers.

`ms_scan` examines a bounded slice and writes root candidates to the caller's array. A header can
extend beyond a slice boundary but must fit in the complete ROM. `ms_catalogue` reads the root's
song/sample/macro counts without modifying a player. `ms_song_info` resolves an index to its group
and song; `ms_start` revalidates it using root/index instead of trusting caller-provided addresses.

The root uses forward root-relative offsets. Each song group's 16-byte prefix and two 128-entry
sound lists precede the song header. Track and pattern offsets are anchored to that inner song
header. Confusing these pointer bases is a likely cause of silent/broken ports.

## Runtime boundaries

| Role | Hook |
|---|---|
| Start song | `ms_song_info(..., &song)`, then `ms_start(player, &song)` |
| Stop | `ms_stop(player)` |
| Native time | `ms_tick(player)` advances exactly one 96-PPQN musical tick |
| Internal scheduling | Accumulate `ms_tick_hz_q16(player)` against elapsed time |
| Live controls | Modify `player->mods`, then `ms_update(player)`; no time advances |
| Audio block | `ms_pcm_render(outL, outR)` when this engine is selected |
| Sample browser | `ms_audition_sample(player, root, sample_index)`; no song or macro execution |
| Track followers | `tracks[n].note/gate/notes/activity` for n 0–15 |
| Status | `ticks`, `loops`, `bpm`, `running`, `error`, `warnings`, `unsupported`, `last_opcode` |

A 24-PPQN external clock requires four native ticks per pulse, distributed by the application.
Keep source tempo changes and the independent renderer alive; a stopped musical clock leaves
looping PCM held. The tick-rate getter clamps tempo scaling at 4x to keep its arithmetic bounded.
Macro WAIT timing is provisional; native real-time/envelope clocks are not fully implemented.
The trial therefore uses internal clocking rather than claiming production clock integration.

Voice slots differ from tracks: multiple notes on one track occupy separate PCM voices. A future
production allocator should account for native priority/max-voice rules, not reuse a tracker-style
one-channel-one-voice map. The basic allocator uses a free voice or steals the oldest. Global/track
loop reporting is approximate; per-track -2 loops are counted on track zero. A source song without
track zero can loop without incrementing that aggregate counter.

## Controls and callbacks

MsMods exposes a 16-bit track mute mask, Q8 semitone pitch, Q8 tempo multiplier, macro-ID shift,
byte offset into the macro pointer table, signed PCM scrub offset, fractional sample start/end,
and drop/reverse probabilities. Original ADSR/PSG controllers, the full production PlayerMods
structure, chaos and save-slot state are not connected. Map only these supported controls in a
future dispatcher. Output crush/gain/echo use `g_ms_hold`, `g_ms_mask`, `g_ms_gain`, `g_ms_reverb`.

MsHooks receives physical voice IDs 0–11. Custom start/update/stop/done callbacks may implement a
different renderer. `note` can alter a proposed initial sample; bounds/loop/step are checked again
afterwards. Future updates recompute gain/pitch, so persistent custom modulation belongs in an
update callback. Keep contexts alive, don't retain callback argument pointers, and don't recurse
into transport or block inside a callback. Error/unsupported/note/loop/stop events support tracing.

All musical/macro state is main-loop owned. Existing serial IRQ code should collect a control
snapshot; the main loop applies it between ticks. Rendering owns positions/completion generations.
Starts use the original short IME-off publication pattern. Stop/disable audio before reinitializing
or switching contexts/renderers. The sequencer executes no cartridge code and performs no I/O writes.

## Validation and next work

The tests/demo exercise a known subset of the documented layout. The external byte-length check
verifies operand sizes, not opcode behavior. Preview is deliberately approximate; strict only
means unsupported commands stop voices. Neither is a fidelity claim.

The next useful work is a real GBA MusyX dump/driver trace: confirm pointer/table variants, initial
MIDI setup, all wait flags/timing, loop-time semantics, macro branching/child voices, native envelopes,
PSG and unknown commands. Log command usage per song, implement the required paths, and compare
pitch/timing/audio against original playback before broadening compatibility claims. The trial
provides a sample browser and visible diagnostics for gathering that evidence.
