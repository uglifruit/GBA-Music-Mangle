# Experimental MusyX PCM trial UF2

[Download UF2](artifacts/musyx-trial.uf2) · [SHA-256](artifacts/SHA256SUMS) · [Scope/limitations](../README.md)

A new, separate Workshop Computer firmware experiment. No pre-existing tracked file, original
embedded payload, normal build, or Krawall/GAX experiment is changed. No production integration is
implied. This is a limited PCM song player plus a sample browser and built-in hardware test tune.

## First test: no compatible cartridge needed

1. Flash **the Workshop Computer's RP2040** with this UF2 using its usual USB BOOTSEL procedure.
2. Connect the usual GBA link cable. If a cart is inserted, power the GBA on holding
   **START + SELECT** so it waits for multiboot rather than starting the cart.
3. Wait for the MusyX trial screen. Press **RIGHT**, then **A**.
4. The display must say **BUILT-IN TEST DATA**. Listen for the authored two-chord test tune at the
   GBA headphone socket. It uses the same decoder, macro subset and PCM renderer as cart mode.

This is a proposed hardware test, not a test already performed in this environment. Hearing the
demo would validate the cable/boot/audio path, not any commercial MusyX cartridge.

## Cartridge experiment

RIGHT switches back to the inserted cart and scans for plausible resource roots. The screen starts
stopped. Choose a root with Up/Down, choose a song with L/R and press A. If it is silent or clearly
wrong, use SELECT to switch to the sample browser, choose a sample with L/R and press A. This bypasses
unknown instrument macros and can help distinguish sample discovery from macro/sequencer problems.

| GBA button | Action |
|---|---|
| A | Start/restart selected song or audition sample |
| L / R | Previous/next item, zero-based index; stops until A |
| Up / Down | Next/previous resource-root candidate; resets item index |
| SELECT | Toggle song player/sample browser; stops until A |
| LEFT | Toggle strict/preview macro mode; stops until A |
| RIGHT | Toggle built-in demo/cartridge source; cartridge mode rescans |
| B | Mute/unmute all PCM output |
| START | Pause/resume song ticks; in sample browser, stop sample |

A looping sample can keep sounding while song ticks are paused. Other changes stop current voices.
Sample playback respects B/mute. Source changes clear the mute mask. The built-in sample bank has
sample 0 (silence) and sample 1 (the test waveform); selecting silent sample 0 is expected silence.

The screen shows the root address, item index/count, BPM/tick, unsupported opcode bitmaps and warning
bits. WARN / ERROR bit 31 marks a fatal decoder stop/rejected song start. See the engine README for
warning definitions and opcode limits. Record these values and the cart title/code for diagnosis.

## Preview and compatibility

The default is **PREVIEW (APPROXIMATE)**. It can skip unimplemented commands only when their byte
length is known, retain sample tails, approximate macro chaining and read simple keymap sample/pitch
fields. The unsupported/warning masks expose these shortcuts. LEFT selects **STRICT MACRO SUBSET**,
which stops a voice on unsupported commands. Strict does not mean verified native timing/fidelity.

Real-cart playback is unverified. The supported forward-relative GBA format follows the cited
BinarySerializer variant; other versions or SFX-only roots can be missed. Discovery is heuristic,
limited to 32 roots. Finding a root/song does not establish musical compatibility.

PSG macros, native ADSR/DLS envelopes, vibrato and many flags/opcodes remain unimplemented. They can
make real songs incomplete, incorrect or silent. Twelve PCM voices, simple stealing, approximate
loop timing and basic note-off behavior apply. Original game initialization/fades/controller scripts
are not run. Some songs may need data/behavior not yet understood. No copyrighted game data or original
driver binary is included; the demo consists only of authored test data.

This UF2 uses internal timing and GBA buttons. Normal CV/knob maps, external clock, patch slots and
production UI/save/load are not connected. Track-zero note/gate and coarse beat/loop replies use the
existing protocol but are untested on hardware. Flash an ordinary Music Mangle UF2 to return to normal
firmware. The trial requests no patch saves and writes nothing to the cartridge.

## Rebuild and evidence

With ARM GCC, Python, CMake, Ninja and Pico SDK 2.2.0 available:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
bash engines/musyx/trial/build.sh
```

Output: `engines/musyx/build/musyx-trial.uf2`. All intermediates stay in the new engine's build
folder. Existing startup/IRQ/link/cart/graphics/audio sources are compiled read-only. Only this
compilation redirects audio init/render to `ms_pcm_*`. A build-directory copy of the RP2040 main
includes the experimental header; the original `gba_payload.h` is never overwritten.

Completed checks: host functional tests, 3,000 randomized cases under ASan/UBSan, ARM7TDMI -Werror,
freestanding link without libc/libgcc helpers, all 54 command lengths against the external parser,
strict demo WAV output, GBA header/logo/checksum, embedded payload identity, UF2 framing/RP2040
family/address and memory limits. The packaged artifact is a deliberate copy of a completed build.

Not completed: physical flashing, multiboot/cable/audio timing, commercial-cart compatibility or
faithful MusyX reproduction. The built-in demo and diagnostic browser are intended to support those
next hardware experiments.
