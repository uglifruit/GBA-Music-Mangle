// pcplay.c — render an MP2K song to a WAV on the PC, with the payload's own engine.
//
// The scan, the sequencer, the voice logic and the mixer are the payload's files, compiled for the
// host under -DHOST (see payload/gba.h and rom.h). Only the timing loop is this file's: one
// 16-sample mixer block at a time, with the song's tempo accumulated exactly as clock.c does it
// and the ~60 Hz envelope frame beside it.
//
// The PSG ("CGB") voices write to a fake register file here and are NOT heard in the WAV - a song
// that leans on squares and noise will sound thin. Everything else is the real thing.
//
//   pcplay ROM [--list] [--song N] [--secs S] [--out FILE.wav] [--mute MASK]
//              [--voice-shift N] [--table BYTES] [--start 0..255] [--scrub BYTES]
//              [--reverse 0..255] [--transpose SEMIS] [--tempo PCT]
//   pcplay ROM --fuzz N        every song, N seconds each, random mangles every 250 ms
//
// Every cart read is checked (host_rom_ptr); a read outside the image is reported and fails the run.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba.h"
#include "rom.h"
#include "scan.h"
#include "player.h"
#include "mixer.h"

uint8_t g_hostIo[0x400];
static uint8_t *s_rom;
static int      s_strict;            // set once rom_init has finished probing past the end
static unsigned long s_badReads;

const uint8_t *host_rom_ptr(uint32_t a)
{
    if (a < ROM_BASE || a >= ROM_BASE + ROM_MAX) {
        fprintf(stderr, "FATAL: cart read at 0x%08X is outside the cart's address space\n", a);
        exit(3);
    }
    // One byte past the end is legal: the mixer interpolates one sample ahead and its sample bounds
    // include that byte (rom_ok(data, size + 1)).
    if (s_strict && a > g_romEnd && s_badReads++ < 10)
        fprintf(stderr, "BAD READ: 0x%08X is past the image end 0x%08X\n", a, g_romEnd);
    return s_rom + (a - ROM_BASE);
}

// ---- WAV -----------------------------------------------------------------------------------------

static void put32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xFF, f); }
static void put16(FILE *f, uint16_t v) { fputc(v & 0xFF, f); fputc(v >> 8, f); }

static void wav_header(FILE *f, uint32_t frames)
{
    fwrite("RIFF", 1, 4, f); put32(f, 36 + frames * 4); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, AUDIO_RATE); put32(f, AUDIO_RATE * 4);
    put16(f, 4); put16(f, 16); fwrite("data", 1, 4, f); put32(f, frames * 4);
}

// ---- the timing loop (clock.c's INT mode, per audio block) ---------------------------------------

#define FRAME_MILLI 274312u
#define TICK_ONE    (1u << 24)

static uint32_t s_rng = 0x12345678u;
static uint32_t rnd(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }

// Mixer bounds: every active channel's sample must lie inside the image (plus the one byte).
static void check_mixer(void)
{
    for (int c = 0; c < MIX_CHANNELS; c++) {
        const MixChan *m = &g_mix[c];
        if (!m->active) continue;
        long off = (long)((const uint8_t *)m->data - s_rom);
        long end = off + (long)(m->endQ >> 12) + 1;
        if (off < 0 || end > (long)g_romSize + 1) {
            if (s_badReads++ < 10)
                fprintf(stderr, "BAD SAMPLE: channel %d spans 0x%08lX..0x%08lX, image ends 0x%08X\n",
                        c, (unsigned long)ROM_BASE + off, (unsigned long)ROM_BASE + end, g_romEnd);
        }
    }
}

typedef struct { int16_t *buf; uint32_t frames, cap; double sumSq; uint32_t peak; } Render;

static void render(Render *r, uint32_t blocks, int fuzz)
{
    static uint32_t tickAcc, frameAcc;
    int8_t L[AUDIO_BLOCK], R[AUDIO_BLOCK];
    for (uint32_t b = 0; b < blocks; b++) {
        if (fuzz && (b & 255) == 0) {
            g_mods.voiceShift    = (int16_t)((int)(rnd() % 129) - 64);
            g_mods.tableOffset   = (int16_t)((int)(rnd() % 4097) - 2048);
            g_mods.sampleStart   = (uint8_t)rnd();
            g_mods.reverseChance = (uint8_t)rnd();
            g_mods.romScrub      = (rnd() & 1) ? (int32_t)(rnd() % 0x2000000u) - 0x1000000 : 0;
            g_mods.scrubLen      = 1024u << (rnd() % 7);
            g_mods.mute          = (uint16_t)rnd();
            g_mods.pitch         = (int16_t)((int)(rnd() % 12289) - 6144);
            g_mods.tempoU        = (uint16_t)(64 + rnd() % 960);
            g_plMaxChans         = (uint8_t)(1 + rnd() % PL_DS_MAX);
        }
        tickAcc += AUDIO_BLOCK * player_tempo_i() * 408u;
        for (int n = 0; tickAcc >= TICK_ONE && n < 8; n++) { tickAcc -= TICK_ONE; player_tick(); }
        if (tickAcc >= TICK_ONE) tickAcc = TICK_ONE - 1;
        frameAcc += AUDIO_BLOCK * 1000u;
        while (frameAcc >= FRAME_MILLI) { frameAcc -= FRAME_MILLI; player_frame(); }
        player_update();
        mixer_render(L, R);
        check_mixer();
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            if (r->buf && r->frames < r->cap) {
                r->buf[r->frames * 2]     = (int16_t)(L[i] * 256);
                r->buf[r->frames * 2 + 1] = (int16_t)(R[i] * 256);
            }
            r->frames++;
            int32_t a = L[i] < 0 ? -L[i] : L[i];
            if ((uint32_t)a > r->peak) r->peak = (uint32_t)a;
            r->sumSq += (double)L[i] * L[i] + (double)R[i] * R[i];
        }
    }
}

// ---- main ----------------------------------------------------------------------------------------

static long argl(int argc, char **argv, const char *name, long def)
{
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], name)) return strtol(argv[i + 1], 0, 0);
    return def;
}
static const char *args(int argc, char **argv, const char *name, const char *def)
{
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], name)) return argv[i + 1];
    return def;
}
static int flag(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], name)) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: pcplay ROM [--list] [--song N] [--secs S] [--out F.wav] [--fuzz SECS] ...\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    s_rom = malloc(ROM_MAX + 64);
    size_t len = fread(s_rom, 1, ROM_MAX, f);
    fclose(f);
    // Past the image the cart bus floats: each halfword reads as its own halfword address. That is
    // what rom_init's size detection looks for, so model it.
    for (size_t o = (len + 1) & ~(size_t)1; o + 1 < ROM_MAX + 64; o += 2) {
        uint16_t v = (uint16_t)(o >> 1);
        s_rom[o] = (uint8_t)v;
        s_rom[o + 1] = (uint8_t)(v >> 8);
    }

    if (!rom_init()) { fprintf(stderr, "no cartridge header (0x96 at 0xB2)\n"); return 1; }
    s_strict = 1;
    scan_run(NULL);
    static const char *meth[3] = { "NONE", "SIGNATURE", "HEURISTIC" };
    printf("%s [%s]  %u KB  scan %s  table 0x%08X  songs %u  rate %u\n", g_romTitle, g_romCode,
           g_romSize >> 10, meth[g_scan.method], g_scan.songTable, g_songCount, g_scan.gameRate);
    if (g_scan.method == SCAN_NONE) return 1;

    if (flag(argc, argv, "--list")) {
        for (int n = 0; n < g_songCount; n++) {
            uint32_t e = scan_song_entry(n), hdr = rom_u32(e);
            printf("%4d  entry %4u  header 0x%08X  tracks %u\n", n, g_songIx[n], hdr, rom_u8(hdr));
        }
        return 0;
    }

    player_init();
    g_plGameRate = g_scan.gameRate;

    long fuzz = argl(argc, argv, "--fuzz", 0);
    if (fuzz > 0) {
        for (int n = 0; n < g_songCount; n++) {
            Render r = { 0 };
            player_start(scan_song_entry(n));
            render(&r, (uint32_t)(fuzz * AUDIO_RATE / AUDIO_BLOCK), 1);
            printf("song %4d  ticks %6u  peak %3u\n", n, g_plTicks, r.peak);
        }
        printf("fuzz done: %lu bad reads\n", s_badReads);
        return s_badReads ? 1 : 0;
    }

    int song = (int)argl(argc, argv, "--song", 0);
    if (!player_start(scan_song_entry(song))) { fprintf(stderr, "song %d is not playable\n", song); return 1; }
    g_mods.mute          = (uint16_t)argl(argc, argv, "--mute", 0);
    g_mods.voiceShift    = (int16_t)argl(argc, argv, "--voice-shift", 0);
    g_mods.tableOffset   = (int16_t)argl(argc, argv, "--table", 0);
    g_mods.sampleStart   = (uint8_t)argl(argc, argv, "--start", 0);
    g_mods.romScrub      = (int32_t)argl(argc, argv, "--scrub", 0);
    g_mods.reverseChance = (uint8_t)argl(argc, argv, "--reverse", 0);
    g_mods.pitch         = (int16_t)(argl(argc, argv, "--transpose", 0) * 256);
    g_mods.tempoU        = (uint16_t)(argl(argc, argv, "--tempo", 100) * 256 / 100);

    double secs = (double)argl(argc, argv, "--secs", 10);
    Render r = { 0 };
    r.cap = (uint32_t)(secs * AUDIO_RATE);
    r.buf = calloc(r.cap * 2, sizeof(int16_t));
    render(&r, (r.cap + AUDIO_BLOCK - 1) / AUDIO_BLOCK, 0);

    const char *out = args(argc, argv, "--out", "out.wav");
    FILE *w = fopen(out, "wb");
    if (!w) { perror(out); return 2; }
    wav_header(w, r.cap);
    fwrite(r.buf, sizeof(int16_t), r.cap * 2, w);
    fclose(w);
    printf("wrote %s: %.1f s, peak %u/128, ticks %u, beats %u, loops %u, bad reads %lu\n", out, secs,
           r.peak, g_plTicks, g_plBeats, g_plLoops, s_badReads);
    return s_badReads ? 1 : 0;
}
