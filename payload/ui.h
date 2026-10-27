// ui.h — the screen: the boot SCAN screen, the PLAY page, and a paged editor.
//
// PLAY is the performance page: the song, the clock, sixteen track lanes (mute, solo, activity,
// note), and the mangles' live values. SELECT toggles PERFORM mode, where the GBA's buttons drive
// the BTN map instead of the page. START enters the editor; L/R move between its pages and START
// returns to PLAY.
//
// These values ARE the index into page_tab[] in ui.c, and the status word sends the page number
// upstream. Renumbering here without reordering that array gives a tab bar that lies rather than a
// compile error.
#pragma once

#include <stdint.h>

#define PAGE_PLAY  0
#define PAGE_MAP   1
#define PAGE_BTN   2
#define PAGE_BASE  3
#define PAGE_SET   4
#define PAGE_MEM   5
#define PAGE_CAL   6
#define PAGE_COUNT 7

extern uint8_t g_page;

void ui_init(void);
void ui_frame(void);                  // one frame: handle buttons, redraw what changed

// The boot / rescan screen.
void ui_scan_begin(void);
void ui_scan_progress(uint32_t done256);
void ui_scan_status(const char *line1, const char *line2);   // what the boot is doing, under the cart
void ui_scan_result(int slot);        // draws the outcome; slot = the saved slot booted from, or 0xFF
void ui_mem_select(int slot);         // put the MEM page's cursor on a slot

// SET page's RESCAN CART calls this; main.c owns the scan flow and sets it.
extern void (*g_ui_rescan)(void);

// SERVICE LOAD for the CAL page (main.c): the modulator + clock service, percent x 16 of ~1 ms.
extern volatile uint16_t g_svcAvg, g_svcPeak;
