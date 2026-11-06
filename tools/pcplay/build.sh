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
# The other engines, through the dispatcher (payload/engine.c).
ENG="../../engines/musyx/musyx.c ../../engines/gax/gax.c ../../engines/krawall/krawall.c ../../engines/krawall/kr_tables.c ../../engines/krawall/discovery.c ../../engines/common/eng_mixer.c ../../engines/common/eng_chaos.c"
EXE=build/pcplay
[[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]] && EXE=build/pcplay.exe
"${CC_CMD[@]}" -std=gnu11 -DHOST -O2 -g -Wall -Wextra -I"$P" -I../.. \
    pcplay.c psgemu.c "$P/rom.c" "$P/scan.c" "$P/player.c" "$P/cgb.c" "$P/mixer.c" "$P/m4a_tables.c" "$P/engine.c" "$P/eng_musyx.c" "$P/eng_gax.c" "$P/eng_krawall.c" $ENG \
    -o "$EXE"
echo "built $EXE"

# modtest: the modulator against stubbed link inputs (see modtest.c).
MT=build/modtest
[[ "$EXE" == *.exe ]] && MT=build/modtest.exe
"${CC_CMD[@]}" -std=gnu11 -DHOST -O1 -g -Wall -Wextra -I"$P" -I../.. \
    modtest.c "$P/rom.c" "$P/scan.c" "$P/player.c" "$P/cgb.c" "$P/mixer.c" "$P/m4a_tables.c" "$P/engine.c" "$P/eng_musyx.c" "$P/eng_gax.c" "$P/eng_krawall.c" $ENG \
    "$P/mod.c" "$P/clock.c" -o "$MT"
echo "built $MT"
