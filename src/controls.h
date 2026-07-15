#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CONTROL_NONE,
    CONTROL_SENSITIVITY_DOWN,
    CONTROL_SENSITIVITY_UP,
    CONTROL_MODE,
    CONTROL_BANK_NEXT,
    CONTROL_PITCH_BEND_TOGGLE,
    CONTROL_MIDI_MODE_TOGGLE,
    CONTROL_RAW_MODE_TOGGLE,
    CONTROL_VOLUME_DOWN,
    CONTROL_VOLUME_UP,
    CONTROL_DURATION_DOWN,
    CONTROL_DURATION_UP,
    CONTROL_ROOT_DOWN,
    CONTROL_ROOT_UP,
} control_event_t;

void controls_init(void);
control_event_t controls_poll(void);
