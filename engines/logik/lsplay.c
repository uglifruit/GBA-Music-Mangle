// lsplay.c — Logik State "GBAModPlay" modules (see lsplay.h).
//
// Written from a reading of the driver in Super Puzzle Fighter II Turbo (Logik State 2002; its row
// reader 0x0834902C, cell processor 0x08349ACC, tick effects 0x08349848, pitch 0x08348444 /
// 0x08348488, render loop 0x08349124 and ARM mixer 0x08349FB8). It is a ProTracker / FastTracker
// player cut down: what the driver does is done here, in its order, with its quirks, except where
// noted (its pattern loop and break into a row continue the run-length streams where they were, so
// they would play the wrong rows: here they seek).
//
// PITCH. Linear mode (every Puzzle Fighter module): period = 7616 - (relative note + note - 2) x 64
// - finetune / 2 + the portamento offset; frequency = T[x mod 768] x 4 >> (7 - x / 768), x = 7680 -
// period, T[i] = 16726 x 2^(i / 768) (FastTracker 2's linear table, generated here; it matches the
// driver's 768 entries). Amiga mode: the period of a 1/8-semitone table (generated; ProTracker's own
// is hand-tuned, within 3), frequency = 7093789 / (period x 2).
// LEVEL. volume (0..64) x the game's music volume >> 6 (the driver also scales by 64 >> 6 twice: a
// master and a fade, both full here); each voice adds sample x level >> 8, mono.
#include "lsplay.h"

static const uint16_t kFreq[768] = {
    16726,16741,16756,16771,16786,16801,16816,16832,16847,16862,16877,16892,16908,16923,16938,16953,
    16969,16984,16999,17015,17030,17046,17061,17076,17092,17107,17123,17138,17154,17169,17185,17200,
    17216,17231,17247,17262,17278,17293,17309,17325,17340,17356,17372,17387,17403,17419,17435,17450,
    17466,17482,17498,17513,17529,17545,17561,17577,17593,17608,17624,17640,17656,17672,17688,17704,
    17720,17736,17752,17768,17784,17800,17816,17832,17848,17865,17881,17897,17913,17929,17945,17962,
    17978,17994,18010,18027,18043,18059,18075,18092,18108,18124,18141,18157,18174,18190,18206,18223,
    18239,18256,18272,18289,18305,18322,18338,18355,18372,18388,18405,18421,18438,18455,18471,18488,
    18505,18521,18538,18555,18572,18588,18605,18622,18639,18656,18672,18689,18706,18723,18740,18757,
    18774,18791,18808,18825,18842,18859,18876,18893,18910,18927,18944,18961,18978,18995,19013,19030,
    19047,19064,19081,19099,19116,19133,19150,19168,19185,19202,19220,19237,19254,19272,19289,19306,
    19324,19341,19359,19376,19394,19411,19429,19446,19464,19482,19499,19517,19534,19552,19570,19587,
    19605,19623,19640,19658,19676,19694,19711,19729,19747,19765,19783,19801,19819,19836,19854,19872,
    19890,19908,19926,19944,19962,19980,19998,20016,20034,20052,20071,20089,20107,20125,20143,20161,
    20179,20198,20216,20234,20252,20271,20289,20307,20326,20344,20362,20381,20399,20418,20436,20455,
    20473,20492,20510,20529,20547,20566,20584,20603,20621,20640,20659,20677,20696,20715,20733,20752,
    20771,20790,20808,20827,20846,20865,20884,20902,20921,20940,20959,20978,20997,21016,21035,21054,
    21073,21092,21111,21130,21149,21168,21187,21206,21226,21245,21264,21283,21302,21322,21341,21360,
    21379,21399,21418,21437,21457,21476,21496,21515,21534,21554,21573,21593,21612,21632,21651,21671,
    21690,21710,21730,21749,21769,21789,21808,21828,21848,21867,21887,21907,21927,21946,21966,21986,
    22006,22026,22046,22066,22086,22105,22125,22145,22165,22185,22205,22226,22246,22266,22286,22306,
    22326,22346,22366,22387,22407,22427,22447,22468,22488,22508,22528,22549,22569,22590,22610,22630,
    22651,22671,22692,22712,22733,22753,22774,22794,22815,22836,22856,22877,22897,22918,22939,22960,
    22980,23001,23022,23043,23063,23084,23105,23126,23147,23168,23189,23210,23230,23251,23272,23293,
    23315,23336,23357,23378,23399,23420,23441,23462,23483,23505,23526,23547,23568,23590,23611,23632,
    23654,23675,23696,23718,23739,23761,23782,23804,23825,23847,23868,23890,23911,23933,23954,23976,
    23998,24019,24041,24063,24084,24106,24128,24150,24172,24193,24215,24237,24259,24281,24303,24325,
    24347,24369,24391,24413,24435,24457,24479,24501,24523,24545,24567,24590,24612,24634,24656,24679,
    24701,24723,24746,24768,24790,24813,24835,24857,24880,24902,24925,24947,24970,24992,25015,25038,
    25060,25083,25105,25128,25151,25174,25196,25219,25242,25265,25287,25310,25333,25356,25379,25402,
    25425,25448,25471,25494,25517,25540,25563,25586,25609,25632,25655,25678,25702,25725,25748,25771,
    25795,25818,25841,25864,25888,25911,25935,25958,25981,26005,26028,26052,26075,26099,26123,26146,
    26170,26193,26217,26241,26264,26288,26312,26336,26359,26383,26407,26431,26455,26479,26502,26526,
    26550,26574,26598,26622,26646,26670,26695,26719,26743,26767,26791,26815,26839,26864,26888,26912,
    26937,26961,26985,27010,27034,27058,27083,27107,27132,27156,27181,27205,27230,27254,27279,27304,
    27328,27353,27378,27402,27427,27452,27477,27502,27526,27551,27576,27601,27626,27651,27676,27701,
    27726,27751,27776,27801,27826,27851,27876,27902,27927,27952,27977,28003,28028,28053,28078,28104,
    28129,28155,28180,28205,28231,28256,28282,28307,28333,28359,28384,28410,28435,28461,28487,28513,
    28538,28564,28590,28616,28642,28667,28693,28719,28745,28771,28797,28823,28849,28875,28901,28927,
    28953,28980,29006,29032,29058,29084,29111,29137,29163,29190,29216,29242,29269,29295,29322,29348,
    29375,29401,29428,29454,29481,29507,29534,29561,29587,29614,29641,29668,29694,29721,29748,29775,
    29802,29829,29856,29883,29910,29937,29964,29991,30018,30045,30072,30099,30126,30154,30181,30208,
    30235,30263,30290,30317,30345,30372,30400,30427,30454,30482,30509,30537,30565,30592,30620,30647,
    30675,30703,30731,30758,30786,30814,30842,30870,30897,30925,30953,30981,31009,31037,31065,31093,
    31121,31149,31178,31206,31234,31262,31290,31319,31347,31375,31403,31432,31460,31489,31517,31546,
    31574,31602,31631,31660,31688,31717,31745,31774,31803,31832,31860,31889,31918,31947,31975,32004,
    32033,32062,32091,32120,32149,32178,32207,32236,32265,32295,32324,32353,32382,32411,32441,32470,
    32499,32529,32558,32587,32617,32646,32676,32705,32735,32764,32794,32823,32853,32883,32912,32942,
    32972,33002,33031,33061,33091,33121,33151,33181,33211,33241,33271,33301,33331,33361,33391,33421
};
static const uint16_t kAmiga[512] = {
    1814,1801,1788,1775,1762,1749,1737,1724,1712,1700,1687,1675,1663,1651,1639,1628,
    1616,1604,1593,1581,1570,1559,1547,1536,1525,1514,1503,1493,1482,1471,1461,1450,
    1440,1429,1419,1409,1399,1389,1379,1369,1359,1349,1339,1330,1320,1311,1301,1292,
    1283,1273,1264,1255,1246,1237,1228,1219,1211,1202,1193,1185,1176,1168,1159,1151,
    1143,1134,1126,1118,1110,1102,1094,1086,1078,1071,1063,1055,1048,1040,1033,1025,
    1018,1011,1003,996,989,982,975,968,961,954,947,940,933,927,920,913,
    907,900,894,887,881,875,868,862,856,850,844,838,832,826,820,814,
    808,802,796,791,785,779,774,768,763,757,752,746,741,736,730,725,
    720,715,709,704,699,694,689,684,679,675,670,665,660,655,651,646,
    641,637,632,628,623,619,614,610,605,601,597,592,588,584,580,575,
    571,567,563,559,555,551,547,543,539,535,532,528,524,520,516,513,
    509,505,502,498,494,491,487,484,480,477,474,470,467,463,460,457,
    453,450,447,444,441,437,434,431,428,425,422,419,416,413,410,407,
    404,401,398,395,392,390,387,384,381,379,376,373,370,368,365,363,
    360,357,355,352,350,347,345,342,340,337,335,332,330,328,325,323,
    321,318,316,314,312,309,307,305,303,300,298,296,294,292,290,288,
    286,284,282,280,278,276,274,272,270,268,266,264,262,260,258,256,
    254,253,251,249,247,245,244,242,240,238,237,235,233,232,230,228,
    227,225,223,222,220,219,217,216,214,212,211,209,208,206,205,203,
    202,201,199,198,196,195,193,192,191,189,188,187,185,184,183,181,
    180,179,177,176,175,174,172,171,170,169,167,166,165,164,163,161,
    160,159,158,157,156,155,154,152,151,150,149,148,147,146,145,144,
    143,142,141,140,139,138,137,136,135,134,133,132,131,130,129,128,
    127,126,125,125,124,123,122,121,120,119,118,118,117,116,115,114,
    113,113,112,111,110,109,109,108,107,106,105,105,104,103,102,102,
    101,100,100,99,98,97,97,96,95,95,94,93,93,92,91,91,
    90,89,89,88,87,87,86,86,85,84,84,83,83,82,81,81,
    80,80,79,78,78,77,77,76,76,75,75,74,74,73,72,72,
    71,71,70,70,69,69,68,68,67,67,66,66,65,65,65,64,
    64,63,63,62,62,61,61,60,60,60,59,59,58,58,58,57,
    57,56,56,55,55,55,54,54,54,53,53,52,52,52,51,51,
    50,50,50,49,49,49,48,48,48,47,47,47,46,46,46,45
};
static const int16_t kSine[32] = {0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,255,253,250,244,235,224,212,
    197,180,161,141,120,97,74,49,24};   // ProTracker's vibrato sine, half a cycle (the rest is negated)

#define NONE 0x1FFu

static void zero(void *v, unsigned n) { volatile uint8_t *p = v; while (n--) *p++ = 0; }
static int clamp(int x, int l, int h) { return x < l ? l : x > h ? h : x; }
static int ok(const LsRom *r, uint32_t a, uint32_t n)
{
    return r->data && a >= r->base && a - r->base <= r->size && n <= r->size - (a - r->base);
}
static uint8_t b(const LsRom *r, uint32_t a) { return r->data[a - r->base]; }
static uint16_t h(const LsRom *r, uint32_t a) { return (uint16_t)(b(r, a) | (b(r, a + 1) << 8)); }
static uint32_t w(const LsRom *r, uint32_t a) { return h(r, a) | ((uint32_t)h(r, a + 2) << 16); }
static uint16_t hbe(const LsRom *r, uint32_t a) { return (uint16_t)((b(r, a) << 8) | b(r, a + 1)); }
static uint32_t divide(uint32_t n, uint32_t d)
{
    uint32_t q = 0, r = 0;
    if (!d) return 0;
    for (int i = 31; i >= 0; --i) { r = (r << 1) | ((n >> i) & 1); if (r >= d) { r -= d; q |= 1u << i; } }
    return q;
}
static void event(LsPlayer *p, unsigned e, uint32_t v) { if (p->hooks.event) p->hooks.event(p->hooks.user, e, v); }
static void unsupported(LsPlayer *p, unsigned fx) { p->unsupported |= 1u << (fx & 31); event(p, LS_EVENT_UNSUPPORTED, fx); }
static void off(LsPlayer *p, unsigned c)
{
    LsTrack *t = &p->tracks[c];
    t->active = t->gate = 0;
    if (p->hooks.stop) p->hooks.stop(p->hooks.user, c);
}
void ls_stop(LsPlayer *p)
{
    for (unsigned c = 0; c < LS_CHANNELS; ++c) off(p, c);
    p->running = 0;
    event(p, LS_EVENT_STOP, 0);
}
static int fail(LsPlayer *p, uint32_t a) { p->error = 1; ls_stop(p); event(p, LS_EVENT_BAD_DATA, a); return 0; }
static unsigned random8(LsPlayer *p) { p->rng ^= p->rng << 13; p->rng ^= p->rng >> 17; p->rng ^= p->rng << 5; return p->rng & 255; }

void ls_init(LsPlayer *p, LsRom r, const LsHooks *hooks)
{
    zero(p, sizeof(*p));
    p->rom.data = r.data; p->rom.base = r.base; p->rom.size = r.size;
    if (!r.data || r.size > 0x2000000 || r.base > UINT32_MAX - r.size) p->rom.size = 0;
    if (hooks) {
        p->hooks.user = hooks->user; p->hooks.start = hooks->start; p->hooks.update = hooks->update;
        p->hooks.stop = hooks->stop; p->hooks.done = hooks->done; p->hooks.note = hooks->note; p->hooks.event = hooks->event;
    }
    p->mods.tempo = 256;
    p->repeat = 1;
    p->rng = 0x4C535059;
    p->music_volume = 90;
}

// ---- the module ----

// Two layouts carry the same magic. Version 1 (2002, Puzzle Fighter): +0x28 channels, +0x38 orders,
// +0x1B1C patterns, +0x1B20 rows, +0x1B24 Amiga, +0x1B28 BPM, +0x1B2C speed, +0x1B30 the order list,
// +0x1C30 each pattern's size, patterns back to back from +0x2034. Version 2 (2003, Driv3r): +0x28
// channels, +0x34 orders, +0x13C rows, +0x140 Amiga, +0x144 BPM, +0x148 speed, +0x14C the order list,
// +0x24C each pattern's offset from +0x650, where the patterns are (no count: the order list's largest).
typedef struct { uint32_t ch, orders, np, rows, amiga, bpm, speed, order, table, data; uint8_t v; } Layout;

static int layout(const LsRom *r, uint32_t m, Layout *L)
{
    if ((m & 3) || !ok(r, m, 0x650) || w(r, m) != 0x4D414247u || w(r, m + 4) != 0x3033444Fu) return 0;
    int v2 = w(r, m + 0x13C) - 1u < 256u && w(r, m + 0x140) <= 1 && w(r, m + 0x144) - 32u < 224u && w(r, m + 0x148) - 1u < 31u;
    if (!v2 && ok(r, m, 0x2034) && w(r, m + 0x38) - 1u < 256u) {
        L->v = 1; L->ch = w(r, m + 0x28); L->orders = w(r, m + 0x38); L->np = w(r, m + 0x1B1C);
        L->rows = w(r, m + 0x1B20); L->amiga = w(r, m + 0x1B24); L->bpm = w(r, m + 0x1B28);
        L->speed = w(r, m + 0x1B2C); L->order = m + 0x1B30; L->table = m + 0x1C30; L->data = m + 0x2034;
    } else {
        L->v = 2; L->ch = w(r, m + 0x28); L->orders = w(r, m + 0x34); L->rows = w(r, m + 0x13C);
        L->amiga = w(r, m + 0x140); L->bpm = w(r, m + 0x144); L->speed = w(r, m + 0x148);
        L->order = m + 0x14C; L->table = m + 0x24C; L->data = m + 0x650; L->np = 0;
        // The first 255 ends the list: Need for Speed's modules store 255 orders, the rest 255s (the
        // driver itself would play on into them; the games stop the song).
        if (L->orders - 1u < 256u)
            for (uint32_t i = 0; i < L->orders; ++i) if (b(r, L->order + i) == 255) { L->orders = i; break; }
        if (L->orders - 1u < 256u)
            for (uint32_t i = 0; i < L->orders; ++i) if (b(r, L->order + i) >= L->np) L->np = b(r, L->order + i) + 1u;
    }
    if (!L->ch || L->ch > LS_CHANNELS || !L->orders || L->orders > 256 || !L->np || L->np > 256 || !L->rows ||
        L->rows > 256 || L->amiga > 1 || L->bpm < 32 || L->bpm > 255 || !L->speed || L->speed > 31) return 0;
    for (uint32_t i = 0; i < L->orders; ++i) if (b(r, L->order + i) >= L->np) return 0;
    return L->v;
}

// Pattern i's first byte and the bytes it may use (version 2 stores no size: up to 64 KB).
static int pattern_at(const LsRom *r, const Layout *L, unsigned i, uint32_t *start, uint32_t *size)
{
    if (L->v == 1) {
        uint32_t a = L->data;
        for (unsigned k = 0; k < i; ++k) a += w(r, L->table + 4 * k);
        *start = a;
        *size = w(r, L->table + 4 * i);
        return ok(r, a, *size);
    }
    uint32_t a = L->data + w(r, L->table + 4 * i);
    if (!ok(r, a, 0x14)) return 0;
    uint32_t room = r->size - (a - r->base);
    *start = a;
    *size = room < 0x10000 ? room : 0x10000;
    return 1;
}

int ls_module_valid(const LsRom *r, uint32_t m)
{
    Layout L;
    if (!layout(r, m, &L)) return 0;
    for (uint32_t i = 0; i < L.np; ++i) {
        uint32_t ps, size;
        if (!pattern_at(r, &L, i, &ps, &size) || size < 0x14 * L.ch) return 0;
    }
    return L.v;
}

uint32_t ls_module_end(const LsRom *r, uint32_t m)
{
    Layout L;
    if (!layout(r, m, &L)) return 0;
    uint32_t end = 0;
    for (uint32_t i = 0; i < L.np; ++i) {
        uint32_t a, size;
        if (!pattern_at(r, &L, i, &a, &size)) return 0;
        for (uint32_t c = 0; c < L.ch && ok(r, a, 0x14); ++c) a += 0x14 + w(r, a + 0x10);
        if (a > end) end = a;
    }
    return end;
}

// Channel c's three streams in a pattern: its block is {+4 volume stream, +8 effect stream, +16 the
// next block}, all from +20, where the note stream starts.
static int streams_at(const LsRom *r, uint32_t ps, uint32_t size, unsigned c, LsStream s[3])
{
    uint32_t a = ps, end = ps + size;
    for (unsigned k = 0; k < c; ++k) {
        if (a + 0x14 > end) return 0;
        uint32_t next = w(r, a + 0x10);
        if (next > end - a - 0x14) return 0;
        a += 0x14 + next;
    }
    if (a + 0x14 > end) return 0;
    uint32_t base = a + 0x14, vo = w(r, a + 4), fo = w(r, a + 8);
    if (vo >= end - base || fo >= end - base) return 0;
    s[0].p = base; s[1].p = base + vo; s[2].p = base + fo;
    for (int i = 0; i < 3; ++i) s[i].left = 0;
    return 1;
}

// One row of a stream. A pair is {rows, value}; a run of 0 lasts one row, as in the driver (its
// count goes below zero and the next row reads again).
static int pair(const LsRom *r, LsStream *s, int wide, uint32_t end)
{
    uint32_t n = wide ? 3 : 2;
    if (s->p + n > end || !ok(r, s->p, n)) return 0;
    s->left = b(r, s->p);
    s->value = wide ? hbe(r, s->p + 1) : b(r, s->p + 1);
    s->p += n;
    return 1;
}
static int stream_row(const LsRom *r, LsStream *s, int wide, uint32_t end, int fresh, uint16_t *v)
{
    if (fresh && !pair(r, s, wide, end)) return 0;
    if ((int16_t)s->left <= 0 && !pair(r, s, wide, end)) return 0;
    s->left = (uint16_t)((int16_t)s->left - 1);
    *v = s->value;
    return 1;
}

int ls_module_instruments(const LsRom *r, uint32_t m, uint32_t used[4])
{
    for (int i = 0; i < 4; ++i) used[i] = 0;
    Layout L;
    if (!ls_module_valid(r, m) || !layout(r, m, &L)) return 0;
    unsigned ch = L.ch, np = L.np, rows = L.rows;
    for (unsigned i = 0; i < np; ++i) {
        uint32_t ps, size;
        if (!pattern_at(r, &L, i, &ps, &size)) return 0;
        for (unsigned c = 0; c < ch; ++c) {
            LsStream s[3];
            if (!streams_at(r, ps, size, c, s)) return 0;
            for (unsigned row = 0; row < rows; ++row) {
                uint16_t v;
                if (!stream_row(r, &s[0], 1, ps + size, row == 0, &v)) return 0;
                unsigned ins = v & 127;
                if ((v >> 7) != NONE && ins) used[ins >> 5] |= 1u << (ins & 31);
            }
        }
    }
    return 1;
}

// An instrument: version 1 12 bytes {length, finetune, volume, loop start, loop length, relative note}
// (u16s) from the bank's start, the samples from +0xC00; version 2 16 bytes {length u32, finetune,
// volume, loop start, loop length, relative note u32}, the samples from +0x1000.
#define BANK_TABLE(v) ((v) == 2 ? 0x1000u : LS_BANK_SIZE)
#define BANK_ENTRY(v) ((v) == 2 ? 16u : 12u)
static void inst(const LsRom *r, uint32_t e, int v, unsigned *len, unsigned *fine, unsigned *vol, unsigned *ls,
                 unsigned *ll, unsigned *rel)
{
    if (v == 2) { *len = w(r, e) > LS_MAX_SAMPLE ? LS_MAX_SAMPLE : w(r, e); e += 2; }
    else *len = h(r, e);
    *fine = h(r, e + 2); *vol = h(r, e + 4); *ls = h(r, e + 6); *ll = h(r, e + 8); *rel = h(r, e + 10);
}

unsigned ls_bank_valid(const LsRom *r, uint32_t a, const uint32_t *used, int v)
{
    if ((a & 3) || !ok(r, a, BANK_TABLE(v))) return 0;
    uint32_t total = 0;
    unsigned n = 0;
    for (unsigned i = 0; i < 256; ++i) {
        uint32_t e = a + i * BANK_ENTRY(v);
        unsigned len, fine, vol, ls, ll, rel;
        if (v == 2 && w(r, e) > LS_MAX_SAMPLE) return 0;
        inst(r, e, v, &len, &fine, &vol, &ls, &ll, &rel);
        (void)fine;
        if (!len) {
            if (used && i < 127 && (used[(i + 1) >> 5] >> ((i + 1) & 31)) & 1) return 0;
            continue;
        }
        if (vol > 64 || ls + ll > len || (int16_t)rel < -96 || (int16_t)rel > 96) return 0;   // rel is signed
        total += len;
        ++n;
    }
    return n && ok(r, a + BANK_TABLE(v), total) ? n : 0;
}

// Which bank format a bank at a is: 2 (16-byte entries) or 1 (12-byte), 0 neither. Version 2
// modules come with either (Driv3r and Carbon 16, Underground 2 12); version 1 with 12.
int ls_bank_format(const LsRom *r, uint32_t a, const uint32_t *used, int version)
{
    if (version == 2 && ls_bank_valid(r, a, used, 2)) return 2;
    return ls_bank_valid(r, a, used, 1) ? 1 : 0;
}

// The order list walked with its jumps (B, D): ticks to the end, or to an order seen before (a loop).
int ls_module_shape(const LsRom *r, uint32_t m, uint32_t *ticks, int *loops)
{
    *ticks = 0; *loops = 0;
    Layout L;
    if (!ls_module_valid(r, m) || !layout(r, m, &L)) return 0;
    unsigned ch = L.ch, orders = L.orders, rows = L.rows, speed = L.speed;
    uint32_t seen[8], total = 0;
    for (int i = 0; i < 8; ++i) seen[i] = 0;
    unsigned order = 0, start_row = 0;
    for (unsigned guard = 0; guard < 512; ++guard) {
        // The end of the list: version 1's driver stops there; version 2's goes round again unless
        // the game says not to (Need for Speed's race music runs to the end and repeats), so it loops.
        if (order >= orders) { *loops = L.v == 2; break; }
        if (start_row == 0) {
            if ((seen[order >> 5] >> (order & 31)) & 1) { *loops = 1; break; }
            seen[order >> 5] |= 1u << (order & 31);
        }
        uint32_t ps, size;
        if (!pattern_at(r, &L, b(r, L.order + order), &ps, &size)) return 0;
        LsStream s[LS_CHANNELS], sn[LS_CHANNELS];
        for (unsigned c = 0; c < ch; ++c) {
            LsStream three[3];
            if (!streams_at(r, ps, size, c, three)) return 0;
            s[c] = three[2]; sn[c] = three[0];
        }
        unsigned next = order + 1, row_to = 0, jumped = 0, notes = 0;
        for (unsigned row = 0; row < rows && !jumped; ++row) {
            for (unsigned c = 0; c < ch; ++c) {
                uint16_t v, nv;
                if (!stream_row(r, &s[c], 1, ps + size, row == 0, &v) ||
                    !stream_row(r, &sn[c], 1, ps + size, row == 0, &nv)) return 0;
                unsigned fx = v >> 8, prm = v & 255;
                if (row < start_row) continue;
                if ((nv >> 7) != NONE) ++notes;
                if (fx == 15 && prm && prm <= 0x1F) speed = prm;
                if (fx == 11 && !jumped && prm == order && !notes) { *ticks = total; return 1; }   // the end: see process()
                if (fx == 11 && !jumped) { next = prm; row_to = 0; jumped = 1; }
                if (fx == 13 && !jumped) { next = order + 1 < orders ? order + 1 : 0; row_to = prm; jumped = 1; }
            }
            if (row >= start_row) total += speed;
        }
        order = next; start_row = row_to < rows ? row_to : 0;
    }
    *ticks = total;
    return 1;
}

// ---- playing ----

static uint32_t freq_linear(int period)
{
    int x = 7680 - period;
    if (x < 0) return 0;
    unsigned oct = divide((unsigned)x, 768), rem = (unsigned)x - oct * 768;
    if (oct > 7) return 0;            // the driver's shift goes negative: no sound (its note 98, a key-off)
    return ((uint32_t)kFreq[rem] * 4) >> (7 - oct);
}

// A frequency moved by u 768ths of an octave (the mods' pitch, in Amiga mode).
static uint32_t freq_shift(uint32_t f, int u)
{
    int oct = 0;
    while (u < 0) { u += 768; --oct; }
    while (u >= 768) { u -= 768; ++oct; }
    if (f > 65535) f = 65535;
    uint32_t g = divide(f * kFreq[u], 16726u);   // f < 65536 and kFreq < 33422: the product fits
    return oct >= 0 ? (uint32_t)(g << oct) : (uint32_t)(g >> -oct);
}

static uint32_t track_freq(LsPlayer *p, LsTrack *t)
{
    int u = p->mods.pitch >> 2;      // Q8 semitones -> 64ths of a semitone (768 an octave)
    if (p->linear) {
        int period = (uint16_t)(7616 - (t->rel + t->note - 2) * 64 - (t->fine >> 1));
        return freq_linear(period + t->lin + t->vib_lin - u);
    }
    int i = clamp(t->fine + t->note, 0, 511);
    int period = kAmiga[i] + t->amiga + t->vib;
    if (period <= 0) return 0;
    uint32_t f = divide(7093789u, (uint32_t)period * 2);
    return u ? freq_shift(f, u) : f;
}

static int valid_voice(LsPlayer *p, const LsVoice *v)
{
    return v->size && v->size <= LS_MAX_SAMPLE && ok(&p->rom, v->data, v->size + 1) && v->start < v->size &&
           (v->loop == LS_NOLOOP || v->loop < v->size) && v->step && v->step <= 0x200000 && v->left <= 255;
}

static void levels(LsPlayer *p, unsigned c)
{
    LsTrack *t = &p->tracks[c];
    unsigned v = (unsigned)clamp(t->vol, 0, 255);
    v = (v * p->music_volume) >> 6;
    if (p->mods.mute & (1u << c)) v = 0;
    t->voice.left = t->voice.right = (uint16_t)(v > 255 ? 255 : v);
    uint32_t f = track_freq(p, t);
    t->voice.step = f >> 2;          // Q12 at 16384 Hz: frequency x 4096 / 16384
}

static void recompute(LsPlayer *p, unsigned c)
{
    LsTrack *t = &p->tracks[c];
    if (!t->active) return;
    levels(p, c);
    if (!t->voice.step) { off(p, c); return; }
    if (p->hooks.update) p->hooks.update(p->hooks.user, c, &t->voice);
}

// An instrument's entry (with the mods' voice shift and table offset), and its sample.
static uint32_t entry_of(LsPlayer *p, unsigned ins, uint32_t *data)
{
    int i = (int)ins + p->mods.voice_shift;
    if (i < 0 || i > 255) return 0;
    uint32_t e = p->bank + (uint32_t)i * BANK_ENTRY(p->bank_format) + (uint32_t)(int32_t)p->mods.table_offset;
    if (!ok(&p->rom, e, 16)) return 0;
    uint32_t a = p->bank + BANK_TABLE(p->bank_format);
    for (int k = 0; k < i; ++k) {
        uint32_t len = p->bank_format == 2 ? w(&p->rom, p->bank + (uint32_t)k * 16) : h(&p->rom, p->bank + (uint32_t)k * 12);
        a += len > LS_MAX_SAMPLE ? LS_MAX_SAMPLE : len;
    }
    *data = a;
    return e;
}

static unsigned ins_volume(LsPlayer *p, unsigned ins)
{
    uint32_t d, e = entry_of(p, ins, &d);
    unsigned len, fine, vol, ls, ll, rel;
    if (!e) return 0;
    inst(&p->rom, e, p->bank_format, &len, &fine, &vol, &ls, &ll, &rel);
    return vol;
}

static void note_on(LsPlayer *p, unsigned c, unsigned note)
{
    LsTrack *t = &p->tracks[c];
    const LsRom *r = &p->rom;
    t->key = (uint8_t)clamp((int)note + 11, 0, 127);
    ++t->notes; t->activity = 255; t->gate = 1;
    off(p, c);
    if (random8(p) < p->mods.drop_chance) return;
    uint32_t data, e = entry_of(p, t->ins, &data);
    if (!e) return;
    unsigned len, fine, vol, ls, ll, rel;
    inst(r, e, p->bank_format, &len, &fine, &vol, &ls, &ll, &rel);
    t->fine = (int16_t)fine;
    t->rel = (int16_t)rel;
    t->note = (uint16_t)note;
    t->lin = 0; t->porta_mem = 0; t->vib_lin = 0;
    if (!len) return;                 // the driver mixes nothing (its end is 0)
    uint32_t size = len, loop = ll ? ls : LS_NOLOOP;
    unsigned chop = (size * p->mods.sample_end) >> 8;
    size -= chop;
    if (!size) return;
    if (loop != LS_NOLOOP && loop >= size) loop = LS_NOLOOP;
    data += (uint32_t)p->mods.scrub;
    if (!ok(r, data, size + 1)) return;
    t->voice.data = data;
    t->voice.size = size;
    t->voice.loop = loop;
    t->voice.start = (size * p->mods.sample_start) >> 8;
    if (t->voice.start >= size) t->voice.start = size - 1;
    t->voice.reverse = random8(p) < p->mods.reverse_chance;
    t->active = 1;
    levels(p, c);
    if (!t->voice.step) { t->active = 0; return; }
    if (p->hooks.note) p->hooks.note(p->hooks.user, c, &t->voice);
    if (!valid_voice(p, &t->voice)) { off(p, c); return; }
    if (p->hooks.start) p->hooks.start(p->hooks.user, c, &t->voice);
}

// Linear mode's 1xx / 2xx (the driver's two helpers): Fx fine (x 4) and Ex extra fine act on the
// first tick only, the rest (x 4) on every later tick; 0 reuses the last parameter.
static void porta_linear(LsTrack *t, unsigned counter, unsigned prm, int down)
{
    if (prm) t->porta_mem = (uint16_t)prm; else prm = t->porta_mem;
    unsigned hi = (prm >> 4) & 15, lo = prm & 15;
    int amount;
    if (hi == 15) { if (counter) return; amount = (int)lo * 4; }
    else if (hi == 14) { if (counter) return; amount = (int)lo; }
    else { if (!counter) return; amount = (int)prm * 4; }
    t->lin += down ? amount : -amount;
}

static void volume_slide(LsPlayer *p, LsTrack *t)
{
    if (!p->counter || !(t->vsl_up | t->vsl_down)) return;   // the driver skips its first tick
    t->vol = (int16_t)clamp(t->vsl_up ? t->vol + t->vsl_up : t->vol - t->vsl_down, 0, 64);
}

static void process(LsPlayer *p, unsigned c)
{
    LsTrack *t = &p->tracks[c];
    unsigned note = t->cell_note, ins = t->cell_ins, fx = t->cell_fx, prm = t->cell_param;
    if ((fx & 15) == 3) {                       // tone portamento: the note is its target, not a new one
        if (note != NONE) t->tp_target = (int16_t)note;
        note = NONE;
        if (ins) t->vol = (int16_t)ins_volume(p, ins - 1);
        ins = 0;
    }
    t->kind = -1;
    if (p->version == 1 || fx != 4) t->vib = 0;       // version 2 keeps a vibrato going
    if (note == NONE) {
        if (ins) { t->ins = (uint16_t)(ins - 1); t->vol = (int16_t)ins_volume(p, t->ins); }
    } else {
        if (ins) { t->ins = (uint16_t)(ins - 1); t->vol = (int16_t)ins_volume(p, t->ins); }
        ++p->pattern_notes;
        note_on(p, c, note);
        if (fx != 3) t->amiga = 0;
    }
    unsigned x = prm >> 4, y = prm & 15;
    switch (fx) {
    case 0: t->kind = 0; break;                  // arpeggio: the driver keeps it but never plays it
    case 1: case 2:
        t->kind = (int8_t)fx;
        if (!p->linear) t->tp_step = (int16_t)(fx == 1 ? -(int)prm : (int)prm);
        else { porta_linear(t, p->counter, prm, fx == 2); recompute(p, c); }
        break;
    case 3:
        t->kind = 3;
        if (prm) t->tp_speed = (int16_t)prm;
        t->tp_step = t->tp_target == (int16_t)t->note ? 0 : t->tp_target > (int16_t)t->note ? t->tp_speed : -t->tp_speed;
        break;
    case 4:
        t->kind = 4;
        if (p->version == 1) { if (x || y) { t->vib_speed = (uint8_t)x; t->vib_depth = (uint8_t)y; } }
        else {                                    // each nibble if given; either restarts the sine
            if (x) t->vib_speed = (uint8_t)x;
            if (y) t->vib_depth = (uint8_t)y;
            if (x || y) t->vib_pos = 0;
        }
        break;
    case 5: case 6: case 10:
        t->kind = (int8_t)fx; t->vsl_up = (uint8_t)x; t->vsl_down = (uint8_t)y;
        break;
    case 11:                                      // position jump: the row's end goes to order prm
        if (prm == p->order && !p->pattern_notes) { p->ended = 1; break; }   // a silent pattern holding itself:
        if (prm <= p->order) { ++p->loops; event(p, LS_EVENT_LOOP, p->loops); }   // the song's end (Driv3r)
        p->order = (uint16_t)(prm - 1);
        p->row = p->rows;
        break;
    case 12: t->vol = (int16_t)prm; break;
    case 13:                                      // pattern break, once a row
        if (!p->broke) {
            p->broke = 1;
            unsigned order = p->order + 1u;
            if (order >= p->orders) order = 0;
            if (order == 0) { ++p->loops; event(p, LS_EVENT_LOOP, p->loops); }
            if (prm && prm < p->rows) { p->seek = 1; p->seek_order = (uint16_t)order; p->seek_row = (uint16_t)prm; }
            else { p->order = (uint16_t)order; p->row = 0; }   // the driver would read on in the old streams
        }
        break;
    case 14:
        if (x == 6) {                             // pattern loop (the driver's other Ex do nothing)
            if (!y) t->loop_row = (uint8_t)(p->row - 1);
            else {
                if (t->loop_count) --t->loop_count; else t->loop_count = (uint8_t)y;
                if (t->loop_count) { p->seek = 1; p->seek_order = p->order; p->seek_row = t->loop_row; }
            }
        } else if (prm) unsupported(p, 16 + x);
        break;
    case 15:
        if (prm <= 0x1F) { if (prm) p->speed = (uint16_t)prm; } else p->bpm = (uint16_t)prm;
        break;
    default: if (fx) unsupported(p, fx); break; // 7 8 9: the driver ignores them too
    }
    if (t->cell_vc) t->vol = (int16_t)(t->cell_vc - 1);
}

static int read_cells(LsPlayer *p, unsigned row)
{
    uint32_t ps, size;
    Layout L;
    if (!layout(&p->rom, p->module, &L) || !pattern_at(&p->rom, &L, b(&p->rom, L.order + p->order), &ps, &size))
        return fail(p, p->module);
    for (unsigned c = 0; c < p->channels; ++c) {
        LsTrack *t = &p->tracks[c];
        if (row == 0 && !streams_at(&p->rom, ps, size, c, t->s)) return fail(p, ps);
        uint16_t nv, vv, fv;
        if (!stream_row(&p->rom, &t->s[0], 1, ps + size, row == 0, &nv) ||
            !stream_row(&p->rom, &t->s[1], 0, ps + size, row == 0, &vv) ||
            !stream_row(&p->rom, &t->s[2], 1, ps + size, row == 0, &fv)) return fail(p, ps);
        t->cell_note = nv >> 7; t->cell_ins = nv & 127; t->cell_vc = (uint8_t)vv;
        t->cell_fx = (uint8_t)(fv >> 8); t->cell_param = (uint8_t)fv;
    }
    return 1;
}

static int position(LsPlayer *p, unsigned order, unsigned row)
{
    if (order >= p->orders || row >= p->rows) return 0;
    p->order = (uint16_t)order;
    for (unsigned r = 0; r < row; ++r) if (!read_cells(p, r)) return 0;
    p->row = (uint16_t)row;
    return 1;
}

static void read_row(LsPlayer *p)
{
    p->broke = 0;
    p->counter = 0;
    if (p->row == 0) p->pattern_notes = 0;
    if (!read_cells(p, p->row)) return;
    ++p->row;
    p->seek = 0; p->ended = 0;
    for (unsigned c = 0; c < p->channels && p->running; ++c) process(p, c);
    event(p, LS_EVENT_ROW, ((uint32_t)p->order << 16) | p->row);
    if (p->seek) { position(p, p->seek_order, p->seek_row); return; }  // after every channel's cell
    if (!p->ended) {
        if (p->row < p->rows) return;
        p->row = 0;
        if (++p->order < p->orders) return;
    }
    p->order = 0; p->row = 0;                    // the driver stops here (its playing flag goes to 0)
    for (unsigned c = 0; c < LS_CHANNELS; ++c) off(p, c);
    if (!p->repeat) { ls_stop(p); return; }
    ++p->loops;
    event(p, LS_EVENT_LOOP, p->loops);
}

// The driver's tick effects: version 1 on its first four channels only (its loop stops at 3), version
// 2 on every channel.
static void effects(LsPlayer *p)
{
    for (unsigned c = 0; c < p->channels && (p->version == 2 || c < 4); ++c) {
        LsTrack *t = &p->tracks[c];
        switch (t->kind) {
        case 1: case 2:
            if (!p->linear) t->amiga = (int16_t)(t->amiga + t->tp_step);
            else { porta_linear(t, p->counter, 0, t->kind == 2); recompute(p, c); }
            break;
        case 3: case 5:                           // tone portamento (no cart seen uses it; XM's glide)
            if (t->tp_step) {
                int target = t->tp_target, now = t->note;
                if (p->linear) {
                    int goal = (now - target) * 64, step = t->tp_speed * 4;
                    if (t->lin < goal) t->lin = t->lin + step > goal ? goal : t->lin + step;
                    else t->lin = t->lin - step < goal ? goal : t->lin - step;
                    if (t->lin == goal) { t->note = (uint16_t)target; t->lin = 0; t->tp_step = 0; }
                    recompute(p, c);
                } else {
                    t->amiga = (int16_t)(t->amiga + t->tp_step);
                    int cur = kAmiga[clamp(t->fine + now, 0, 511)] + t->amiga;
                    int goal = kAmiga[clamp(t->fine + target, 0, 511)];
                    if ((t->tp_step < 0 && cur <= goal) || (t->tp_step > 0 && cur >= goal)) {
                        t->note = (uint16_t)target; t->amiga = 0; t->tp_step = 0;
                    }
                }
            }
            if (t->kind == 5) volume_slide(p, t);
            break;
        case 4: case 6:
            if (p->version == 2 && p->linear) {   // version 2: XM's linear vibrato
                int v = (kSine[(t->vib_pos >> 2) & 31] * t->vib_depth) >> 5;
                t->vib_lin = t->vib_pos > 31 ? -v : v;
                t->vib_pos = (uint8_t)((t->vib_pos + ((t->vib_speed + 4) >> 1)) & 63);
                recompute(p, c);
            } else {
                if (p->version == 1 && !p->counter) t->vib_pos = 0;   // version 1 starts its sine every row
                t->vib = (int16_t)((((t->vib_pos & 32) ? -kSine[t->vib_pos & 31] : kSine[t->vib_pos & 31]) * t->vib_depth) >> 7);
                t->vib_pos = (uint8_t)((t->vib_pos + t->vib_speed) & 63);
            }
            if (t->kind == 6) volume_slide(p, t);
            break;
        case 10: volume_slide(p, t); break;
        default: break;
        }
        if (!p->linear) recompute(p, c);
    }
    ++p->counter;
}

void ls_update(LsPlayer *p)
{
    for (unsigned c = 0; c < p->channels; ++c) {
        LsTrack *t = &p->tracks[c];
        if (!t->active) continue;
        if (p->hooks.done && p->hooks.done(p->hooks.user, c)) { off(p, c); continue; }
        recompute(p, c);
    }
}

void ls_tick(LsPlayer *p)
{
    if (!p->running) return;
    if (p->tick == 0) read_row(p); else effects(p);
    if (!p->running) return;
    if (++p->tick >= p->speed) p->tick = 0;
    for (unsigned c = 0; c < p->channels; ++c) if (p->tracks[c].activity) p->tracks[c].activity -= (uint8_t)((p->tracks[c].activity >> 3) + 1);
    ls_update(p);
}

int ls_seek(LsPlayer *p, unsigned order, unsigned row)
{
    if (!p->running) return 0;
    for (unsigned c = 0; c < LS_CHANNELS; ++c) off(p, c);
    if (!position(p, order, row)) return fail(p, p->module);
    p->tick = 0;
    return 1;
}

int ls_start(LsPlayer *p, uint32_t m, uint32_t bank, int format)
{
    ls_stop(p);
    zero(p->tracks, sizeof(p->tracks));
    Layout L;
    int v = ls_module_valid(&p->rom, m);
    if (!v || !layout(&p->rom, m, &L)) return 0;
    int f = format ? (ls_bank_valid(&p->rom, bank, 0, format) ? format : 0) : ls_bank_format(&p->rom, bank, 0, v);
    if (!f) return 0;
    p->module = m; p->bank = bank; p->version = (uint8_t)v; p->bank_format = (uint8_t)f;
    p->channels = (uint16_t)(v == 2 && L.ch > 8 ? 8 : L.ch);   // version 2's driver plays 8 at most
    p->orders = (uint16_t)L.orders;
    p->pattern_count = (uint16_t)L.np;
    p->rows = (uint16_t)L.rows;
    p->linear = !L.amiga;
    p->bpm = (uint16_t)L.bpm;
    p->speed = (uint16_t)L.speed;
    p->music_volume = v == 2 ? 64 : 90;          // the games' own: Driv3r sets 64, Puzzle Fighter 90
    p->order = p->row = p->tick = p->counter = 0;
    p->error = 0; p->unsupported = 0; p->loops = 0;
    for (unsigned c = 0; c < LS_CHANNELS; ++c) p->tracks[c].kind = -1;
    p->running = 1;
    return 1;
}

uint32_t ls_tick_hz_q16(const LsPlayer *p)
{
    uint32_t hz = divide((uint32_t)p->bpm * 2u, 5);   // the driver's ticks/s, an integer
    return (uint32_t)(((uint64_t)hz << 16) * p->mods.tempo >> 8);
}
