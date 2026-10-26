// cgb.h — the four PSG ("CGB") voices m4a songs use, driven like m4a's CgbSound.
#pragma once
#include <stdint.h>

void     cgb_init(void);
void     cgb_frame(void);
void     cgb_kick(void);
void     cgb_off(int ch);
void     cgb_mute_changed(void);
uint32_t cgb_key_to_freq(int chanNum, int key, int fine);
