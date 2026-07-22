#include "midi_uart.h"

#include <stdbool.h>

#include "board_pins.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/platform.h"
#include "tusb.h"

#define MIDI_UART uart0
#define MIDI_QUEUE_CAPACITY 128u

typedef struct {
    uint8_t bytes[3];
    uint8_t length;
    uint8_t uart_index;
    bool usb_sent;
} midi_event_t;

static midi_event_t queue[MIDI_QUEUE_CAPACITY];
static uint8_t queue_read;
static uint8_t queue_write;
static void (*sysex_byte_handler)(uint8_t byte);
static void (*note_input_handler)(uint8_t note, bool pressed);
static void (*chord_reset_input_handler)(bool clear_latch);
static uint8_t sysex_tx[96];
static uint16_t sysex_tx_length;
static uint16_t sysex_tx_position;

static void __not_in_flash_func(enqueue)(uint8_t status, uint8_t data1,
                                         uint8_t data2, uint8_t length) {
    uint8_t next = (uint8_t)((queue_write + 1u) % MIDI_QUEUE_CAPACITY);
    if (next == queue_read) return;
    midi_event_t *event = &queue[queue_write];
    event->bytes[0] = status;
    event->bytes[1] = data1 & 0x7fu;
    event->bytes[2] = data2 & 0x7fu;
    event->length = length;
    event->uart_index = 0u;
    event->usb_sent = false;
    queue_write = next;
}

static void enqueue_usb_only(uint8_t status, uint8_t data1, uint8_t data2) {
    uint8_t previous_write = queue_write;
    enqueue(status, data1, data2, 3u);
    if (queue_write != previous_write) queue[previous_write].uart_index = 0xffu;
}

void midi_uart_init(void) {
    uart_init(MIDI_UART, 31250u);
    gpio_set_function(PIN_MIDI_TX, GPIO_FUNC_UART);
    uart_set_format(MIDI_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(MIDI_UART, true);

    tusb_rhport_init_t device = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    tusb_init(0u, &device);
}

void midi_service(void) {
    tud_task();

    while (tud_midi_available()) {
        uint8_t packet[4];
        if (!tud_midi_packet_read(packet)) break;
        uint8_t cin = packet[0] & 0x0fu;
        uint8_t count = cin == 0x05u ? 1u : cin == 0x06u ? 2u : 3u;
        if (cin >= 0x04u && cin <= 0x07u &&
            sysex_byte_handler != NULL) {
            for (uint8_t index = 0u; index < count; ++index) {
                sysex_byte_handler(packet[index + 1u]);
            }
        } else if ((cin == 0x08u || cin == 0x09u) &&
                   note_input_handler != NULL) {
            const bool pressed = cin == 0x09u && packet[3] != 0u;
            note_input_handler((uint8_t)(packet[2] & 0x7fu), pressed);
        } else if (cin == 0x0bu && chord_reset_input_handler != NULL &&
                   (packet[2] == 120u || packet[2] == 123u)) {
            chord_reset_input_handler(packet[2] == 120u);
        }
    }

    if (sysex_tx_position < sysex_tx_length && tud_midi_mounted()) {
        uint32_t sent = tud_midi_stream_write(
            0u, &sysex_tx[sysex_tx_position],
            (uint32_t)(sysex_tx_length - sysex_tx_position));
        sysex_tx_position = (uint16_t)(sysex_tx_position + sent);
        if (sysex_tx_position == sysex_tx_length) {
            sysex_tx_position = 0u;
            sysex_tx_length = 0u;
        }
    }

    while (queue_read != queue_write) {
        midi_event_t *event = &queue[queue_read];
        if (!event->usb_sent) {
            if (!tud_midi_mounted()) {
                event->usb_sent = true;
            } else {
                uint8_t cin = (uint8_t)(event->bytes[0] >> 4);
                uint8_t packet[4] = {
                    cin, event->bytes[0], event->bytes[1], event->bytes[2]
                };
                if (!tud_midi_packet_write(packet)) return;
                event->usb_sent = true;
            }
        }

        if (event->uart_index == 0xffu) event->uart_index = event->length;
        while (event->uart_index < event->length &&
               uart_is_writable(MIDI_UART)) {
            uart_putc_raw(MIDI_UART, event->bytes[event->uart_index++]);
        }
        if (event->uart_index < event->length) return;
        queue_read = (uint8_t)((queue_read + 1u) % MIDI_QUEUE_CAPACITY);
    }
}

bool midi_usb_send_sysex(const uint8_t *bytes, uint16_t length) {
    if (bytes == NULL || length == 0u || length > sizeof(sysex_tx) ||
        sysex_tx_length != 0u || !tud_midi_mounted()) return false;
    for (uint16_t index = 0u; index < length; ++index) {
        sysex_tx[index] = bytes[index];
    }
    sysex_tx_length = length;
    sysex_tx_position = 0u;
    return true;
}

void midi_set_sysex_byte_handler(void (*handler)(uint8_t byte)) {
    sysex_byte_handler = handler;
}

void midi_set_note_input_handler(void (*handler)(uint8_t note, bool pressed)) {
    note_input_handler = handler;
}

void midi_set_chord_reset_input_handler(void (*handler)(bool clear_latch)) {
    chord_reset_input_handler = handler;
}

bool midi_usb_mounted(void) {
    return tud_midi_mounted();
}

void midi_discard_pending(void) {
    queue_read = queue_write;
}

void __not_in_flash_func(midi_note_on)(uint8_t channel, uint8_t note,
                                      uint8_t velocity) {
    enqueue((uint8_t)(0x90u | (channel & 0x0fu)), note, velocity, 3u);
}

void __not_in_flash_func(midi_note_off)(uint8_t channel, uint8_t note) {
    enqueue((uint8_t)(0x80u | (channel & 0x0fu)), note, 0u, 3u);
}

void midi_control_change(uint8_t channel, uint8_t control, uint8_t value) {
    enqueue((uint8_t)(0xb0u | (channel & 0x0fu)), control, value, 3u);
}

void midi_usb_control_change(uint8_t channel, uint8_t control, uint8_t value) {
    enqueue_usb_only((uint8_t)(0xb0u | (channel & 0x0fu)), control, value);
}

void midi_program_change(uint8_t channel, uint8_t program) {
    enqueue((uint8_t)(0xc0u | (channel & 0x0fu)), program, 0u, 2u);
}

void midi_pitch_bend(uint8_t channel, uint16_t value) {
    if (value > 16383u) value = 16383u;
    enqueue((uint8_t)(0xe0u | (channel & 0x0fu)),
            (uint8_t)(value & 127u), (uint8_t)(value >> 7), 3u);
}
