#!/usr/bin/env python3
"""mktestrom.py - build a synthetic MP2K test cartridge, for testing the engine with no game ROM.

    python -I tools/pcplay/mktestrom.py test.gba

Everything in it is generated here: a SelectSong-shaped routine (so the signature scan finds the
song table), a sound-mode word, a voicegroup and samples, and these songs:

  0  MELODY  one-cycle saw (C4 = 261.63 Hz at key 60) playing A4, C5, E5 against a kick on track 2,
             at 120 BPM, looping with GOTO. With track 2 muted, the first half-second is a pure
             440 Hz saw - pcplay's self-test measures exactly that.
  1  SPLIT   the same saw through a key-split voice and a rhythm (drum) voice.
  2  PSG     a square-wave (CGB) voice - silent in pcplay, which does not render the PSG.
             Entered on music player 1: a sound effect, as far as the SFX filter is concerned.
  3  BLIP    a short one-shot saw note, on music player 2: another sound effect.
  4+ JUNK    headers whose tracks point at random bytes and whose voicegroup is random: the fuzz
             run plays these with random mangles and must never read outside the image.

A SECOND copy of the library (as on a two-games-in-one cart) sits at 0x8000 with its own song
table of three songs and its own sound-mode word (18157 Hz), so the scan must report two tables.
"""
import math
import random
import struct
import sys

B = 0x08000000
SIZE = 0x100000  # 1 MB

rom = bytearray(SIZE)
rom[0xA0:0xAC] = b'MP2K TEST\0\0\0'
rom[0xAC:0xB0] = b'TEST'
rom[0xB2] = 0x96


def w32(off, v):
    struct.pack_into('<I', rom, off, v & 0xFFFFFFFF)


# ---- the library: SelectSong pattern (saptapper's), table pointer at +40, a sound-mode word ----
rom[0x1000:0x101E] = bytes([
    0x00, 0xb5, 0x00, 0x04, 0x07, 0x4a, 0x08, 0x49, 0x40, 0x0b,
    0x40, 0x18, 0x83, 0x88, 0x59, 0x00, 0xc9, 0x18, 0x89, 0x00,
    0x89, 0x18, 0x0a, 0x68, 0x01, 0x68, 0x10, 0x1c, 0x00, 0xf0])
TABLE = 0x2000
w32(0x1028, B + TABLE)
# m4aSoundInit (saptapper's first prologue) with its m4aSoundMode literal at the end of its pool,
# then m4aSoundMain (PUSH {LR}) just before SelectSong, as the library lays them out.
rom[0x0F00:0x0F04] = bytes([0x70, 0xb5, 0x14, 0x48])
w32(0x0FE0, 0x0094C500)        # DA 8 bit, 13379 Hz, master 12, 5 channels
rom[0x0FF0:0x0FF2] = bytes([0x00, 0xb5])
# A decoy before m4aSoundInit that passes every field test (42048 Hz). The old search of the 16 KB
# around SelectSong took the first passing word, i.e. this one; it must not be found.
w32(0x0E00, 0x009CC500)

# ---- samples ----
SAW = 0x10000                  # WaveData: type, flags(0x40 loop), freq, loopStart, size, data
SAW_LEN = 64
w32(SAW, 0x40000000)
w32(SAW + 4, int(261.6256 * SAW_LEN * 1024))     # key 60 plays C4
w32(SAW + 8, 0)
w32(SAW + 12, SAW_LEN)
for i in range(SAW_LEN):
    rom[SAW + 16 + i] = (int(-100 + 200 * i / SAW_LEN)) & 0xFF

KICK = 0x11000                 # one-shot: a decaying low sine-ish thump
KICK_LEN = 3000
w32(KICK, 0)
w32(KICK + 4, 13379 * 1024)
w32(KICK + 8, 0)
w32(KICK + 12, KICK_LEN)
for i in range(KICK_LEN):
    v = 110 * math.exp(-i / 700) * math.sin(2 * math.pi * 60 * i / 13379 * (1 + 2 * math.exp(-i / 150)))
    rom[KICK + 16 + i] = int(v) & 0xFF

SQWAVE = 0x12000               # 4-bit wave table for a CGB wave voice
for i in range(16):
    rom[SQWAVE + i] = 0xFF if i < 8 else 0x00


# ---- voicegroups ----
def tone(off, typ, key, length, pansweep, wav, a, d, s, r):
    rom[off:off + 4] = bytes([typ, key, length, pansweep])
    w32(off + 4, wav)
    rom[off + 8:off + 12] = bytes([a, d, s, r])


VG = 0x3000
SUB = 0x3800                   # key-split / drum sub-voicegroup
SPLIT = 0x3A00                 # key-split table: every key -> sub entry 0
tone(VG + 0 * 12, 0x00, 60, 0, 0, B + SAW, 255, 250, 200, 200)       # DS saw
tone(VG + 1 * 12, 0x01, 60, 0, 0x08, 2, 0, 0, 15, 0)                 # CGB square, 50 % duty
tone(VG + 2 * 12, 0x40, 60, 0, 0, B + SUB, 0, 0, 0, 0)               # key split (table below)
w32(VG + 2 * 12 + 8, B + SPLIT)
tone(SUB + 0 * 12, 0x00, 60, 0, 0, B + SAW, 255, 240, 180, 180)     # what the split resolves to
# drum kit: key 36 -> kick at its own key, panned a little left
DRUMS = 0x3C00
for k in range(128):
    tone(DRUMS + 12 * k, 0x00, 60, 0, 0xC0 - 16, B + KICK, 255, 0, 255, 160)
tone(VG + 3 * 12, 0x80, 60, 0, 0, B + DRUMS, 0, 0, 0, 0)            # rhythm
for k in range(128):
    rom[SPLIT + k] = 0

# ---- track data helpers ----
FINE, GOTO, TEMPO, VOICE, VOL, PAN = 0xB1, 0xB2, 0xBB, 0xBD, 0xBE, 0xBF


def W(t):      # wait t ticks (must be in gClockTable; all used here are)
    return 0x80 + t


def N(t):      # note lasting t ticks
    return 0xCF + t


def track(off, body, loop_from):
    """body: list of ints; a GOTO back to body[loop_from] is appended."""
    data = bytes(body)
    rom[off:off + len(data)] = data
    p = off + len(data)
    rom[p] = GOTO
    w32(p + 1, B + off + loop_from)
    rom[p + 5] = FINE


def header(off, tracks, vg):
    rom[off:off + 4] = bytes([len(tracks), 0, 0, 0])
    w32(off + 4, B + vg)
    for i, t in enumerate(tracks):
        w32(off + 8 + 4 * i, B + t)


# song 0: melody + kick
T0, T1 = 0x5000, 0x5100
track(T0, [VOL, 100, VOICE, 0, TEMPO, 60,
           N(24), 69, 100, W(24), N(12), 72, 100, W(12), N(12), 76, 100, W(12)], 6)
track(T1, [VOL, 110, VOICE, 3,
           N(24), 36, 120, W(24)], 4)
header(0x4000, [T0, T1], VG)

# song 1: key split + drums. Track 1's first note is an INTRO: the loop starts at the second
# (offset 10), so RESTART LOOP has an intro to skip.
T2, T3 = 0x5200, 0x5300
track(T2, [VOL, 100, VOICE, 2, TEMPO, 70, N(12), 64, 90, W(12), N(12), 67, 90, W(12)], 10)
track(T3, [VOL, 100, VOICE, 3, N(6), 36, 120, W(12)], 4)
header(0x4040, [T2, T3], VG)

# song 2: PSG square
T4 = 0x5400
track(T4, [VOL, 100, VOICE, 1, N(24), 60, 100, W(24)], 4)
header(0x4080, [T4], VG)

# song 3: a one-shot blip (an effect: it ends with FINE, no loop)
T5 = 0x5500
rom[T5:T5 + 8] = bytes([VOL, 120, VOICE, 0, N(6), 84, 110, W(6)])
rom[T5 + 8] = FINE
header(0x40C0, [T5], VG)

# junk songs: random track bytes, random voicegroup, in a random-filled region
rng = random.Random(1234)
JUNK = 0x20000
rom[JUNK:JUNK + 0x40000] = bytes(rng.getrandbits(8) for _ in range(0x40000))
songs = [0x4000, 0x4040, 0x4080, 0x40C0]
players = {0x4080: 1, 0x40C0: 2}          # the PSG song and the blip are effects
for j in range(24):
    h = 0x4100 + j * 0x48
    tc = rng.randint(1, 16)
    parts = [JUNK + rng.randrange(0, 0x3FF00) for _ in range(tc)]
    header(h, parts, JUNK + (rng.randrange(0, 0x3F000) & ~3))
    songs.append(h)

for i, h in enumerate(songs):
    ms = players.get(h, 0)
    struct.pack_into('<IHH', rom, TABLE + 8 * i, B + h, ms, ms)
w32(TABLE + 8 * len(songs), 0)

# ---- the second library copy and its table: melody, split, blip ----
rom[0x7F00:0x801E] = rom[0x0F00:0x101E]           # init, main and SelectSong, as above
TABLE2 = 0x9000
w32(0x8028, B + TABLE2)
w32(0x7FE0, 0x0096C800)        # DA 8 bit, 18157 Hz, master 12, 8 channels
for i, (h, ms) in enumerate([(0x4000, 0), (0x4040, 0), (0x40C0, 2)]):
    struct.pack_into('<IHH', rom, TABLE2 + 8 * i, B + h, ms, ms)
w32(TABLE2 + 24, 0)

with open(sys.argv[1] if len(sys.argv) > 1 else 'test.gba', 'wb') as f:
    f.write(rom)
print(f'wrote {len(rom)} bytes, {len(songs)} songs')
