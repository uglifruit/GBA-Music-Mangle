#!/usr/bin/env python3
"""Verify UF2 framing, RP2040 family, embedded multiboot bytes and GBA header checksum."""
import pathlib
import struct
import sys


def verify(uf2, payload):
    blob = pathlib.Path(uf2).read_bytes()
    mb = pathlib.Path(payload).read_bytes()
    assert blob and len(blob) % 512 == 0, 'invalid UF2 length'
    flash = bytearray()
    last = None
    total = len(blob) // 512
    for i in range(total):
        block = blob[i * 512:(i + 1) * 512]
        magic0, magic1, flags, address, size, number, count, family = struct.unpack_from('<8I', block)
        assert (magic0, magic1) == (0x0A324655, 0x9E5D5157), 'UF2 magic'
        assert struct.unpack_from('<I', block, 508)[0] == 0x0AB16F30, 'UF2 tail'
        assert flags & 0x2000 and family == 0xE48BFF56, 'RP2040 family'
        assert number == i and count == total and size == 256, 'UF2 block sequence'
        assert 0x10000000 <= address < 0x10200000, 'flash address'
        assert last is None or address == last + size, 'noncontiguous flash'
        last = address
        flash.extend(block[32:32 + size])
    assert 0xC0 <= len(mb) <= 0x40000, 'multiboot size'
    assert mb[0xA0:0xAC] == b'GAXTRIALPOC ', 'trial payload title'
    assert mb[0xB2] == 0x96, 'fixed GBA header byte'
    assert (sum(mb[0xA0:0xBE]) + 0x19) & 255 == 0, 'GBA checksum'
    # gbafix's fixed logo is authoritative; import the read-only repository helper.
    sys.dont_write_bytecode = True
    import importlib.util
    helper = pathlib.Path(__file__).resolve().parents[3] / 'payload' / 'gbafix.py'
    spec = importlib.util.spec_from_file_location('gbafix', helper)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    assert mb[4:0xA0] == mod.NINTENDO_LOGO, 'GBA logo'
    assert mb in flash, 'experimental payload missing from RP2040 image'
    print(f'PASS: {total} RP2040 UF2 blocks, {len(mb)}-byte trial multiboot payload, header and inclusion')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('verify.py UF2 PAYLOAD.mb')
    verify(*sys.argv[1:])
