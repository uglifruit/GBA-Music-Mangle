#!/usr/bin/env bash
# build.sh — build pcplay, the PC renderer, from the payload's own engine files.
#
# Compiler: $CC if set, else zig's bundled clang via pip (`pip install ziglang`), which needs no
# admin rights and is what this Windows machine uses.
set -euo pipefail
cd "$(dirname "$0")"
P=../../payload
CC_CMD=(${CC:-python -m ziglang cc})
mkdir -p build
EXE=build/pcplay
[[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]] && EXE=build/pcplay.exe
"${CC_CMD[@]}" -std=gnu11 -DHOST -O2 -g -Wall -Wextra -I"$P" -I../.. \
    pcplay.c "$P/rom.c" "$P/scan.c" "$P/player.c" "$P/cgb.c" "$P/mixer.c" "$P/m4a_tables.c" \
    -o "$EXE"
echo "built $EXE"

# modtest: the modulator against stubbed link inputs (see modtest.c).
MT=build/modtest
[[ "$EXE" == *.exe ]] && MT=build/modtest.exe
"${CC_CMD[@]}" -std=gnu11 -DHOST -O1 -g -Wall -Wextra -I"$P" -I../.. \
    modtest.c "$P/rom.c" "$P/scan.c" "$P/player.c" "$P/cgb.c" "$P/mixer.c" "$P/m4a_tables.c" \
    "$P/mod.c" "$P/clock.c" -o "$MT"
echo "built $MT"
