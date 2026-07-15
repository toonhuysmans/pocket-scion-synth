#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    STATUS_RGB_GREEN,
    STATUS_RGB_YELLOW,
    STATUS_RGB_BLUE,
    STATUS_RGB_CYAN,
    STATUS_RGB_PURPLE,
} status_rgb_colour_t;

void status_rgb_init(void);
void status_rgb_show_level(status_rgb_colour_t colour,
                           uint8_t value, uint8_t maximum);
void status_rgb_show_program(uint8_t program, bool pitch_bend_enabled);
void status_rgb_show_midi_mode(bool multichannel);
void status_rgb_show_raw_mode(void);
void status_rgb_service(uint8_t program, bool pitch_bend_enabled, bool raw_mode,
                        float sensor_expression, float sensor_proximity,
                        uint8_t active_voices, uint8_t rhythm_density,
                        uint8_t pending_ratchets, uint32_t note_on_counter,
                        uint32_t ratchet_fire_counter,
                        int16_t amp_envelope, int16_t lfo_level);
