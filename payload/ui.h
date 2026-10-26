// ui.h — the screen: the boot SCAN screen, the PLAY page, and a paged editor.
//
// PLAY is the performance page: the song, the clock, sixteen track lanes (mute, solo, activity,
// note), and the mangles' live values. START enters the editor; L/R move between its pages and
// START returns to PLAY.
//
// These values ARE the index into page_tab[] in ui.c, and the status word sends the page number
// upstream to the LEDs. Renumbering here without reordering that array gives a tab bar that lies
// rather than a compile error.
#pragma once

#include <stdint.h>

#define PAGE_PLAY  0
#define PAGE_MAP   1
#define PAGE_SET   2
#define PAGE_MEM   3
#define PAGE_CAL   4
#define PAGE_COUNT 5

extern uint8_t g_page;

void ui_init(void);
void ui_frame(void);                  // one frame: handle buttons, redraw what changed

// The boot / rescan screen.
void ui_scan_begin(void);
void ui_scan_progress(uint32_t done256);
void ui_scan_result(void);            // draws the outcome; returns at once

// SET page's RESCAN CART calls this; main.c owns the scan flow and sets it.
extern void (*g_ui_rescan)(void);
