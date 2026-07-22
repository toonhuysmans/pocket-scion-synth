#pragma once

enum {
#if PICO_RP2350
    // Pimoroni Pico Audio Pack: I2S data/clock on GP9/10/11.
    // Pimoroni Pico Display Pack: A/B/X/Y buttons on GP12/13/14/15.
    PIN_SENSOR = 0,
    PIN_RGB_DATA = 1,
    PIN_I2S_DATA = 9,
    PIN_I2S_BCLK = 10,
    PIN_I2S_LRCLK = 11,
    PIN_MIDI_TX = 16,
    PIN_BUTTON_SENS_DOWN = 12,
    PIN_BUTTON_SENS_UP = 13,
    PIN_BUTTON_MODE = 14,
    PIN_BUTTON_ROOT_DOWN = 15,
    // No fifth physical button is present on the Display Pack. GP28 is kept
    // as an inactive spare so the existing control state machine remains
    // compatible; Display Pack users get sensitivity-, sensitivity+, mode,
    // and volume- directly, with mode-shift for the alternate function.
    PIN_BUTTON_ROOT_UP = 28,
    PIN_AUX_ACTIVE_LOW = 27,
    PIN_AUX_ACTIVE_HIGH = 29,
#else
    PIN_SENSOR = 0,
    PIN_RGB_DATA = 1,
    PIN_I2S_DATA = 12,
    PIN_I2S_BCLK = 13,
    PIN_I2S_LRCLK = 14,
    PIN_MIDI_TX = 16,
    PIN_BUTTON_ROOT_UP = 17,
    PIN_BUTTON_ROOT_DOWN = 18,
    PIN_BUTTON_MODE = 19,
    PIN_BUTTON_SENS_DOWN = 20,
    PIN_BUTTON_SENS_UP = 21,
    PIN_AUX_ACTIVE_LOW = 27,
    PIN_AUX_ACTIVE_HIGH = 29,
#endif
};
