#!/usr/bin/env python3
"""mkengines.py OUTDIR [MP2K_TEST.gba] - synthetic carts for the other engines.

  gax.gba      a GAX 3 song (the GAX player's own test fixture) and the engine's credit text
  krawall.gba  a Krawall module with a four-entry sample bank and a "$Id: Krawall" line
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


def krawall_block(rom, o):
    """Krawall: module at o+0x1000, pattern at o+0x2000, a sample, a 4-entry bank at o+0x800."""
    for k in range(4):
        put32(rom, o + 0x800 + 4 * k, BASE + o + 0x3000)
    put32(rom, o + 0x3000, 256); put32(rom, o + 0x3004, BASE + o + 0x3012 + 256); put32(rom, o + 0x3008, 8363)
    rom[o + 0x300E] = 64; rom[o + 0x3010] = 1
    for i in range(257):
        rom[o + 0x3012 + i] = ((i % 32) * 7 + 16) & 255
    m = o + 0x1000
    rom[m] = 1; rom[m + 1] = 1; rom[m + 355] = 128; rom[m + 356] = 6; rom[m + 357] = 125
    put32(rom, m + 364, BASE + o + 0x2000)
    put16(rom, o + 0x2020, 8)
    d = o + 0x2022
    rom[d:d + 4] = bytes([0x60, 49, 1, 0x50])
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
