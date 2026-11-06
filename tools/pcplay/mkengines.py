#!/usr/bin/env python3
"""mkengines.py OUTDIR [MP2K_TEST.gba] - synthetic carts for the other engines.

  gax.gba      a GAX 3 song (the GAX player's own test fixture) and the engine's credit text
  krawall.gba  a Krawall module with a four-entry sample bank and a "$Id: Krawall" line
  krawall2.gba an instrument-based Krawall module holding two songs (order list 0, +++, 1) whose
               restart lies past its order list, with what real carts have and the first discovery
               refused: an instrument with no envelope points (count stored as 255), a sample-map
               entry with a junk high byte, a sample panned past +-64
  gax2.gba     a GAX 2 song: a song entry of four handlers (dummy, header, the channel's pattern
               table, dummy), the V2 header, a V2 instrument (one wave, envelope at +124, performance
               rows at +136), an empty sample 0 and a SIGNED sample, and "GAX Sound Engine v2.01a"
  mixed.gba    the MP2K test cart (mktestrom.py's) with the GAX song and text added: a cart that
               uses two engines, for the scan's multi-engine tables

Authored test data only, laid out as engines/gax/tests and engines/krawall/tests build theirs.
"""
import pathlib
import struct
import sys

BASE = 0x08000000


def header(rom, title, code):
    rom[0xA0:0xAC] = title.ljust(12, b'\0')[:12]
    rom[0xAC:0xB0] = code
    rom[0xB2] = 0x96


def put16(rom, a, v): struct.pack_into('<H', rom, a, v & 0xFFFF)
def put32(rom, a, v): struct.pack_into('<I', rom, a, v & 0xFFFFFFFF)


def gax_block(rom, o):
    """GAX: header at o+0x1000; one channel, eight rows, two orders, one compressed pattern."""
    put16(rom, o + 0x1000, 1); put16(rom, o + 0x1002, 8); put16(rom, o + 0x1004, 2); put16(rom, o + 0x1008, 256)
    put32(rom, o + 0x100C, BASE + o + 0x2000); put32(rom, o + 0x1010, BASE + o + 0x3000)
    put32(rom, o + 0x1014, BASE + o + 0x3100)
    put16(rom, o + 0x1018, 15769); put32(rom, o + 0x1020, BASE + o + 0x4000)
    rom[o + 0x4006] = 12                                  # second order transposed an octave
    rom[o + 0x2000:o + 0x2005] = bytes([0, 49 | 128, 1, 0xFF, 7])
    put32(rom, o + 0x3000, BASE + o + 0x3200); rom[o + 0x3200] = 1; put32(rom, o + 0x3004, BASE + o + 0x3300)
    rom[o + 0x3301] = 1; put32(rom, o + 0x330C, BASE + o + 0x3500); rom[o + 0x3310] = 1; rom[o + 0x3311] = 1
    put32(rom, o + 0x3314, BASE + o + 0x3600)
    put32(rom, o + 0x331C, 0); put32(rom, o + 0x3320, 0); put32(rom, o + 0x3324, 256)
    rom[o + 0x3500] = 1; rom[o + 0x3501] = rom[o + 0x3502] = rom[o + 0x3503] = 255; rom[o + 0x3508] = 255
    rom[o + 0x3600] = 3; rom[o + 0x3602] = 1
    put32(rom, o + 0x3100, BASE + o + 0x3800); put32(rom, o + 0x3104, 0)
    put32(rom, o + 0x3108, BASE + o + 0x5000); put32(rom, o + 0x310C, 256)
    for i in range(257):
        rom[o + 0x5000 + i] = (16 + (i % 32) * 7) & 255
    text = b'GAX Sound Engine 3.05 (synthetic test)\0'
    rom[o + 0x6001:o + 0x6001 + len(text)] = text      # deliberately unaligned


def gax2_block(rom, o):
    """GAX 2: song entry at o+0x7000 (handlers at o+0x7100..), V2 header at o+0x1000, pattern table at
    o+0x4000, sequence at o+0x2000, instrument table o+0x3000 (instrument at o+0x3300), samples o+0x3100."""
    put16(rom, o + 0x1000, 1); put16(rom, o + 0x1002, 8); put16(rom, o + 0x1004, 2); put16(rom, o + 0x1008, 256)
    put32(rom, o + 0x100C, BASE + o + 0x2000); put32(rom, o + 0x1010, BASE + o + 0x3000)
    put32(rom, o + 0x1014, BASE + o + 0x3100); put16(rom, o + 0x1018, 15769)
    rom[o + 0x4006] = 12                                  # second order transposed an octave
    rom[o + 0x2000:o + 0x2005] = bytes([0, 49 | 128, 1, 0xFF, 7])
    put32(rom, o + 0x3000, BASE + o + 0x3200); put32(rom, o + 0x3004, BASE + o + 0x3300)
    i = o + 0x3300                                        # the V2 instrument
    rom[i + 1] = 1                                        # sample 1
    put32(rom, i + 16, 0); put32(rom, i + 20, 0); put32(rom, i + 24, 256)   # start, loop, end
    put32(rom, i + 124, BASE + o + 0x3500); put32(rom, i + 128, BASE + o + 0x3500)
    rom[i + 132] = 1; rom[i + 133] = 1; put32(rom, i + 136, BASE + o + 0x3600)   # perf speed 1 (0 never steps)
    rom[o + 0x3500] = 1; rom[o + 0x3501] = rom[o + 0x3502] = rom[o + 0x3503] = 255; rom[o + 0x3508] = 255
    rom[o + 0x3600] = 2; rom[o + 0x3602] = 1              # performance row: note 2 (no shift), slot 1
    put32(rom, o + 0x3100, 0); put32(rom, o + 0x3104, 0)  # sample 0 empty, as V2 carts have it
    put32(rom, o + 0x3108, BASE + o + 0x5000); put32(rom, o + 0x310C, 256)
    for k in range(257):
        rom[o + 0x5000 + k] = ((k % 32) * 7 - 112) & 255   # a SIGNED saw
    code = BASE + o + 0x6801                              # handlers' code pointers (Thumb: odd)
    for n, data in enumerate((o + 0x7400, o + 0x1000, o + 0x4000, o + 0x7400)):
        hd = o + 0x7100 + n * 0x20
        for k, v in enumerate((code, code + 4, code + 8, 1, 0, 0x4C, BASE + data)):
            put32(rom, hd + 4 * k, v)
        put32(rom, o + 0x7004 + 4 * n, BASE + hd)
    put32(rom, o + 0x7000, 4)
    text = b'GAX Sound Engine v2.01a (synthetic test)\0'
    rom[o + 0x6001:o + 0x6001 + len(text)] = text


def kr_sample(rom, a, pan=0):
    """A 256-byte looping Krawall sample at a (a saw), as krawerter's Sample.cpp lays one out."""
    put32(rom, a, 256); put32(rom, a + 4, BASE + a + 0x12 + 256); put32(rom, a + 8, 8363)
    rom[a + 0xE] = 64; rom[a + 0xF] = pan & 255; rom[a + 0x10] = 1
    for i in range(257):
        rom[a + 0x12 + i] = ((i % 32) * 7 + 16) & 255


def kr_bank(rom, o, n, first, stride):
    """A bank: n pointers at o to entries first, first + stride, ... (ascending, as krawerter writes)."""
    for k in range(n):
        put32(rom, o + 4 * k, BASE + first + k * stride)


def krawall_block(rom, o):
    """Krawall: module at o+0x1000, pattern at o+0x2000, a 4-entry bank of samples at o+0x800."""
    kr_bank(rom, o + 0x800, 4, o + 0x3000, 0x200)
    for k in range(4):
        kr_sample(rom, o + 0x3000 + k * 0x200)
    m = o + 0x1000
    rom[m] = 1; rom[m + 1] = 1; rom[m + 355] = 128; rom[m + 356] = 6; rom[m + 357] = 125
    put32(rom, m + 364, BASE + o + 0x2000)
    put16(rom, o + 0x2020, 8)
    d = o + 0x2022
    rom[d:d + 4] = bytes([0x60, 49, 1, 0x50])
    text = b'$Id: Krawall synthetic test $\0'
    rom[o + 0x6002:o + 0x6002 + len(text)] = text


def krawall2_block(rom, o):
    """Krawall, instrument-based: module at o+0x1000 (orders 0, 254, 1; restart 5), patterns at
    o+0x2000 (8 rows, instrument 1) and o+0x2100 (64 rows, instrument 3), samples at o+0x3000,
    instruments at o+0x4000, their banks at o+0x800 and o+0x900."""
    kr_bank(rom, o + 0x800, 4, o + 0x3000, 0x200)
    for k in range(4):
        kr_sample(rom, o + 0x3000 + k * 0x200, pan=-100 if k == 2 else 0)
    kr_bank(rom, o + 0x900, 4, o + 0x4000, 0x140)
    for k in range(4):
        a = o + 0x4000 + k * 0x140
        for i in range(96):
            put16(rom, a + 2 * i, k)                  # every key plays sample k
        if k == 0:
            put16(rom, a + 2 * 48, 0x9800)            # note 49's entry: a junk high byte over 0
        if k == 2:
            rom[a + 192 + 48] = rom[a + 244 + 48] = 255   # no points: krawerter writes num-1 = 255
    m = o + 0x1000
    rom[m] = 1; rom[m + 1] = 3; rom[m + 2] = 5        # one channel, three orders, restart past them
    rom[m + 3:m + 6] = bytes([0, 254, 1])
    rom[m + 355] = 128; rom[m + 356] = 6; rom[m + 357] = 125; rom[m + 358] = 1
    put32(rom, m + 364, BASE + o + 0x2000); put32(rom, m + 368, BASE + o + 0x2100)
    put16(rom, o + 0x2020, 8)
    rom[o + 0x2022:o + 0x2026] = bytes([0x20, 49, 1, 0])          # ch 0: note 49, instrument 1
    put16(rom, o + 0x2120, 64)
    rom[o + 0x2122:o + 0x2126] = bytes([0x20, 49, 3, 0])          # ch 0: note 49, instrument 3
    text = b'$Id: Krawall synthetic test $\0'
    rom[o + 0x6002:o + 0x6002 + len(text)] = text


def main():
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    rom = bytearray(0x10000)
    header(rom, b'GAX TEST', b'GAXT')
    gax_block(rom, 0x1000)
    (out / 'gax.gba').write_bytes(rom)
    rom = bytearray(0x10000)
    header(rom, b'KRAWALL TEST', b'KRWT')
    krawall_block(rom, 0x1000)
    (out / 'krawall.gba').write_bytes(rom)
    rom = bytearray(0x10000)
    header(rom, b'KRAWALL TWO', b'KRW2')
    krawall2_block(rom, 0x1000)
    (out / 'krawall2.gba').write_bytes(rom)
    rom = bytearray(0x10000)
    header(rom, b'GAX TWO TEST', b'GAX2')
    gax2_block(rom, 0x1000)
    (out / 'gax2.gba').write_bytes(rom)
    if len(sys.argv) > 2:
        rom = bytearray(pathlib.Path(sys.argv[2]).read_bytes())
        o = 0xC0000
        if len(rom) < o + 0x8000 or any(rom[o:o + 0x8000]):
            sys.exit('mixed: the MP2K cart has no free 32 KB at 0xC0000')
        gax_block(rom, o)
        (out / 'mixed.gba').write_bytes(rom)
    print('wrote', ', '.join(p.name for p in sorted(out.glob('*.gba'))))


if __name__ == '__main__':
    main()
