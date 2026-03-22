/**
 * @file morse_logic.c
 * @brief Morse code decoding and training logic.
 * 
 * This module handles the timing-based decoding of Morse code,
 * calculates rhythm accuracy scores, and manages the Koch method
 * character sequences for training.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "morse_logic.h"

typedef struct {
    char c;
    const char *seq;
} morse_map_t;

static const morse_map_t MORSE_TABLE[] = {
    {'A', ".-"},   {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},  {'E', "."},
    {'F', "..-."}, {'G', "--."},  {'H', "...."}, {'I', ".."},   {'J', ".---"},
    {'K', "-.-"},  {'L', ".-.."}, {'M', "--"},   {'N', "-."},   {'O', "---"},
    {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."},  {'S', "..."},  {'T', "-"},
    {'U', "..-"},  {'V', "...-"}, {'W', ".--"},  {'X', "-..-"}, {'Y', "-.--"},
    {'Z', "--.."}, {'1', ".----"},{'2', "..---"},{'3', "...--"},{'4', "....-"},
    {'5', "....."},{'6', "-...."},{'7', "--..."},{'8', "---.."},{'9', "----."},
    {'0', "-----"},{'/', "-..-."},{'?', "..--.."},{',', "--..--"},{'.', ".-.-.-"}
};

static const char *KOCH_ORDER = "KMRSUAPTLOWIJEF0YVGC5/QN.DH8B64Z73219?,";

static morse_decoder_t d;

/**
 * @brief Decode a string of dots and dashes into a character.
 * 
 * @param seq Pointer to the sequence string (e.g., ".-")
 * @return char The decoded character, or '?' if unknown.
 */
static char decode_sequence(const char *seq) {
    if (strlen(seq) == 0) return 0;
    for (int i = 0; i < sizeof(MORSE_TABLE)/sizeof(morse_map_t); i++) {
        if (strcmp(MORSE_TABLE[i].seq, seq) == 0) return MORSE_TABLE[i].c;
    }
    return '?';
}

void morse_logic_init(uint32_t wpm)
{
    d.dot_ms = 1200 / wpm;
    d.seq_ptr = 0;
    d.is_pressed = false;
    d.last_edge_ms = 0;
    d.target_char = 0;
    d.pulse_count = 0;
    d.last_match_score = 0;
    memset(d.current_sequence, 0, sizeof(d.current_sequence));
    memset(d.pulse_durations, 0, sizeof(d.pulse_durations));
}

/**
 * @brief Calculate the rhythmic match score (0-100) for a sequence of pulses.
 * 
 * @param seq Ideal sequence for comparison.
 * @param durs Measured durations of pulses.
 * @param count Number of pulses measured.
 * @param unit_ms Ideal dot duration in milliseconds.
 * @return int Score from 0 to 100.
 */
static int calculate_match_score(const char *seq, uint32_t *durs, int count, uint32_t unit_ms)
{
    if (count == 0 || strlen(seq) == 0) return 0;
    
    float total_error = 0;
    int elements = 0;
    
    // We expect 'count' pulses (dots/dashes)
    // We don't track gaps in pulse_durations yet, but let's compare the pulses
    for (int i = 0; i < count && seq[i] != '\0'; i++) {
        float ideal = (seq[i] == '.') ? unit_ms : unit_ms * 3;
        float error = abs((int)durs[i] - (int)ideal) / ideal;
        total_error += error;
        elements++;
    }
    
    if (elements == 0) return 0;
    int score = (int)(100.0f * (1.0f - (total_error / elements)));
    return (score < 0) ? 0 : score;
}

char morse_logic_handle_key(bool pressed, uint32_t now)
{
    uint32_t duration = now - d.last_edge_ms;
    char result = 0;

    // Filter out very short pulses/gaps (mechanical chatter)
    if (duration < 10 && d.last_edge_ms != 0) {
        return 0; 
    }

    if (pressed && !d.is_pressed) {
        // Press: end of a space. 
        if (d.seq_ptr > 0) {
            // Context-aware gap check: if we have a target and we aren't done yet, be lenient.
            uint32_t threshold = d.dot_ms * 4;
            if (d.target_char) {
                const char *target_seq = morse_logic_get_sequence(d.target_char);
                if (target_seq && d.seq_ptr < strlen(target_seq)) {
                    threshold = d.dot_ms * 6; // Give even more air if we expect more pulses
                }
            }

            if (duration > threshold) {
                 result = decode_sequence(d.current_sequence);
                 // Calculate score if it matches target or just for the sequence
                 d.last_match_score = calculate_match_score(d.current_sequence, d.pulse_durations, d.pulse_count, d.dot_ms);
                 d.seq_ptr = 0;
                 d.pulse_count = 0;
                 memset(d.current_sequence, 0, sizeof(d.current_sequence));
            }
        }
    } else if (!pressed && d.is_pressed) {
        // Release: end of a pulse.
        if (d.pulse_count < 8) d.pulse_durations[d.pulse_count++] = duration;
        
        if (duration < d.dot_ms * 2.2) {
            if (d.seq_ptr < 7) d.current_sequence[d.seq_ptr++] = '.';
        } else {
            if (d.seq_ptr < 7) d.current_sequence[d.seq_ptr++] = '-';
        }
    }

    d.is_pressed = pressed;
    d.last_edge_ms = now;
    return result;
}

char morse_logic_update(uint32_t now)
{
    if (!d.is_pressed && d.seq_ptr > 0) {
        uint32_t duration = now - d.last_edge_ms;
        uint32_t threshold = d.dot_ms * 4;
        
        // If we expect more pulses for the target, wait longer
        if (d.target_char) {
            const char *target_seq = morse_logic_get_sequence(d.target_char);
            if (target_seq && d.seq_ptr < strlen(target_seq)) {
                threshold = d.dot_ms * 8; // Wait up to 8 dots if we know more is coming
            }
        }

        if (duration > threshold) {
            char result = decode_sequence(d.current_sequence);
            d.last_match_score = calculate_match_score(d.current_sequence, d.pulse_durations, d.pulse_count, d.dot_ms);
            d.seq_ptr = 0;
            d.pulse_count = 0;
            memset(d.current_sequence, 0, sizeof(d.current_sequence));
            return result;
        }
    }
    return 0;
}

void morse_logic_set_target(char c)
{
    d.target_char = c;
}

int morse_logic_get_match_score(void)
{
    return d.last_match_score;
}

const char* morse_logic_get_koch_chars(int level)
{
    static char buf[64];
    int len = level;
    if (len > strlen(KOCH_ORDER)) len = strlen(KOCH_ORDER);
    strncpy(buf, KOCH_ORDER, len);
    buf[len] = '\0';
    return buf;
}

void morse_logic_generate_sequence(int level, int count, char *out_buf)
{
    const char *chars = morse_logic_get_koch_chars(level);
    int num_chars = strlen(chars);
    srand(time(NULL));
    for (int i = 0; i < count; i++) {
        out_buf[i] = chars[rand() % num_chars];
    }
    out_buf[count] = '\0';
}

const char* morse_logic_get_sequence(char c)
{
    c = (c >= 'a' && c <= 'z') ? c - 32 : c;
    for (int i = 0; i < sizeof(MORSE_TABLE)/sizeof(morse_map_t); i++) {
        if (MORSE_TABLE[i].c == c) return MORSE_TABLE[i].seq;
    }
    return NULL;
}

const char* morse_logic_get_all_chars(void)
{
    return KOCH_ORDER;
}

const char* morse_logic_get_current_bits(void)
{
    return d.current_sequence;
}
