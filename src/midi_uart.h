#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void midi_uart_init(void);
void midi_service(void);
bool midi_usb_mounted(void);
void midi_discard_pending(void);
void midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
void midi_note_off(uint8_t channel, uint8_t note);
void midi_control_change(uint8_t channel, uint8_t control, uint8_t value);
void midi_program_change(uint8_t channel, uint8_t program);
void midi_pitch_bend(uint8_t channel, uint16_t value);

#ifdef __cplusplus
}
#endif
