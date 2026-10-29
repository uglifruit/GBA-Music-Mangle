# Experimental Workshop Computer UF2: Krawall audition

This is a **trial**, not a replacement release of Music Mangle. It has its own GBA entry point,
UI, scanner and unsigned PCM renderer. Existing tracked files, the standard embedded payload,
the normal build and the default branch are not changed.

## Try it

1. Put the Workshop Computer's RP2040 into its usual USB BOOTSEL/UF2 mode and copy
   [artifacts/krawall-trial.uf2](artifacts/krawall-trial.uf2) to it.
2. Connect the usual link cable and insert an original/reproduction GBA cartridge. Power on the
   GBA holding **START + SELECT** so it waits for multiboot with the cart still accessible.
3. Wait for **KRAWALL TRIAL** and the scan to finish. Sound does not start automatically.
4. Press **A** to audition the selected candidate. Listen at the GBA headphone output.

Flashing the trial changes the firmware installed on the Workshop Computer. To return, flash
an ordinary Music Mangle UF2. No cartridge writes or patch saves are performed by this trial.
The source and generated payload in the existing project have not been overwritten.

## Buttons

| GBA button | Action |
|---|---|
| A | Start/restart selected module and bank candidates |
| START | Pause/resume sequencer ticks (a held looping sample may continue sounding) |
| B | Mute/unmute output; sequencing continues |
| L / R | Previous/next module candidate; stops playback until A |
| Up / Down | Next/previous sample-bank candidate; stops playback until A |
| Right / Left | Next/previous instrument-bank candidate; stops playback until A |
| SELECT | Stop and rescan the inserted cartridge |

The screen shows cartridge title, module/sample/instrument bank addresses, selected format
(`MODERN`/`2003`), order/row and unsupported-effect masks. In FX HIGH / ERR the top bit indicates
invalid configuration or malformed data. A module can appear twice when both packing layouts
look plausible: try the candidate with the other format. Scanning a large cart can take time.

This first build uses **internal song tempo** and GBA buttons only. Normal CV/knob maps, patch
slots, external-clock scheduling, save/load and the production UI are not connected. Beat/loop
and track-0 pitch/gate replies use the existing serial protocol, but their hardware outputs have
not been tested. PCM gain starts at half the standard renderer's gain.

## Choosing a cartridge / diagnosing silence

A cart must actually contain supported Krawall data. Merely being a GBA game does not establish
that; the production application's engine sniff or a dump inspected with UnkrawerterGBA can help.
No specific commercial title or region has been validated in this environment.

Discovery is heuristic. It scans for plausible native module headers and pointer tables with at
least **four** structurally valid samples/instruments. It retains up to 64 module/format candidates
and 16 candidates of each bank type. It does not prove which bank belongs to which song. Some
correct banks will be missed, and some false positives will pass. Cycle banks and formats with
the controls above. If nothing works, record the cart title/code, displayed addresses, format,
order/row, and error/effect masks. A future cart profile can specify the exact module and banks.

Short banks (<4 entries), modified/unsupported Krawall variants and relocated rip files are not
automatically handled. The proof-of-concept engine's effect and channel limits still apply:
see [../README.md](../README.md). Unknown effects are reported rather than emulated faithfully.
A tune that starts may still sound wrong due to these unsupported tracker features.

## Build without changing the application

With ARM GCC, Python, CMake, Ninja and Pico SDK 2.2.0 available:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
bash engines/krawall/trial/build.sh
```

The script compiles existing startup/IRQ/link/graphics/cart/audio sources **read-only** into
`engines/krawall/build/trial/`. For this compilation only, audio's renderer/init calls are
redirected to the Krawall renderer. A build-directory copy of the RP2040 main is compiled against
the new generated payload header. No original file, including `gba_payload.h`, is rewritten.
The Pico PIO header is generated through the separate trial CMake project.

Output: `engines/krawall/build/krawall-trial.uf2`. The committed artifact and SHA-256 in `artifacts/`
are copies of a completed build. Updating them is a deliberate packaging step, not automatic.

## Checks completed / still needed

- Freestanding ARM compilation/link, no unresolved libc/libgcc dependencies.
- Native module/sample/instrument discovery tests, modern and old packed-row tests, PCM/control
  tests and 2,000 randomized cases under ASan/UBSan.
- A native dataset created by upstream Krawerter from its included `secondpm.s3m` example:
  discovery found the module and 41-entry sample bank; the host renderer produced a 10-second
  stereo WAV without decoder errors. Unsupported tracker effects were reported.
- Multiboot header/logo/checksum, payload inclusion in RP2040 flash data, UF2 block/family/address
  checks, and GBA/RP2040 memory limits checked for this artifact.

**Not verified:** flashing, link-cable boot, scanning a commercial cart, correct song/bank association,
real-time CPU/audio timing or audible playback on a physical GBA. Those are the purpose of this trial.
