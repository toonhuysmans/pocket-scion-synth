#include "controls.h"

#include "board_pins.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#define BUTTON_COUNT 5u
#define SENS_DOWN_INDEX 0u
#define SENS_UP_INDEX 1u
#define MODE_BUTTON_INDEX 2u
#define VOLUME_DOWN_INDEX 3u
#define VOLUME_UP_INDEX 4u
#define SCAN_PERIOD_US 2000u
#define REPEAT_DELAY_US 400000u
#define REPEAT_PERIOD_US 120000u
#define BANK_HOLD_US 900000u
#define DOUBLE_CLICK_US 320000u
#define DUAL_HOLD_US 3000000u

typedef struct {
    uint8_t pin;
    bool stable_pressed;
    bool candidate_pressed;
    uint8_t candidate_count;
    uint32_t next_repeat_us;
    bool action_sent;
    bool long_fired;
    uint32_t pressed_us;
    control_event_t direct_event;
    control_event_t modified_event;
} button_t;

static button_t buttons[BUTTON_COUNT] = {
#if PICO_RP2350
    { PIN_BUTTON_SENS_DOWN, false, false, 0, 0, false, false, 0,
      CONTROL_PARAMETER_NEXT, CONTROL_NONE },
    { PIN_BUTTON_SENS_UP, false, false, 0, 0, false, false, 0,
      CONTROL_PARAMETER_PREVIOUS, CONTROL_NONE },
    { PIN_BUTTON_MODE, false, false, 0, 0, false, false, 0,
      CONTROL_PARAMETER_INCREASE, CONTROL_NONE },
    { PIN_BUTTON_ROOT_DOWN, false, false, 0, 0, false, false, 0,
      CONTROL_PARAMETER_DECREASE, CONTROL_NONE },
    { PIN_BUTTON_ROOT_UP, false, false, 0, 0, false, false, 0,
      CONTROL_NONE, CONTROL_NONE },
#else
    { PIN_BUTTON_SENS_DOWN, false, false, 0, 0, false, false, 0,
      CONTROL_SENSITIVITY_DOWN, CONTROL_DURATION_DOWN },
    { PIN_BUTTON_SENS_UP, false, false, 0, 0, false, false, 0,
      CONTROL_SENSITIVITY_UP, CONTROL_DURATION_UP },
    { PIN_BUTTON_MODE, false, false, 0, 0, false, false, 0,
      CONTROL_MODE, CONTROL_NONE },
    { PIN_BUTTON_ROOT_DOWN, false, false, 0, 0, false, false, 0,
      CONTROL_VOLUME_DOWN, CONTROL_ROOT_DOWN },
    { PIN_BUTTON_ROOT_UP, false, false, 0, 0, false, false, 0,
      CONTROL_VOLUME_UP, CONTROL_ROOT_UP },
#endif
};

static uint32_t next_scan_us;
static bool mode_chord_used;
static bool mode_long_fired;
static uint32_t mode_pressed_us;
static bool mode_click_pending;
static uint32_t mode_click_deadline_us;
static uint8_t mode_click_count;
static bool raw_chord_active;
static bool raw_chord_fired;
static uint32_t raw_chord_started_us;
static bool midi_chord_active;
static bool midi_chord_fired;
static uint32_t midi_chord_started_us;

static bool deferred_pair_button(unsigned index) {
    return index == SENS_DOWN_INDEX || index == SENS_UP_INDEX ||
           index == VOLUME_DOWN_INDEX || index == VOLUME_UP_INDEX;
}

static control_event_t event_for_button(unsigned index, bool modified) {
    return modified ? buttons[index].modified_event : buttons[index].direct_event;
}

void controls_init(void) {
    for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
        gpio_init(buttons[i].pin);
        gpio_set_dir(buttons[i].pin, GPIO_IN);
#if PICO_RP2350
        // Pimoroni Display Pack buttons pull their GPIOs low when pressed.
        // The Pocket SCION PCB provides its own biasing, but the standalone
        // Pico 2 setup needs an explicit idle-high state.
        gpio_pull_up(buttons[i].pin);
#else
        gpio_disable_pulls(buttons[i].pin);
#endif
    }
    gpio_init(PIN_AUX_ACTIVE_LOW);
    gpio_set_dir(PIN_AUX_ACTIVE_LOW, GPIO_IN);
    gpio_disable_pulls(PIN_AUX_ACTIVE_LOW);
    gpio_init(PIN_AUX_ACTIVE_HIGH);
    gpio_set_dir(PIN_AUX_ACTIVE_HIGH, GPIO_IN);
    gpio_disable_pulls(PIN_AUX_ACTIVE_HIGH);
    next_scan_us = time_us_32();
}

control_event_t controls_poll(void) {
    uint32_t now = time_us_32();
    if ((int32_t)(now - next_scan_us) < 0) return CONTROL_NONE;
    next_scan_us = now + SCAN_PERIOD_US;

    bool pressed_event[BUTTON_COUNT] = { false };
    bool released_event[BUTTON_COUNT] = { false };

    for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
        button_t *button = &buttons[i];
        bool pressed = !gpio_get(button->pin);
        if (pressed != button->candidate_pressed) {
            button->candidate_pressed = pressed;
            button->candidate_count = 1;
        } else if (button->candidate_count < 4u) {
            button->candidate_count++;
        }

        if (button->candidate_count == 4u && pressed != button->stable_pressed) {
            button->stable_pressed = pressed;
            if (pressed) {
                pressed_event[i] = true;
                button->next_repeat_us = now + REPEAT_DELAY_US;
                button->action_sent = false;
                button->long_fired = false;
                button->pressed_us = now;
            } else {
                released_event[i] = true;
            }
        }
    }

    #if !PICO_RP2350
    if (pressed_event[MODE_BUTTON_INDEX]) {
        mode_chord_used = false;
        mode_long_fired = false;
        mode_pressed_us = now;
    }
    #else
    // The Display Pack X button is a direct parameter control, not the
    // Pocket SCION's multi-click Mode button.
    if (pressed_event[MODE_BUTTON_INDEX]) return CONTROL_PARAMETER_INCREASE;
    #endif

    if (!buttons[MODE_BUTTON_INDEX].stable_pressed && !raw_chord_active &&
        buttons[SENS_DOWN_INDEX].stable_pressed &&
        buttons[SENS_UP_INDEX].stable_pressed) {
        raw_chord_active = true;
        raw_chord_fired = false;
        raw_chord_started_us = now;
        buttons[SENS_DOWN_INDEX].action_sent = true;
        buttons[SENS_UP_INDEX].action_sent = true;
    }
    if (!buttons[MODE_BUTTON_INDEX].stable_pressed && !midi_chord_active &&
        buttons[VOLUME_DOWN_INDEX].stable_pressed &&
        buttons[VOLUME_UP_INDEX].stable_pressed) {
        midi_chord_active = true;
        midi_chord_fired = false;
        midi_chord_started_us = now;
        buttons[VOLUME_DOWN_INDEX].action_sent = true;
        buttons[VOLUME_UP_INDEX].action_sent = true;
    }

    if (raw_chord_active &&
        !buttons[SENS_DOWN_INDEX].stable_pressed &&
        !buttons[SENS_UP_INDEX].stable_pressed) {
        raw_chord_active = false;
        raw_chord_fired = false;
        return CONTROL_NONE;
    }
    if (midi_chord_active &&
        !buttons[VOLUME_DOWN_INDEX].stable_pressed &&
        !buttons[VOLUME_UP_INDEX].stable_pressed) {
        midi_chord_active = false;
        midi_chord_fired = false;
        return CONTROL_NONE;
    }
    if (raw_chord_active && !raw_chord_fired &&
        (int32_t)(now - raw_chord_started_us) >= (int32_t)DUAL_HOLD_US) {
        raw_chord_fired = true;
        return CONTROL_RAW_MODE_TOGGLE;
    }
    if (midi_chord_active && !midi_chord_fired &&
        (int32_t)(now - midi_chord_started_us) >= (int32_t)DUAL_HOLD_US) {
        midi_chord_fired = true;
        return CONTROL_MIDI_MODE_TOGGLE;
    }

    for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
        if (i == MODE_BUTTON_INDEX || !pressed_event[i]) continue;
        if ((raw_chord_active && i <= SENS_UP_INDEX) ||
            (midi_chord_active && i >= VOLUME_DOWN_INDEX)) continue;
        bool modified = false;
#if !PICO_RP2350
        modified = buttons[MODE_BUTTON_INDEX].stable_pressed;
#endif
        if (!modified && deferred_pair_button(i)) continue;
        if (modified) {
            mode_chord_used = true;
            mode_click_pending = false;
            mode_click_count = 0u;
        }
        buttons[i].action_sent = true;
        return event_for_button(i, modified);
    }

    #if !PICO_RP2350
    if (released_event[MODE_BUTTON_INDEX] && !mode_chord_used) {
        if (mode_click_pending &&
            (int32_t)(now - mode_click_deadline_us) > 0) {
            mode_click_count = 0u;
        }
        ++mode_click_count;
        if (mode_click_count >= 3u) {
            mode_click_count = 0u;
            mode_click_pending = false;
            return CONTROL_MIDI_MODE_TOGGLE;
        }
        mode_click_pending = true;
        mode_click_deadline_us = now + DOUBLE_CLICK_US;
    }
    #endif

    for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
        if (!released_event[i] || !deferred_pair_button(i) ||
            buttons[i].action_sent) continue;
        if ((raw_chord_active && i <= SENS_UP_INDEX) ||
            (midi_chord_active && i >= VOLUME_DOWN_INDEX)) continue;
        buttons[i].action_sent = true;
        return buttons[i].direct_event;
    }

    #if !PICO_RP2350
    if (buttons[MODE_BUTTON_INDEX].stable_pressed && !mode_chord_used &&
        !mode_long_fired &&
        (int32_t)(now - mode_pressed_us) >= (int32_t)BANK_HOLD_US) {
        mode_long_fired = true;
        mode_chord_used = true;
        mode_click_pending = false;
        mode_click_count = 0u;
        return CONTROL_PITCH_BEND_TOGGLE;
    }
    #endif

    for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
        if (i < 2u && buttons[i].stable_pressed && !buttons[i].long_fired &&
            (int32_t)(now - buttons[i].pressed_us) >= (int32_t)BANK_HOLD_US) {
            buttons[i].long_fired = true;
            return i == 0u ? CONTROL_PARAMETER_ENTER : CONTROL_PARAMETER_BACK;
        }
        if (!buttons[i].stable_pressed) continue;
#if !PICO_RP2350
        if (i == MODE_BUTTON_INDEX) continue;
#endif
        if ((raw_chord_active && i <= SENS_UP_INDEX) ||
            (midi_chord_active && i >= VOLUME_DOWN_INDEX)) continue;
        if ((int32_t)(now - buttons[i].next_repeat_us) >= 0) {
            buttons[i].next_repeat_us = now + REPEAT_PERIOD_US;
            bool modified = false;
#if !PICO_RP2350
            modified = buttons[MODE_BUTTON_INDEX].stable_pressed;
#endif
            if (modified) {
                mode_chord_used = true;
                mode_click_pending = false;
                mode_click_count = 0u;
            }
            buttons[i].action_sent = true;
            return event_for_button(i, modified);
        }
    }

    #if !PICO_RP2350
    if (mode_click_pending && !buttons[MODE_BUTTON_INDEX].stable_pressed &&
        (int32_t)(now - mode_click_deadline_us) >= 0) {
        mode_click_pending = false;
        uint8_t count = mode_click_count;
        mode_click_count = 0u;
        return count == 2u ? CONTROL_BANK_NEXT : CONTROL_MODE;
    }
    #endif

    return CONTROL_NONE;
}
