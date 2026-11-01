# GAX 3.05-style audition UF2 — experimental

[Trial UF2](artifacts/gax-trial.uf2) · [SHA-256](artifacts/SHA256SUMS) · [Engine limitations](../README.md)

All files live in the new `engines/gax/` folder. The ordinary firmware, its embedded GBA payload,
existing Krawall experiment and all pre-existing tracked files remain unchanged. This UF2 is an
opt-in hardware experiment, not a production Music Mangle update.

## Try it

1. Put the **Workshop Computer** RP2040 into its usual USB BOOTSEL mode and copy the trial UF2
   onto it. The UF2 goes on the Workshop Computer, not the GBA.
2. Connect the usual link cable, insert a suitable cartridge, and power the GBA on holding
   **START + SELECT** so it waits for multiboot while keeping the cartridge readable.
3. Wait for **GAX TRIAL** and the scan to finish. It starts stopped.
4. Press **A** to audition a candidate. Listen at the GBA headphone output.

Flashing installs trial firmware on the Workshop Computer. Flash an ordinary Music Mangle UF2 to
return. The trial performs no cartridge writes or patch saves. As with the original project,
flash carts may expose their menu ROM rather than the intended game after this boot sequence.

## GBA buttons

| Button | Action |
|---|---|
| A | Start/restart the selected candidate |
| L / R | Previous/next song candidate; stops playback until A |
| START | Pause/resume control frames; a looping sample may continue sounding |
| Up / Down | Choose the track to follow/mute (zero-based track index on screen) |
| B | Toggle mute on that track; sequencing continues |
| Left / Right | Transpose all voices down/up one semitone, limited to ±24 |
| SELECT | Stop and rescan |

The screen shows a discovered GAX signature if present, song-header address, order/row, track,
candidate count and three unsupported-feature masks. FEATURES / ERROR bit 31 means a fatal decoder
stop or rejected song start. Capture those values and the cartridge title/code when reporting a
problem. GAX frames aren't musical beats, so the trial sends no artificial beat pulses.

## Which cartridges?

This implementation targets **3.05-style GAX data**, not every game with a GAX signature. No
particular commercial cartridge/region/revision has been validated here. The signature is useful
context but does not establish layout compatibility. GAX 1/2 and incompatible earlier/modified GAX
3 layouts are outside this trial. A cart can be detected as GAX by the normal application and
still not work in this experiment.

The structural scanner retains at most 64 song candidates. It validates bounded channel/order/
pattern and bank pointers, but false positives are possible. Unlike Krawall, supported GAX headers
supply their own instrument and sample banks, so there is no separate bank-cycling UI. A zero count
means no supported header candidates were found, not proof that the cart contains no GAX data.

Some real instruments rely on unsupported synthesis. Wavetable modulation is absent, ping-pong
loops are approximated as forward loops, and native vibrato is absent; the mask reports these.
Tone portamento, delayed/extended commands and conditional performance jumps are also incomplete.
Only 12 physical voices sound. A track can start yet sound wrong or be incomplete. A static zero
song volume remains silent; game-driven fades/volume scripts are not run by this player.

This first UF2 uses native internal frame timing and GBA controls. Normal CV/knob maps, external
clocking, patch slots, save/load and the production UI are not wired. Track pitch/gate replies use
the existing protocol but have not been verified on hardware. Gain starts at half renderer unity.

## Rebuild without touching the application

With ARM GCC, Python, CMake, Ninja and Pico SDK 2.2.0 available:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
bash engines/gax/trial/build.sh
```

Every generated file goes under `engines/gax/build/trial/`. The script reads the existing startup,
IRQ/link/graphics/cart/audio sources. Only this compilation redirects audio's init/render calls to
`gx_pcm_*`. CMake compiles a build-directory copy of the RP2040 main against the trial payload.
It never rewrites the original `gba_payload.h` or changes the normal CMake/payload scripts.

Output: `engines/gax/build/gax-trial.uf2`. The artifact and checksum here are copies of a completed
build. Updating those committed copies is deliberate, not automatic.

## Evidence and remaining checks

- ARM7TDMI compilation with `-Werror`, freestanding link with no unresolved libc/libgcc symbols.
- Host ASan/UBSan functional tests and 3,000 randomized pattern/instrument/control cases.
- An independent MIT Python serializer created native bus-addressed data from authored test notes
  and waveform. The player scanned it and rendered five seconds of PCM with nine loops, no decoder
  errors and no unsupported-feature reports. This was synthetic data, not a commercial ROM test.
- GBA multiboot title/logo/checksum, embedded payload identity, UF2 block framing/RP2040 family/
  flash addresses and GBA/RP2040 memory limits checked for the supplied artifact.

**Still untested:** flashing, cable boot, real-cart scan, real-time audio timing and audible playback
on a physical GBA/Workshop Computer. Those are the purpose of this trial, not completed validation.
