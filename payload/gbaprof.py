# gbaprof.py CART SONG SECS [KNOB_X]: run the real payload under unicorn with a cycle model, play SONG
# (1-based cue) for SECS emulated seconds, and report where the GBA's time goes: the tick rate (is the
# clock keeping up?), UI frames a second (do the buttons answer?), the mixer's share, a flat profile,
# and (FOCUS=function, default ms_tick) that function's costliest blocks. Line numbers need a build
# with -g added to build.sh's CFLAGS (it does not change the code). pip install unicorn. Slow: about
# a minute an emulated second. Use your own cart dumps.
# Model: ARM fetch EWRAM 6, IWRAM 1; +2 fetches per block (branch refill); data EWRAM 3/6, ROM 4/6
# (3/1 waits), IWRAM/IO/VRAM 1. Timer 2, Timer 3 and VCOUNT follow the cycle count. The DMA2
# interrupt is replayed at frame stops: one mixer_render per 16384 cycles owed, its cycles added.
import os, sys, struct, subprocess, bisect, array
from unicorn import *
from unicorn.arm_const import *
PAY = os.path.dirname(os.path.abspath(__file__))
NM = os.path.expanduser('~/.pico-sdk/toolchain/14_2_Rel1/bin/arm-none-eabi-nm.exe')
CART, SONG, SECS = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
KNOBX = int(sys.argv[4]) if len(sys.argv) > 4 else 2048
sym, funcs = {}, []
for ln in subprocess.run([NM, '-S', os.path.join(PAY, 'build/payload.elf')], capture_output=True, text=True).stdout.splitlines():
    p = ln.split()
    if len(p) == 4:
        sym.setdefault(p[3], int(p[0], 16))
        if p[2] in 'tTwW': funcs.append((int(p[0], 16), int(p[1], 16), p[3]))
    elif len(p) == 3: sym.setdefault(p[2], int(p[0], 16))
funcs.sort(); starts = [f[0] for f in funcs]
def fname(a):
    i = bisect.bisect_right(starts, a) - 1
    return funcs[i][2] if i >= 0 and a < funcs[i][0] + funcs[i][1] else '?%x' % (a >> 16)

cart = open(CART, 'rb').read()
csize = 0x2000000
uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
for base, size in [(0, 0x4000), (0x02000000, 0x40000), (0x03000000, 0x8000),
                   (0x05000000, 0x1000), (0x06000000, 0x18000), (0x07000000, 0x1000), (0x08000000, csize)]:
    uc.mem_map(base, size)
uc.mem_write(0x02000000, open(os.path.join(PAY, 'build/payload.mb'), 'rb').read())
ramp = array.array('H', range(0x10000)).tobytes()
first = (len(cart) // 2) & 0xFFFF
bus = (ramp[first * 2:] + ramp * (csize // 0x20000 + 1))[:csize - len(cart)]
uc.mem_write(0x08000000, cart + bus)

C = dict(cyc=0, keys=0, prof=False)
io = bytearray(0x1000)
def io_read(uc, off, size, ud):
    c = C['cyc']
    if off == 0x006: return (c // 1232) % 228
    if off == 0x108: return (c >> 10) & 0xFFFF
    if off == 0x10C: return c & 0xFFFF
    if off == 0x130: return (~C['keys']) & 0x3FF
    v = int.from_bytes(io[off:off + size], 'little')
    if off == 0x128: v |= 0x80
    return v
def io_write(uc, off, size, val, ud): io[off:off + size] = val.to_bytes(size, 'little')
uc.mmio_map(0x04000000, 0x1000, io_read, None, io_write, None)

FETCH = {0x02: 6, 0x03: 1}
cur = ['boot']; lastb = [0]; blk = {}; FOCUS = os.environ.get('FOCUS', 'ms_tick')
cost = {}
def on_block(uc, addr, size, ud):
    f = FETCH.get(addr >> 24, 1)
    c = (size // 4 + 2) * f
    C['cyc'] += c
    if C['prof']:
        n = fname(addr); cur[0] = n; lastb[0] = addr
        cost[n] = cost.get(n, 0) + c
        if n == FOCUS: blk[addr] = blk.get(addr, 0) + c
def on_mem(uc, access, addr, size, value, ud):
    r = addr >> 24
    if r == 0x02: c = 6 if size == 4 else 3
    elif 0x08 <= r <= 0x0D: c = 6 if size == 4 else 4
    else: c = 1
    C['cyc'] += c
    if C['prof']:
        cost[cur[0]] = cost.get(cur[0], 0) + c
        if cur[0] == FOCUS: blk[lastb[0]] = blk.get(lastb[0], 0) + c
uc.hook_add(UC_HOOK_BLOCK, on_block)
uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, on_mem)

calls = {}
def on_call(uc, addr, size, ud):
    n = fname(addr); calls[n] = calls.get(n, 0) + 1
for n in ('ms_tick', 'ms_frame', 'ms_update', 'mod_update', 'clock_service', 'ui_frame', 'macro_run',
          'player_tick', 'player_frame', 'gfx_idle'):
    if n in sym: uc.hook_add(UC_HOOK_CODE, on_call, begin=sym[n], end=sym[n])

flags = dict(skip=False, stop=None)
def on_stop(uc, addr, size, ud):
    if flags['skip']: flags['skip'] = False; return
    flags['stop'] = addr; uc.emu_stop()
for name in ('rom_init', 'audio_init', 'ui_frame'):
    uc.hook_add(UC_HOOK_CODE, on_stop, begin=sym[name], end=sym[name])
uc.hook_add(UC_HOOK_INTR, lambda uc, n, ud: None)
def w8(n, v, i=0):  uc.mem_write(sym[n] + i, struct.pack('<B', v))
def w16(n, v, i=0): uc.mem_write(sym[n] + 2 * i, struct.pack('<H', v))
def w32(n, v, i=0): uc.mem_write(sym[n] + 4 * i, struct.pack('<I', v))
def r32(n, i=0):    return struct.unpack('<I', uc.mem_read(sym[n] + 4 * i, 4))[0]
def r16(n, i=0):    return struct.unpack('<H', uc.mem_read(sym[n] + 2 * i, 2))[0]
def run_to(name, count=4_000_000_000):
    flags['skip'] = True
    uc.emu_start(uc.reg_read(UC_ARM_REG_PC), 0, count=count)
    assert flags['stop'] == sym[name], (name, hex(uc.reg_read(UC_ARM_REG_PC)))

uc.reg_write(UC_ARM_REG_CPSR, 0xDF); uc.reg_write(UC_ARM_REG_SP, 0x03007F00)
uc.emu_start(0x020000C0, 0, count=100_000_000)
w16('g_hostCaps', 0xFFFF); w8('g_hostSeen', 1); w32('g_rx', 1)
w16('g_slotMask', 0); w16('g_slotTagSeen', 0xFFFF)
for i in range(4): w16('g_in', 2048, i)
for i in range(3): w16('g_knob', 2048, i)
w16('g_knob', KNOBX, 1)
w8('g_switch', 1)
run_to('audio_init')
run_to('ui_frame')

# The mixer, called as the DMA2 interrupt would be: its own stack, back to a stop address.
MIX = sym['mixer_render']; RET = 0x3F00
uc.mem_write(RET, b'\xfe\xff\xff\xea')
mixc = dict(owed=C['cyc'], n=0, cyc=0)
def mixer_catchup():
    while C['cyc'] - mixc['owed'] >= 16384:
        mixc['owed'] += 16384
        regs = [uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
                UC_ARM_REG_R12, UC_ARM_REG_LR, UC_ARM_REG_SP, UC_ARM_REG_PC, UC_ARM_REG_CPSR)]
        uc.reg_write(UC_ARM_REG_R0, sym['g_ringL']); uc.reg_write(UC_ARM_REG_R1, sym['g_ringR'])
        uc.reg_write(UC_ARM_REG_LR, RET); uc.reg_write(UC_ARM_REG_SP, 0x03007E00)
        was = cur[0]; c0 = C['cyc']
        uc.emu_start(MIX, RET, count=10_000_000)
        cur[0] = was
        mixc['n'] += 1; mixc['cyc'] += C['cyc'] - c0
        for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R12,
                         UC_ARM_REG_LR, UC_ARM_REG_SP, UC_ARM_REG_PC, UC_ARM_REG_CPSR), regs): uc.reg_write(r, v)

def step(keys=0, n=1):
    for _ in range(n):
        C['keys'] = keys
        w32('g_rx', r32('g_rx') + 33); w32('g_streamRx', r32('g_streamRx') + 33)
        mixer_catchup()
        run_to('ui_frame')
def press(k): step(k); step(0); step(0)
KA, KR = 1, 16
step(0, 4)
for _ in range(SONG - 1): press(KR)
press(KA)
step(0, 20)
# Measure.
t0 = C['cyc']; ticks0 = r32('g_plTicks') if 'g_plTicks' in sym else 0
calls.clear(); cost.clear(); mixc['n'] = 0; mixc['cyc'] = 0; C['prof'] = True
frames = 0
while C['cyc'] - t0 < SECS * 16777216:
    step(0); frames += 1
C['prof'] = False
el = (C['cyc'] - t0) / 16777216
ticks = r32('g_plTicks') - ticks0
print('song %d: %.2f s emulated, ui_frames %d (%.1f/s), ticks %d (%.1f/s), clock BPM field %d, playing %d' % (
    SONG, el, frames, frames / el, ticks, ticks / el, r16('g_clkBpm'), struct.unpack('<i', uc.mem_read(sym['g_songPlaying'], 4))[0] if 'g_songPlaying' in sym else -9))
tot = C['cyc'] - t0
print('mixer: %d calls, %.0f cycles each, %.1f %% of the CPU' % (mixc['n'], mixc['cyc'] / max(1, mixc['n']), 100 * mixc['cyc'] / tot))
print('calls/s:', ', '.join('%s %.0f' % (k, v / el) for k, v in sorted(calls.items())))
print('flat profile (%% of all cycles):')
for k, v in sorted(cost.items(), key=lambda x: -x[1])[:28]:
    print('  %5.1f %%  %s' % (100 * v / tot, k))

A2L = os.path.expanduser('~/.pico-sdk/toolchain/14_2_Rel1/bin/arm-none-eabi-addr2line.exe')
top = sorted(blk.items(), key=lambda x: -x[1])[:25]
print('inside', FOCUS)
for (a, v) in top:
    l = subprocess.run([A2L, '-e', os.path.join(PAY, 'build/payload.elf')] + ['%x' % a], capture_output=True, text=True).stdout.strip()
    print('  %5.1f %%  %08x  %s' % (100 * v / tot, a, l.replace(chr(92), '/').split('/')[-1]))
