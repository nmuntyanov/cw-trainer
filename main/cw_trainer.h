#ifndef CW_TRAINER_H
#define CW_TRAINER_H

#include <stdint.h>

extern uint32_t freq;
extern uint8_t vol_val;
extern uint32_t wpm;
extern uint8_t noise_level;
extern char callsign[16];
extern char qth_locator[16];

void update_settings(uint32_t new_freq, uint8_t new_vol, uint32_t new_wpm, uint8_t new_noise, const char* new_callsign, const char* new_qth);
void trigger_playback(char c);
void trigger_playback(char c);
void trigger_playback_string(const char* s);

#endif // CW_TRAINER_H
