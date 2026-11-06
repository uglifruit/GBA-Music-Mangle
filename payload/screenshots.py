# screenshots.py — render the manual's screenshots by running the REAL payload under emulation.
#
# Adapted from GBA FM Voice's (payload/screenshots.py there). The payload draws in mode 3, so VRAM at
# 0x06000000 IS the 240x160 RGB555 framebuffer: no PPU emulation is needed, only the ARM. unicorn
# runs build/payload.elf from the multiboot entry with a cart image mapped at 0x08000000 (by
# default tools/pcplay/mkdemorom.py's synthetic MP2K cart, so no game's data is in the pictures). The I/O
# registers are a plain register file except the few the payload waits or paces on; a host that has
# said HELLO is faked by writing the link globals; buttons are pressed between ui_frame() calls.
# Timer 2 runs on, so the clock and the sequencer advance and the PLAY page's lanes move; the DMA
# interrupt is never taken, so nothing is mixed (the DSP and SVC meters on CAL are set by hand).
# Run after ./build.sh, whenever the UI changes:
#
#   pip install unicorn pillow
#   python screenshots.py [cart.gba] [out dir]
#       (defaults: ../tools/pcplay/build/demo.gba (python -I ../tools/pcplay/mkdemorom.py), ../docs/images/screens)
import os, shutil, struct, subprocess, sys
from unicorn import *
from unicorn.arm_const import *
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
CART = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'tools', 'pcplay', 'build', 'demo.gba')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', 'docs', 'images', 'screens')
os.makedirs(OUT, exist_ok=True)
NM = shutil.which('arm-none-eabi-nm') or os.path.expanduser('~/.pico-sdk/toolchain/14_2_Rel1/bin/arm-none-eabi-nm.exe')

sym = {}
for ln in subprocess.run([NM, os.path.join(HERE, 'build/payload.elf')], capture_output=True, text=True).stdout.splitlines():
    p = ln.split()
    if len(p) == 3: sym.setdefault(p[2], int(p[0], 16))

cart = open(CART, 'rb').read()
csize = 0x2000000                          # the whole cart space: past the image, open bus
uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
for base, size in [(0, 0x4000), (0x02000000, 0x40000), (0x03000000, 0x8000),
                   (0x05000000, 0x1000), (0x06000000, 0x18000), (0x07000000, 0x1000), (0x08000000, csize)]:
    uc.mem_map(base, size)
uc.mem_write(0x02000000, open(os.path.join(HERE, 'build/payload.mb'), 'rb').read())
import array
ramp = array.array('H', range(0x10000)).tobytes()          # open bus: each halfword is its address / 2
first = (len(cart) // 2) & 0xFFFF
bus = (ramp[first * 2:] + ramp * (csize // 0x20000 + 1))[:csize - len(cart)]
uc.mem_write(0x08000000, cart + bus)

# ---- I/O: plain register file, except the few registers the payload waits or paces on ----
io = bytearray(0x1000)
st = dict(vc=0, tm2=0, tm3=0, keys=0)
def io_read(uc, off, size, ud):
    if off == 0x006:                       # VCOUNT: 8 lines a read, so a frame's waits run the service
        st['vc'] = (st['vc'] + 8) % 228; return st['vc']   # (mod_update, clock) ~28 times
    if off == 0x108:                       # Timer 2 (16384 Hz): the control clock moves on
        st['tm2'] = (st['tm2'] + 16) & 0xFFFF; return st['tm2']
    if off == 0x10C:                       # Timer 3: a service costs ~20 % of its 1 ms
        st['tm3'] = (st['tm3'] + 3277) & 0xFFFF; return st['tm3']
    if off == 0x130:                       # KEYINPUT, active low
        return (~st['keys']) & 0x3FF
    v = int.from_bytes(io[off:off + size], 'little')
    if off == 0x128: v |= 0x80             # SIO start bit stays set: no word ever completes
    return v
def io_write(uc, off, size, val, ud):
    io[off:off + size] = val.to_bytes(size, 'little')
uc.mmio_map(0x04000000, 0x1000, io_read, None, io_write, None)

def w8(n, v, i=0):  uc.mem_write(sym[n] + i, struct.pack('<B', v))
def w16(n, v, i=0): uc.mem_write(sym[n] + 2 * i, struct.pack('<H', v))
def w32(n, v, i=0): uc.mem_write(sym[n] + 4 * i, struct.pack('<I', v))
def r32(n, i=0):    return struct.unpack('<I', uc.mem_read(sym[n] + 4 * i, 4))[0]

# ---- stops: the scan (to fake a talking host), the scan's result screen, and every ui_frame ----
UI = sym['ui_frame']
flags = dict(skip=False, stop=None)
def on_code(uc, addr, size, ud):
    if flags['skip']:
        flags['skip'] = False; return
    flags['stop'] = addr
    uc.emu_stop()
for name in ('rom_init', 'audio_init', 'ui_frame'):
    uc.hook_add(UC_HOOK_CODE, on_code, begin=sym[name], end=sym[name])
uc.hook_add(UC_HOOK_INTR, lambda uc, n, ud: None)

def run_to(name, count=400_000_000):
    flags['skip'] = True
    uc.emu_start(uc.reg_read(UC_ARM_REG_PC), 0, count=count)
    assert flags['stop'] == sym[name], (name, hex(uc.reg_read(UC_ARM_REG_PC)))

def shot(name):
    px = struct.unpack('<%dH' % (240 * 160), uc.mem_read(0x06000000, 240 * 160 * 2))
    img = Image.new('RGB', (240, 160))
    img.putdata([((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31) for c in px])
    img.resize((480, 320), Image.NEAREST).save(os.path.join(OUT, f'screen-{name}.png'))
    print('wrote', name)

uc.reg_write(UC_ARM_REG_CPSR, 0xDF)       # System mode, IRQ/FIQ masked, as the BIOS hands over
uc.reg_write(UC_ARM_REG_SP, 0x03007F00)
uc.emu_start(0x020000C0, 0, count=100_000_000)
assert flags['stop'] == sym['rom_init'], hex(uc.reg_read(UC_ARM_REG_PC))

# A host that has said HELLO and is streaming: the factory Workshop with CV out calibration,
# audio-input edges and the pulse outputs; three saved slots, none of them this cart's (so the boot
# scans); knobs at noon, the switch in the middle.
w16('g_hostCaps', 0xFFFF); w8('g_hostSeen', 1); w32('g_rx', 1)
w16('g_slotMask', 0b0000000000100101); w16('g_slotTagSeen', 0xFFFF)
for i in range(4): w16('g_in', 2048, i)
for i in range(3): w16('g_knob', 2048, i)
w8('g_switch', 1)

run_to('audio_init', 2_000_000_000)       # the scan has run; its result is on screen
shot('scan')
run_to('ui_frame', 2_000_000_000)

def step(keys=0, n=1):
    """Run n frames with `keys` held."""
    for _ in range(n):
        st['keys'] = keys
        w32('g_rx', r32('g_rx') + 33); w32('g_streamRx', r32('g_streamRx') + 33)
        w16('g_dspAvg', 30 * 16); w16('g_dspPeak', 41 * 16)
        w16('g_svcAvg', 22 * 16); w16('g_svcPeak', 38 * 16)
        run_to('ui_frame')

KA, KB, KSEL, KSTART, KR, KL, KU, KD, KRR, KLL = 1, 2, 4, 8, 16, 32, 64, 128, 256, 512
def press(k, hold=0):
    step(hold | k); step(hold); step(hold)

step(0, 4); shot('play-cued')
press(KA); step(0, 90); shot('play')                 # A on the SONG row: PLAY
press(KSEL); step(0, 4); shot('play-perform')        # SELECT: PERFORM (the buttons are the BTN map)
press(KSEL); step(0, 2)
press(KSTART); step(0, 3)                            # START: the edit pages, from MAP
for name in ['map', 'btn', 'base', 'set', 'mem', 'cal']:
    step(0, 3); shot(name)
    press(KRR)                                       # R: the next page
