# Krawall player proof of concept

Opt-in, allocation-free C engine for Krawall data stored in a GBA ROM. Everything here is new;
no existing source, build rule, generated payload or default engine was changed. It is not
selected by the normal build. See [INTEGRATION.md](INTEGRATION.md) for the application handoff.

The engine reads native Krawall module/pattern/sample/instrument structures directly, using
bounds-checked bus addresses. It does not run cartridge code, convert songs to MP2K, or require
Krawall's original player at runtime. The host tool and tests use the same sequencer as ARM.

## Run

```sh
cd engines/krawall
make test             # C compiler + AddressSanitizer/UndefinedBehaviorSanitizer
make                  # builds build/krawplay
make arm              # ARM7TDMI, -Werror, freestanding object/dependency check
build/krawplay scan your.gba
build/krawplay scan your.gba old
build/krawplay render your.gba 0x08MODULE 0x08SAMPLES SAMPLE_COUNT build/song.wav 10
# For instrument-based modules, append INSTRUMENT_TABLE INSTRUMENT_COUNT.
# To select pre-20040707 format, append 'old' after those two arguments
# (use 0 0 old for an old sample-based module).
```

Replace illustrative addresses with actual hexadecimal bus addresses, e.g. `0x08010000`.
WAV output is stereo 16-bit at 16384 Hz. A structural module scan does **not** discover or prove
its associated sample/instrument banks. Supply the correct banks and counts from your own dump;
UnkrawerterGBA's verbose output/explicit overrides can help. The player rejects invalid ranges.
Modern and pre-20040707 formats must be selected explicitly; this tool does not guess versions.
`.krw`/`.krb` relocated rip files are not accepted: this player uses original ROM addresses.

## Implemented

- Both packed-row layouts, extended instrument numbers in modern data, 32 logical tracks.
- Sample and instrument modes, 96-note instrument maps, forward sample loops, reverse/start/end
  mangles, signed fine tuning/relative notes, volume/pan envelopes, sustain/loop and release fade.
- External tracker ticks; speed/BPM, order jumps and pattern breaks, transport/seek/repeat.
- Basic volume/channel/global/pan slides, fine XM volume changes, arpeggio, sample offset,
  set volume/pan/global/channel volume, note cut and marker callbacks.
- Live mute/transpose/tempo, voice shift, table offset, drop/reverse probability and ROM scrub.
- Separate unsigned-PCM renderer derived from this repository's MIT mixer. Krawall PCM is
  unsigned; directly using the signed Sappy renderer would distort it. New symbols/state allow
  both implementations to coexist. No audio/serial registers are taken over by the sequencer.

## Deliberate proof-of-concept limits

This is **not** a complete or bit-exact replacement for the original Krawall player. Unsupported
tracker effects generate `KR_EVENT_UNSUPPORTED` and a diagnostic bitmap; notes still play.
Portamento, native vibrato/tremolo, retrigger, delayed notes, pattern loops/delays, volume-column
commands and fine S3M slide variants remain unimplemented. Basic slides do not yet reproduce all
XM/S3M flag-dependent quirks. Pitch uses equal temperament rather than Amiga period arithmetic;
automatic instrument vibrato and envelope-set-position are not implemented. The sample c2Freq
field is not a music pitch source (as in the reference player's normal note path).

The supplied renderer has 12 physical voices: channels 12–31 are sequenced but inaudible, with no
voice-stealing allocator. Its one-sample interpolation guard must lie within ROM; samples longer
than 512K are rejected. Scrubs outside the ROM are dropped, not wrapped or converted to raw voices.
Envelope advancement follows native tracker ticks, so envelopes pause with the external clock.
Hardware timing, real cartridge compatibility, and faithful playback of every game are unverified.

## Evidence and provenance

`make test` checks decoded notes, nonzero PCM, unsigned midpoint silence, modern/old layouts,
clocking, mute position continuity, transposition, order movement, effect diagnostics, envelopes,
invalid samples/channels/streams/callbacks, and 2,000 randomized input/control cases under ASan/UBSan.
`make arm` checks the core, bridge, renderer and integration facade with ARM GCC, links a combined
relocatable object without standard libraries, and fails if any unresolved symbols remain.

An additional local validation used upstream Krawerter to convert its `secondpm.s3m` example,
assembled the generated native data at `0x08000000`, found the resulting module, and rendered
10 seconds of nonzero PCM without decoder errors. Unsupported effects were reported, so this
establishes format interoperability and audio output, not full playback fidelity. The example
song, reference sources and generated audio are not added to this repository.

Format/behaviour references:
- https://github.com/sebknzl/krawall (`lib/mtypes.h`, player, converter). Upstream is LGPL-2.1+.
- https://github.com/MCJack123/UnkrawerterGBA (MIT); especially pre-20040707 packing details.

This implementation is independent C code under the repository's MIT licence; upstream runtime
code is not vendored. `pcm_mixer.c` is an explicitly named adaptation of this repository's MIT
`payload/mixer.c`, with separate symbols and unsigned sample decoding.

Reference snapshots inspected: Krawall `8d338581bf0ed0afad15b66bf5979ad4c4d15ed0`,
UnkrawerterGBA `999e310fcc62a0d21e783549051a06a2a3fbd848`.

## Workshop Computer trial UF2

A separate audition firmware is available with GBA-button transport and heuristic bank discovery:
[trial instructions and UF2](trial/README.md). It is an opt-in experimental artifact and does not
change the standard build. The trial's native discovery helper is also available on host through
`build/krawplay discover your.gba`.
