# GBA MusyX PCM proof of concept

An isolated native-data experiment and separate Workshop Computer UF2. All additions live under
`engines/musyx/`; no production sources, existing experiment, build script or embedded payload was
changed. This is a basic PCM player/sample browser, **not a complete MusyX implementation**.

[Trial UF2 and instructions](trial/README.md) · [Integration handoff](INTEGRATION.md)

## First useful check

Flash the separate UF2, boot the GBA in the usual multiboot mode, then press **RIGHT, A**. That
selects and plays a small built-in, authored MusyX-layout tune. The screen explicitly says
**BUILT-IN TEST DATA**. Hearing that tune validates the hardware path; it does not prove cartridge
compatibility. RIGHT switches back to cartridge discovery.

## Host build

```sh
cd engines/musyx
make test       # ASan/UBSan functional checks and 3,000 randomized cases
make            # builds the host scanner/player
make arm        # ARM7TDMI -Werror; combined freestanding object must have no unresolved symbols
make demo       # generates authored demo.gba and renders five seconds to build/demo.wav
build/musyxplay scan your.gba
build/musyxplay render your.gba 0x08012340 0 build/song.wav 10
build/musyxplay render your.gba 0x08012340 0 build/strict.wav 10 strict
build/musyxplay sample your.gba 0x08012340 0 build/sample.wav 3
```

Use an actual root address from the scan. Song/sample indices are zero-based. Inputs are ROM
images/native bus-addressed data, not MIDI, SF2 or relocated GSF files. Output is stereo 16-bit WAV
at 16384 Hz. The core itself uses no heap, floating point, libc or compiler division helpers.

## What works in the covered format

- Forward root-relative resource tables, song-group program/drum mappings, song-relative track and
  pattern pointers, sixteen musical tracks plus the seventeenth tempo/control track.
- Musical time at 96 PPQN, program changes, simultaneous notes, note durations, pattern selection,
  tempo changes and a limited track-loop implementation.
- Twelve polyphonic PCM voices with simple oldest-voice stealing, signed 8-bit samples, sample rate,
  root note, forward loops and start offsets. Negative/end-of-sample loop markers become one-shots.
- Bounded macro interpreter for the described subset: waits, pitch, sample starts, relative jumps,
  key/velocity/random splits, key-off traps, current-voice key-off, volume scaling and END/STOP.
- Live mute/transpose plus low-level voice/table shift, start/end trim, scrub and reverse/drop hooks.
- Independent sample audition that bypasses sequence and macro execution.

## Strict versus preview

**Strict** stops a voice at an unimplemented command. It is a supported-command subset, not a
claim that all implemented timing/flags have been verified against the original GBA driver.

**Preview** is the default trial/CLI mode. Known but unimplemented opcodes are skipped using their
validated serialized byte lengths. Unknown byte lengths are never guessed. END can leave a sample
ringing until its note-off; PLAYMACRO becomes an approximate same-voice tail selection with C4-centred
pitch adjustment; keymaps use only their sample/pitch fields. These approximations are reported in
opcode/warning diagnostics. They can produce recognizable but incorrect music. PSG-only macros may
remain silent. The authored demo uses the implemented subset and also plays in strict mode.

## Important limits

The GBA layout here follows BinarySerializer's documented variant. Other MusyX versions/packing,
backward-relative pointer layouts and SFX-only resource roots are not supported by discovery.
At most 32 roots, 256 songs per root, and 1024 macros/samples per table are considered. Table counts
use the first-data-pointer layout heuristic from the reference, so some legitimate variants may
be missed. Structural candidates are not proof of compatibility.

PSG synthesis (VOICE_ON/SETNOISE/HARDENVELOPE), native ADSR/DLS envelopes, vibrato, many flags and
other macro commands are absent. They are reported and either stop the voice or are skipped in
preview. Many games rely on them. PCM starts/volume are centered mono duplicated to stereo; original
priority/per-macro voice limits are not reproduced. Samples over 512K or missing an in-ROM guard
byte are rejected. Invalid scrub/selection drops a voice rather than playing arbitrary memory.

WAIT_TICKS with flags is approximated as musical ticks in preview. WAIT 65535 waits until note-off;
zero-duration notes hold until another macro/stop action. Loop metadata with nonzero StartLoopTime
is not reproduced: it warns and restarts the target entry immediately. Tempo/control track handling
is basic; native controller events and exact synchronization across differently looping tracks need
real-data verification. No game code is executed to fill any of these gaps.

The synthetic checks demonstrate this implementation's subset, not commercial-game fidelity. No
physical GBA/Workshop Computer or commercial MusyX ROM was available for validation.

## Diagnostics

`unsupported[0..1]` records opcode IDs 0–63. Bit 54 denotes approximate keymaps, bit 62 an opcode
outside the bitmap, and bit 63 a macro command-budget stop. `last_opcode` preserves the actual last
unsupported value (UINT32_MAX means none). `warnings` bits are:

| Bit/value | Meaning |
|---|---|
| 0 / 1 | Approximate nonzero track-loop time |
| 1 / 2 | Preview kept sample alive at macro END |
| 2 / 4 | Approximate PLAYMACRO tail selection |
| 3 / 8 | Voice stolen due to the 12-voice cap |
| 4 / 16 | Invalid/unsupported individual voice selection or range |

`rejected` counts rejected voices; `error` marks a fatal malformed sequence. Event callbacks carry
bad addresses and unsupported commands. Individual voice failures need not stop the entire song.

## Evidence and references

Host tests cover root discovery, chords, 96-PPQN/BPM changes, durations and loops, signed PCM,
mute-position continuity, pitch, strict/preview differences, traps, self-jump limits, sample browsing,
invalid data/callbacks and 3,000 mutated macro/song/control cases under ASan/UBSan. Five seconds of
the authored fixture rendered six loops, nonzero PCM, zero unsupported opcodes/warnings/rejections
and zero errors in strict mode. Macro byte lengths also match all 54 external parser definitions.

- [MusyXBoy](https://github.com/byvar/musyxboy), MIT, byvar, snapshot
  `4fd946a8fe15f2e78ba095ecdac0b236b57f65fb`: GBA scanner/converter and 96-PPQN sequence context.
- [BinarySerializer.Audio.GBA](https://github.com/BinarySerializer/BinarySerializer.Audio.GBA/tree/main/src/MusyX),
  MIT, BinarySerializer, snapshot `ed612685bc78e894a5cc6ab86da3cb49637fa94e`: GBA resource, song,
  sample, keymap and SMaL field layouts. Many fields/semantics are explicitly incomplete there.

Optional independent layout check:
`python tests/check_reference.py /path/to/BinarySerializer.Audio.GBA`.

New code is under the repository's MIT licence. The PCM renderer is a namespaced copy of the
repository's signed PCM mixer. No proprietary SDK/driver code, commercial songs or samples are
bundled. The demo generator authors its own notes/waveform; `demo_data.h` embeds those same bytes.
