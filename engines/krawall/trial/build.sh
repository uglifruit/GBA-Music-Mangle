#!/usr/bin/env bash
# Explicit experimental build: all products go under engines/krawall/build/trial.
set -euo pipefail
cd "$(dirname "$0")"
engine=$(cd .. && pwd)
repo=$(cd ../../.. && pwd)
build="$engine/build/trial"
mkdir -p "$build/payload"
cc=${ARM_CC:-arm-none-eabi-gcc}
flags=(-mcpu=arm7tdmi -mthumb-interwork -marm -O2 -ffreestanding -fno-builtin -Wall -Wextra -Werror -I"$repo/payload" -I"$repo")
objects=()
compile() {
    local source=$1 output=$2
    shift 2
    "$cc" "${flags[@]}" "$@" -c "$source" -o "$build/payload/$output.o"
    objects+=("$build/payload/$output.o")
}
compile "$repo/payload/crt0.s" crt0 -x assembler-with-cpp
for src in irq link gfx rom; do compile "$repo/payload/$src.c" "$src"; done
# Remap only this compilation's two audio calls; no source or original renderer is altered.
compile "$repo/payload/audio.c" audio -Dmixer_init=kr_pcm_init -Dmixer_render=kr_pcm_render
for src in krawall discovery pcm_mixer mangle_adapter; do compile "$engine/$src.c" "$src"; done
compile "$engine/trial/main.c" trial_main
"$cc" "${flags[@]}" -nostdlib -T "$repo/payload/multiboot.ld" "${objects[@]}" -Wl,-Map,"$build/payload/trial.map" -o "$build/payload/trial.elf"
arm-none-eabi-objcopy -O binary "$build/payload/trial.elf" "$build/payload/trial.mb"
python - "$build/payload/trial.mb" <<'PYHEADER'
import sys
with open(sys.argv[1], 'r+b') as f:
    f.seek(0xA0); f.write(b'KRAWALLTRIALKRW0')
PYHEADER
python "$repo/payload/gbafix.py" "$build/payload/trial.mb"
python "$repo/payload/bin2h.py" "$build/payload/trial.mb" "$build/payload/gba_payload.h" gba_payload
cmake -S "$engine/trial" -B "$build/rp2040" -G Ninja -DTRIAL_PAYLOAD_HEADER="$build/payload/gba_payload.h"
cmake --build "$build/rp2040" -j 4
cp "$build/rp2040/krawall_trial.uf2" "$engine/build/krawall-trial.uf2"
python "$engine/trial/verify.py" "$engine/build/krawall-trial.uf2" "$build/payload/trial.mb"
sha256sum "$engine/build/krawall-trial.uf2"
