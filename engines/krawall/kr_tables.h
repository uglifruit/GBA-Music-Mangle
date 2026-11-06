// kr_tables.h - Krawall's player tables (kr_tables.c, LGPL-2.1: see COPYING.LGPL).
#ifndef KR_TABLES_H
#define KR_TABLES_H
#include <stdint.h>
#define KR_TABLE_VIBRATO 64
#define KR_TABLE_PERIODS (12*10)
extern const int16_t kr_table_sine[], kr_table_ramp[], kr_table_square[], kr_table_rand[];
extern const uint16_t kr_table_finetune[];   // 64 entries: finetune -32..31 (s8 >> 2), Q15
extern const uint16_t kr_table_periods[];    // Amiga periods, C-0 .. B-9
extern const uint32_t kr_table_linear[];     // 768 entries: one octave of XM linear frequencies
#endif
