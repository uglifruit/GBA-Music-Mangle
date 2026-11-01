# GAX proof of concept — independent, opt-in experiment

A bounded native-data player for the **GAX 3.05-style** format, with a separate Workshop Computer
UF2 trial. This directory is new: no existing source, generated payload, build rules, Krawall
experiment or default firmware was changed. The normal build does not include these files.

[Download/use the trial](trial/README.md) · [Developer integration handoff](INTEGRATION.md)

This engine reads song headers, per-channel order lists, compressed patterns, instruments,
performance lists, envelopes and unsigned PCM from the inserted cartridge. It does not call
cartridge driver functions, patch the cartridge, or take over its original RAM/IRQ layout.

## Build and check on host

```sh
cd engines/gax
make test                         # ASan/UBSan, including 3,000 randomized input cases
make                              # host scanner/WAV renderer
make arm                          # ARM7TDMI -Werror, freestanding link, no unresolved symbols
build/gaxplay scan your.gba
build/gaxplay render your.gba 0x08012340 build/song.wav 10
# Use an actual song-header address printed by scan, not the illustrative address above.
```

The ROM buffer must remain alive while playing. Bus addresses retain their original `0x08xxxxxx`
values in both host and ARM execution. Output is 16-bit stereo WAV, 16384 Hz. Only ordinary ROM
images/native bus-addressed data are accepted; relocated GSF/`.gax` files are not interchangeable.
A structural header match is a candidate, not proof of engine version or musical compatibility.

## Implemented and bounded

- Header validation and incremental discovery (up to 32 channels, 256 rows, 1024 orders).
- Compressed note/effect rows, single/multiple rests and empty patterns; independent order streams
  per channel with signed order transposition. Instrument/sample banks come from the song header.
- Instrument wave slots (including holes), unsigned PCM, start/loop parameters, performance-list
  note/fixed-note changes, waveform changes, simple pitch/volume slides, set volume/speed and jumps.
- Multi-point linear volume envelopes, sustain and looping; release from sustain on note-off.
- Sequence speed, alternating speed, pitch/volume slides, set volume, pattern break and note-off.
- Live mute/transpose; voice shift, pointer-table byte offset, scrub, sample start/end, reverse/drop
  chance; follower counters and optional callback bridge for the existing public mod/follower types.
- Separate namespaced PCM state/renderer using the repository's atomic-start/generation design.
  Every sample range, loop and interpolation guard is checked before starting. RLE must consume
  a positive number of remaining rows. At most one performance event per channel per frame executes.

## Scope and limitations

This is **not a complete GAX implementation or a fidelity claim**. It targets the documented
3.05-style structures. GAX 1/2, earlier incompatible GAX 3 variants and modified drivers are not
supported. A version signature is displayed for diagnosis, not used as proof that a song works.

- Tone portamento, delayed notes/extended sequence effects, conditional/delayed performance jumps
  and native instrument vibrato are unimplemented and reported. Unknown command IDs are reported.
- Wavetable modulation synthesis is not implemented. Ping-pong loops play as ordinary forward loops.
  Both set visible feature bits rather than claiming an exact result.
- Envelope advancement is once per control frame; native sub-frame interpolation/timing, some end
  behaviours and scaling/rounding are approximations. Output is centered mono duplicated to stereo.
- The renderer has 12 physical voices. Higher logical channels are decoded but inaudible; no voice
  stealing is implemented. Samples over 512K and samples lacking an in-ROM interpolation guard drop.
- Source mixing rate is retained as metadata; output resamples to 16384 Hz. Equal-tempered pitch is
  an approximation of native table/rounding. Global effects (native reverb/filter/speech/SFX/jingles),
  game-controlled fades and the full production mangle feature set are not implemented.
- An out-of-range instrument, sample or scrub drops that voice. A malformed pattern stops playback.
  No original cartridge code is executed to fill gaps in supported features.

`unsupported[0]` reports sequence effect IDs 0–15, `[1]` performance effect IDs +32, and `[2]`
feature IDs +64: bit 0 ping-pong approximation, bit 1 unsupported modulation, bit 2 vibrato.
Callbacks also report malformed streams; `error` marks a fatal decoder stop.

## Validation and reproducibility

Tests exercise scanner boundaries, compressed patterns/RLE, note-off, waveform changes, holes in
wave slots, speed changes, loops, transposition, envelopes/sustain, PCM energy, unsigned-midpoint
silence, mute position continuity, mod/follower mapping and malformed ROM/callback ranges.
The randomized cases mutate patterns, instruments and controls under ASan/UBSan.

An independent format check uses the unmodified MIT Python serializer with **authored synthetic
notes and waveform**, not a commercial soundtrack:

```sh
python tests/make_reference_fixture.py /path/to/shinen-gax-python build/reference.gba
build/gaxplay scan build/reference.gba
# Use the address printed by the script/scan:
build/gaxplay render build/reference.gba 0x080009f0 build/reference.wav 5
```

At the referenced snapshot the fixture header is `0x080009f0`. Five seconds rendered nonzero PCM,
nine song loops, zero decoder errors and zero unsupported-feature reports. This demonstrates
format interoperability for the covered fixture; it is not validation against a commercial cart.
No physical GBA/Workshop Computer or commercial ROM was available for testing.

## References and attribution

- [loveemu's research note](https://gist.github.com/loveemu/9b3063ffd9a76cb18e379324e43f3251).
  Direct gist access was blocked in this execution environment; it was not presented as read.
- [Gaxtapper](https://github.com/loveemu/gaxtapper), snapshot
  `649be537bc0cc5590e01dad1e36a44c90c3bdff8`: header layout and limits, driver/version context.
  Gaxtapper is a GPL-3.0 GSF ripper using original cartridge driver calls, not a standalone replayer.
- [shinen-gax-python](https://github.com/beanieaxolotl/shinen-gax-python), MIT, copyright
  beanie_axolotl, snapshot `084522b75f2337e3e619c2945b4a4818ace56359`: compressed rows,
  instruments, performance lists, envelopes, waveform parameters and independent serializer.

This directory's new implementation is under the repository's MIT licence. No proprietary driver
binary, commercial song data or upstream runtime source is bundled. `pcm_mixer.c` is an explicitly
namespaced unsigned-PCM adaptation of the repository's MIT mixer; the existing mixer is unchanged.
