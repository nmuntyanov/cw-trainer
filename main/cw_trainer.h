#ifndef CW_TRAINER_H
#define CW_TRAINER_H

#include <stdint.h>

extern uint32_t freq;
extern uint8_t vol_val;
extern uint32_t wpm;

void update_settings(uint32_t new_freq, uint8_t new_vol, uint32_t new_wpm);

#endif // CW_TRAINER_H
