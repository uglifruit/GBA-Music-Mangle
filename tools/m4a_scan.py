#!/usr/bin/env python3
"""m4a_scan.py - mirror of the payload's cart scan (payload/scan.c), run on a ROM file.

    python -I tools/m4a_scan.py game.gba [--songs]

Prints what the GBA's SCAN page should show for the same cartridge: every song table found (a
cart can carry more than one copy of the sound library), how each was found, its address and
length, its playable songs split into music (music player 0) and sound effects (any other player),
and (with --songs) each playable song's header, track count and player. If the GBA and this
disagree on a cart, one of them has a bug - this file is meant to stay a line-for-line mirror of
scan.c, so fix both together.

Use your own cartridge dumps.
"""
import argparse
import struct
import sys

ROM_BASE = 0x08000000
MAX_TRACKS = 16
SCAN_MAX_SONGS = 2048
SCAN_MAX_TABLES = 8
SELECT_SONG = bytes([
    0x00, 0xb5, 0x00, 0x04, 0x07, 0x4a, 0x08, 0x49, 0x40, 0x0b,
    0x40, 0x18, 0x83, 0x88, 0x59, 0x00, 0xc9, 0x18, 0x89, 0x00,
    0x89, 0x18, 0x0a, 0x68, 0x01, 0x68, 0x10, 0x1c, 0x00, 0xf0,
])
SELECT_SONG_MAX_DIFF = 8
SELECT_SONG_TABLE_OFF = 40
M4A_RATES = [5734, 7884, 10512, 13379, 15768, 18157, 21024, 26758, 31536, 36314, 40137, 42048]


class Rom:
    def __init__(self, data):
        # The GBA sees the image padded to a power-of-two cart size; scan.c bounds everything by
        # that detected size, so do the same.
        size = 0x100000
        while size < len(data):
            size <<= 1
        self.size = min(size, 0x2000000)
        self.data = data + bytes(self.size - len(data))
        self.end = ROM_BASE + self.size

    def ok(self, addr, n):
        return ROM_BASE <= addr < self.end and n <= self.end - addr

    def isptr(self, v):
        return ROM_BASE <= v < self.end

    def u8(self, a):
        return self.data[a - ROM_BASE]

    def u16(self, a):
        return struct.unpack_from('<H', self.data, a - ROM_BASE)[0]

    def u32(self, a):
        return struct.unpack_from('<I', self.data, a - ROM_BASE)[0]


def header_ok(rom, hdr):
    """scan_header_ok: returns track count, or None if not believable."""
    if hdr & 3 or not rom.ok(hdr, 8):
        return None
    tc = rom.u8(hdr)
    if tc > MAX_TRACKS:
        return None
    if tc == 0:
        return 0
    if not rom.ok(hdr, 8 + 4 * tc):
        return None
    tone = rom.u32(hdr + 4)
    if tone & 3 or not rom.ok(tone, 12):
        return None
    for i in range(tc):
        if not rom.isptr(rom.u32(hdr + 8 + 4 * i)):
            return None
    return tc


def entry_ok(rom, e):
    if not rom.ok(e, 8) or rom.u16(e + 4) >= 32:
        return None
    return header_ok(rom, rom.u32(e))


def find_signatures(rom):
    """Every SelectSong match, in address order, as (routine, table) - scan.c's find_signature."""
    d = rom.data
    found = []
    off = 0
    while True:
        # next 4-aligned "00 b5" (PUSH {LR}), as next_push_lr does
        off = d.find(b'\x00\xb5', off)
        if off < 0 or off + len(SELECT_SONG) > rom.size:
            return found
        if off & 3:
            off += 1
            continue
        diff = sum(1 for i in range(len(SELECT_SONG)) if d[off + i] != SELECT_SONG[i])
        a = ROM_BASE + off
        if diff <= SELECT_SONG_MAX_DIFF and rom.ok(a + SELECT_SONG_TABLE_OFF, 4):
            table = rom.u32(a + SELECT_SONG_TABLE_OFF)
            if not table & 3 and rom.ok(table, 8):
                found.append((a, table))
        off += 4


def find_heuristic(rom):
    """The longest few runs (>= 8 entries, >= 4 real songs), longest first."""
    best = []
    a = ROM_BASE
    while a < rom.end:
        w = rom.u32(a) if rom.ok(a, 4) else 0
        if not (w & 3) and rom.isptr(w) and rom.ok(a, 8) and rom.u16(a + 4) < 32:
            n = real = 0
            while True:
                tc = entry_ok(rom, a + n * 8)
                if tc is None:
                    break
                if tc > 0:
                    real += 1
                n += 1
                if n >= SCAN_MAX_SONGS:
                    break
            if real >= 4 and n >= 8 and (len(best) < SCAN_MAX_TABLES or n > best[-1][1]):
                best.append((a, n))
                best.sort(key=lambda x: -x[1])        # stable: an equal run keeps its place
                del best[SCAN_MAX_TABLES:]
            if n:
                a += (n - 1) * 8
        a += 4
    return [a for a, _ in best]


def find_sound_mode(rom, near):
    lo = near - 0x2000 if near > ROM_BASE + 0x2000 else ROM_BASE
    hi = min(near + 0x2000, rom.end)
    a = lo & ~3
    while a + 4 <= hi:
        if near <= a < near + 48:
            a += 4
            continue
        w = rom.u32(a)
        da, fq, mv, ch = (w >> 20) & 0xF, (w >> 16) & 0xF, (w >> 12) & 0xF, (w >> 8) & 0xF
        if (w >> 24) == 0 and 8 <= da <= 0xB and 1 <= fq <= 12 and mv >= 4 and 1 <= ch <= 12 \
                and ((w & 0xFF) == 0 or (w & 0x80)):
            return w
        a += 4
    return 0


def song_list(rom, table):
    """scan_select with LIST_ALL: (entries, [(index, header, tracks, player), ...])."""
    songs, entries, prev = [], 0, 0
    for i in range(0xFFFF):
        e = table + i * 8
        if not rom.ok(e, 8):
            break
        hdr = rom.u32(e)
        if not rom.isptr(hdr):
            break
        entries = i + 1
        tc = header_ok(rom, hdr)
        if hdr != prev and tc and len(songs) < SCAN_MAX_SONGS:
            songs.append((i, hdr, tc, rom.u16(e + 4)))
        prev = hdr
    return entries, songs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('rom')
    ap.add_argument('--songs', action='store_true', help='list every playable song')
    args = ap.parse_args()

    with open(args.rom, 'rb') as f:
        rom = Rom(f.read())

    title = rom.data[0xA0:0xAC].rstrip(b'\0').decode('ascii', 'replace')
    code = rom.data[0xAC:0xB0].decode('ascii', 'replace')
    print(f'title      {title}  [{code}]')
    print(f'rom size   {rom.size // 1024} KB')

    # add_table: dedupe by table address, at most SCAN_MAX_TABLES
    tables = []
    sigs = find_signatures(rom)
    cands = [('SIGNATURE', s, t) for s, t in sigs] if sigs else \
            [('HEURISTIC', 0, t) for t in find_heuristic(rom)]
    for method, select, table in cands:
        if any(t[2] == table for t in tables) or len(tables) >= SCAN_MAX_TABLES:
            continue
        tables.append((method, select, table))

    # drop tables with no playable song
    kept = []
    for method, select, table in tables:
        entries, songs = song_list(rom, table)
        if songs:
            kept.append((method, select, table, entries, songs))
    if not kept:
        print('method     NONE - no MP2K song table found')
        return 1

    print(f'tables     {len(kept)}')
    for n, (method, select, table, entries, songs) in enumerate(kept):
        music = sum(1 for s in songs if s[3] == 0)
        print(f'\n--- table {n + 1}: {method}')
        if select:
            print(f'selectsong 0x{select:08X}')
        print(f'song table 0x{table:08X}')
        print(f'entries    {entries}')
        print(f'playable   {len(songs)}  (music {music}, sfx {len(songs) - music})')
        mode = find_sound_mode(rom, select) if select else 0
        if mode:
            print(f'sound mode 0x{mode:08X}  rate {M4A_RATES[((mode >> 16) & 0xF) - 1]} Hz  '
                  f'chans {(mode >> 8) & 0xF}  masvol {(mode >> 12) & 0xF}')
        else:
            print('sound mode not found (assuming 13379 Hz)')
        if args.songs:
            print('\n  n   entry  header      tracks player')
            for k, (i, hdr, tc, ms) in enumerate(songs):
                print(f'{k + 1:4d}  {i:5d}  0x{hdr:08X}  {tc:5d}  {ms:5d}{"  (sfx)" if ms else ""}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
