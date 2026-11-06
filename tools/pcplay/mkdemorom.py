#!/usr/bin/env python3
"""mkdemorom.py - a synthetic MP2K cart to photograph: the manual's screenshots (payload/screenshots.py).

    python -I tools/pcplay/mkdemorom.py demo.gba

mktestrom.py's library (a SelectSong-shaped routine, a sound-mode word, a saw and a kick) with songs
that fill the PLAY page: an 8-track groove (bass, three chord voices, an arpeggio, a lead, kick and a
high tick), a 4-track second piece, and a one-shot effect. Everything is generated here; nothing comes
from a game. mktestrom.py stays as it is (the reference WAVs are rendered from it).
"""
import math
import struct
import sys

B = 0x08000000
SIZE = 0x40000

rom = bytearray(SIZE)
rom[0xA0:0xAC] = b'DEMO CART\0\0\0'
rom[0xAC:0xB0] = b'DEMO'
rom[0xB2] = 0x96


def w32(off, v):
    struct.pack_into('<I', rom, off, v & 0xFFFFFFFF)


# ---- the library, as mktestrom.py lays it out ----
rom[0x1000:0x101E] = bytes([
    0x00, 0xb5, 0x00, 0x04, 0x07, 0x4a, 0x08, 0x49, 0x40, 0x0b,
    0x40, 0x18, 0x83, 0x88, 0x59, 0x00, 0xc9, 0x18, 0x89, 0x00,
    0x89, 0x18, 0x0a, 0x68, 0x01, 0x68, 0x10, 0x1c, 0x00, 0xf0])
TABLE = 0x2000
w32(0x1028, B + TABLE)
rom[0x0F00:0x0F04] = bytes([0x70, 0xb5, 0x14, 0x48])
w32(0x0FE0, 0x0094C500)        # DA 8 bit, 13379 Hz, master 12, 5 channels
rom[0x0FF0:0x0FF2] = bytes([0x00, 0xb5])

SAW, SAW_LEN = 0x10000, 64
w32(SAW, 0x40000000); w32(SAW + 4, int(261.6256 * SAW_LEN * 1024)); w32(SAW + 8, 0); w32(SAW + 12, SAW_LEN)
for i in range(SAW_LEN):
    rom[SAW + 16 + i] = (int(-100 + 200 * i / SAW_LEN)) & 0xFF
KICK, KICK_LEN = 0x11000, 3000
w32(KICK, 0); w32(KICK + 4, 13379 * 1024); w32(KICK + 8, 0); w32(KICK + 12, KICK_LEN)
for i in range(KICK_LEN):
    v = 110 * math.exp(-i / 700) * math.sin(2 * math.pi * 60 * i / 13379 * (1 + 2 * math.exp(-i / 150)))
    rom[KICK + 16 + i] = int(v) & 0xFF


def tone(off, typ, key, pansweep, wav, a, d, s, r):
    rom[off:off + 4] = bytes([typ, key, 0, pansweep])
    w32(off + 4, wav)
    rom[off + 8:off + 12] = bytes([a, d, s, r])


VG = 0x3000
tone(VG + 0 * 12, 0x00, 60, 0, B + SAW, 255, 250, 200, 200)      # saw, sustained
tone(VG + 1 * 12, 0x00, 60, 0, B + SAW, 255, 200, 0, 120)        # saw, plucked
DRUMS = 0x3C00
for k in range(128):
    tone(DRUMS + 12 * k, 0x00, 60, 0, B + KICK, 255, 0, 255, 160)
tone(VG + 2 * 12, 0x80, 60, 0, B + DRUMS, 0, 0, 0, 0)            # rhythm: the kick at every key

FINE, GOTO, TEMPO, VOICE, VOL, PAN = 0xB1, 0xB2, 0xBB, 0xBD, 0xBE, 0xBF
CLOCK = list(range(25)) + [28, 30, 32, 36, 40, 42, 44, 48, 52, 54, 56, 60, 64, 66, 68, 72, 76, 78, 80, 84,
                          88, 90, 92, 96]           # m4a's gClockTable: waits and lengths are indices
def W(t): return 0x80 + CLOCK.index(t)
def N(t): return 0xCF + CLOCK.index(t)


def track(off, head, notes):
    """head: setup bytes; notes: (key, velocity, length, wait) looped with GOTO."""
    body = bytearray(head)
    loop = len(body)
    for key, vel, length, wait in notes:
        body += bytes([N(length), key, vel, W(wait)])
    rom[off:off + len(body)] = body
    p = off + len(body)
    rom[p] = GOTO; w32(p + 1, B + off + loop); rom[p + 5] = FINE
    return off


def header(off, tracks):
    rom[off:off + 4] = bytes([len(tracks), 0, 0, 0])
    w32(off + 4, B + VG)
    for i, t in enumerate(tracks):
        w32(off + 8 + 4 * i, B + t)
    return off


# song 0: an 8-track groove in A minor, 4 bars of Am F C G at 110 BPM (a beat is 24 ticks)
prog = [(57, 60, 64), (53, 57, 60), (48, 52, 55), (55, 59, 62)]   # Am F C G
t = 0x5000
s0 = []
s0.append(track(t, [VOL, 80, VOICE, 1, TEMPO, 55, PAN, 64],
                [(r - 24, 110, 12, 12) for r, _, _ in prog for _ in range(8)])); t += 0x100      # bass, eighths
for v in range(3):                                                                               # chords
    s0.append(track(t, [VOL, 45, VOICE, 0, PAN, 40 + 24 * v],
                    [(c[v], 80, 96, 96) for c in prog])); t += 0x100
s0.append(track(t, [VOL, 55, VOICE, 1, PAN, 90],                                                 # arpeggio
                [(c[i % 3] + 12, 90, 6, 6) for c in prog for i in range(16)])); t += 0x200
s0.append(track(t, [VOL, 65, VOICE, 0, PAN, 64],                                                 # lead
                [(76, 100, 24, 24), (72, 100, 24, 24), (74, 100, 48, 48),
                 (77, 100, 24, 24), (76, 100, 24, 24), (72, 100, 48, 48),
                 (79, 100, 48, 48), (76, 100, 24, 24), (74, 100, 24, 24),
                 (71, 100, 48, 48), (72, 100, 24, 24), (74, 100, 24, 24)])); t += 0x100
s0.append(track(t, [VOL, 120, VOICE, 2], [(36, 120, 12, 24)] * 16)); t += 0x100                   # kick
s0.append(track(t, [VOL, 60, VOICE, 1, PAN, 100], [(96, 70, 3, 12)] * 32)); t += 0x100            # tick
SONG0 = header(0x4000, s0)

# song 1: a 4-track piece, slower
s1 = []
s1.append(track(t, [VOL, 110, VOICE, 1, TEMPO, 45], [(45, 110, 24, 24), (43, 110, 24, 24), (41, 110, 48, 48)])); t += 0x100
s1.append(track(t, [VOL, 80, VOICE, 0], [(64, 80, 48, 48), (62, 80, 48, 48)])); t += 0x100
s1.append(track(t, [VOL, 80, VOICE, 0], [(69, 80, 48, 48), (67, 80, 48, 48)])); t += 0x100
s1.append(track(t, [VOL, 120, VOICE, 2], [(36, 120, 12, 24)] * 4)); t += 0x100
SONG1 = header(0x4040, s1)

# song 2: a one-shot effect on music player 1
FX = t
rom[FX:FX + 12] = bytes([VOL, 120, VOICE, 1, N(6), 84, 110, W(6), N(12), 91, 110, W(12)])
rom[FX + 12] = FINE
SONG2 = header(0x4080, [FX])

for i, (h, ms) in enumerate([(SONG0, 0), (SONG1, 0), (SONG2, 1)]):
    struct.pack_into('<IHH', rom, TABLE + 8 * i, B + h, ms, ms)
w32(TABLE + 24, 0)

with open(sys.argv[1] if len(sys.argv) > 1 else 'demo.gba', 'wb') as f:
    f.write(rom)
print('wrote', len(rom), 'bytes')
