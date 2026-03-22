#ifndef MORSE_LOGIC_H
#define MORSE_LOGIC_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Morse Decoder State
 */
typedef struct {
    uint32_t dot_ms;
    uint32_t last_edge_ms;
    bool is_pressed;
    char current_sequence[8];
    int seq_ptr;
    
    /* Phase 5: Rhythm Comparison */
    char target_char;
    uint32_t pulse_durations[8];
    int pulse_count;
    int last_match_score;
} morse_decoder_t;

const char* morse_logic_get_current_bits(void);

/**
 * @brief Initialize Morse Logic
 */
void morse_logic_init(uint32_t wpm);

/**
 * @brief Process a key edge (press or release)
 * @param pressed true if pressed, false if released
 * @param timestamp_ms current time in ms
 * @return Decoded character if a character was just completed, or 0
 */
char morse_logic_handle_key(bool pressed, uint32_t timestamp_ms);

/**
 * @brief Check for timeouts and decode characters if idle.
 */
char morse_logic_update(uint32_t timestamp_ms);

/**
 * @brief Get the Koch level sequence (first N characters)
 */
const char* morse_logic_get_koch_chars(int level);

/**
 * @brief Generate a random sequence of N characters for a given level
 */
void morse_logic_generate_sequence(int level, int count, char *out_buf);

/**
 * @brief Get the Morse dot/dash sequence for a character.
 * Returns a string like ".-" or NULL if not found.
 */
const char* morse_logic_get_sequence(char c);

/**
 * @brief Get all supported characters in Koch order.
 */
const char* morse_logic_get_all_chars(void);

/**
 * @brief Set the expected target character for rhythmic comparison.
 */
void morse_logic_set_target(char c);

/**
 * @brief Get the rhythmic match score (0-100) of the last decoded character.
 */
int morse_logic_get_match_score(void);

#endif // MORSE_LOGIC_H
