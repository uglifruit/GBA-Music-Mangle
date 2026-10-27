// rom.c — cartridge size detection and header. See rom.h.

#include "rom.h"
#include "gba.h"

#define REG_WAITCNT R16(0x0204)

uint32_t g_romSize = 0;
uint32_t g_romEnd  = ROM_BASE;
char     g_romTitle[13];
char     g_romCode[5];
uint8_t  g_romVersion, g_romCheck;

void rom_set_speed(int speed)
{
    // WS0 first access 3, second access 1, prefetch on (0x4317); or the BIOS's 4/2 (0x4014). SRAM
    // stays at 8 cycles in both - we never touch it.
    REG_WAITCNT = (speed == ROM_SPEED_SAFE) ? 0x4014 : 0x4317;
}

// Past the end of the image the cart bus floats, and a read returns the low 16 bits of the
// halfword address that was put on it: open bus. So "the halfwords here count up from addr/2" is
// the signature of no ROM at addr.
static int open_bus_at(uint32_t addr)
{
    const volatile uint16_t *p = (const volatile uint16_t *)ROM_PTR(addr);
    for (int i = 0; i < 16; i++) {
        if (p[i] != (uint16_t)((addr >> 1) + i)) return 0;
    }
    return 1;
}

static void copy_text(char *out, uint32_t addr, int n)
{
    const uint8_t *p = ROM_PTR(addr);
    for (int i = 0; i < n; i++) {
        uint8_t c = p[i];
        out[i] = (c >= 0x20 && c < 0x7F) ? (char)c : (c == 0 ? ' ' : '.');
    }
    out[n] = 0;
    for (int i = n - 1; i >= 0 && out[i] == ' '; i--) out[i] = 0;
}

int rom_init(void)
{
    rom_set_speed(ROM_SPEED_FAST);

    // The header's fixed byte (0x96 at 0xB2) is the cheapest "is there a cart" test there is, and
    // open bus at the very start is the other half of it.
    g_romSize = 0;
    g_romEnd  = ROM_BASE;
    if (open_bus_at(ROM_BASE) || ROM_PTR(ROM_BASE)[0xB2] != 0x96) {
        g_romTitle[0] = 0;
        g_romCode[0]  = 0;
        g_romVersion  = 0;
        g_romCheck    = 0;
        return 0;
    }

    // Carts come in power-of-two sizes from 1 MB to 32 MB; the image ends at the first one whose
    // next byte is open bus. (Some carts mirror instead of floating; they then report 32 MB, which
    // is merely a longer scan.)
    uint32_t size = ROM_MAX;
    for (uint32_t s = 0x00100000u; s < ROM_MAX; s <<= 1) {
        if (open_bus_at(ROM_BASE + s)) { size = s; break; }
    }
    g_romSize = size;
    g_romEnd  = ROM_BASE + size;

    copy_text(g_romTitle, ROM_BASE + 0xA0, 12);
    copy_text(g_romCode,  ROM_BASE + 0xAC, 4);
    g_romVersion = ROM_PTR(ROM_BASE)[0xBC];
    g_romCheck   = ROM_PTR(ROM_BASE)[0xBD];
    return 1;
}

uint16_t rom_cart_tag(void)
{
    if (!g_romSize) return 0;
    const uint8_t *h = ROM_PTR(ROM_BASE);
    uint32_t t = 0x2545u;
    for (int i = 0xAC; i < 0xB0; i++) t = t * 31u + h[i];
    t = t * 31u + g_romVersion;
    t = t * 31u + g_romCheck;
    t = (t ^ (t >> 16)) & 0xFFFFu;
    return (uint16_t)(t ? t : 1u);
}
