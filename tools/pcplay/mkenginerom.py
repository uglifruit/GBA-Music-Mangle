#!/usr/bin/env python3
"""mkenginerom.py - a synthetic 1 MB cart with NO MP2K data, only other engines' signatures.

    python -I tools/pcplay/mkenginerom.py engines.gba

Tests the scan's engine sniff (scan.h, "OTHER ENGINES"). Every signature sits at a different
alignment, one starts at the image's first byte after the header and one ends on its last byte,
and there's a near miss that must not count. Expected (from m4a_scan.py, pcplay, or the GBA):

    engine     GAX KRAWALL XM x2
    info       GAX Sound Engine 3.05 (Aug 13 2003)
"""
import sys

SIZE = 0x100000
rom = bytearray((i * 7 + (i >> 8)) & 0x7F for i in range(SIZE))   # junk, no pointers
rom[0xA0:0xAC] = b'ENGINE TEST\0'
rom[0xAC:0xB0] = b'ENGT'
rom[0xB2] = 0x96


def put(at, data):
    rom[at:at + len(data)] = data


put(0x1001, b"GAX Sound Engine 3.05 (Aug 13 2003) \xa9 Shin'en Multimedia\0")   # off by 1
put(0x2003, b'GAX ENGINE V3.05\0')                                               # off by 3
put(0x3002, b'$Id: Krawall.c,v 1.12 2003/04/02 seb Exp $\0')                     # off by 2
put(0x4000, b'Extended Module: Title Theme         \x1a')                       # aligned
put(SIZE - 17, b'Extended Module: ')                                             # last byte
put(0x5005, b'GAX Sound Engin\0')                                                # near miss
with open(sys.argv[1] if len(sys.argv) > 1 else 'engines.gba', 'wb') as f:
    f.write(rom)
