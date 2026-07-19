#include "editor_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "midi_uart.h"
#include "preset_store.h"

#define PROTOCOL_VERSION 1u
#define MANUFACTURER_DEVELOPMENT 0x7du
#define PRODUCT_0 0x50u
#define PRODUCT_1 0x53u
#define RX_CAPACITY 32u

enum {
    COMMAND_HELLO = 0x01,
    COMMAND_SELECT = 0x02,
    COMMAND_GET = 0x03,
    COMMAND_SET = 0x04,
    COMMAND_COMMIT = 0x05,
    COMMAND_REVERT = 0x06,
    COMMAND_RESTORE = 0x07,
    COMMAND_SENSOR_SNAPSHOT = 0x08,
    RESPONSE_ACK = 0x40,
    RESPONSE_CAPABILITIES = 0x41,
    RESPONSE_VALUE = 0x42,
    RESPONSE_SENSOR_SNAPSHOT = 0x43,
    RESPONSE_NACK = 0x7f,
};

static synth_t *controlled_synth;
static uint8_t receive_buffer[RX_CAPACITY];
static uint8_t receive_length;
static bool receiving;

static uint8_t checksum(const uint8_t *bytes, size_t length) {
    uint8_t sum = 0u;
    for (size_t index = 0u; index < length; ++index) {
        sum = (uint8_t)((sum + bytes[index]) & 0x7fu);
    }
    return (uint8_t)((128u - sum) & 0x7fu);
}

static void send_response(uint8_t command, uint8_t request,
                          const uint8_t *payload, uint8_t payload_length) {
    uint8_t message[32];
    uint8_t length = 0u;
    message[length++] = 0xf0u;
    message[length++] = MANUFACTURER_DEVELOPMENT;
    message[length++] = PRODUCT_0;
    message[length++] = PRODUCT_1;
    message[length++] = PROTOCOL_VERSION;
    message[length++] = command;
    message[length++] = request;
    for (uint8_t index = 0u; index < payload_length; ++index) {
        message[length++] = payload[index] & 0x7fu;
    }
    message[length] = checksum(&message[1], (size_t)length - 1u);
    ++length;
    message[length++] = 0xf7u;
    (void)midi_usb_send_sysex(message, length);
}

static void send_ack(uint8_t request, uint8_t command) {
    const uint8_t payload[] = {command, 0u};
    send_response(RESPONSE_ACK, request, payload, sizeof(payload));
}

static void send_nack(uint8_t request, uint8_t command, uint8_t error) {
    const uint8_t payload[] = {command, error};
    send_response(RESPONSE_NACK, request, payload, sizeof(payload));
}

static void process_message(void) {
    if (receive_length < 7u || receive_buffer[0] != MANUFACTURER_DEVELOPMENT ||
        receive_buffer[1] != PRODUCT_0 || receive_buffer[2] != PRODUCT_1 ||
        receive_buffer[3] != PROTOCOL_VERSION ||
        checksum(receive_buffer, receive_length) != 0u) return;

    const uint8_t command = receive_buffer[4];
    const uint8_t request = receive_buffer[5];
    const uint8_t *payload = &receive_buffer[6];
    const uint8_t payload_length = (uint8_t)(receive_length - 7u);
    bool ok = false;

    if (command == COMMAND_HELLO && payload_length == 0u) {
        const uint8_t capabilities[] = {
            2u, 5u, 0u,
            (uint8_t)(PRESET_STORE_PATCH_COUNT & 0x7fu),
            (uint8_t)((PRESET_STORE_PATCH_COUNT >> 7u) & 0x7fu),
            PRESET_STORE_BANK_COUNT,
            SYNTH_EDITOR_SCENE_PARAMETER_COUNT,
            SYNTH_EDITOR_PATCH_SHARED_COUNT,
            SYNTH_EDITOR_BANK_PARAMETER_COUNT,
            SYNTH_EDITOR_GLOBAL_PARAMETER_COUNT,
            (uint8_t)(preset_store_flash_size() / (1024u * 1024u)),
        };
        send_response(RESPONSE_CAPABILITIES, request, capabilities,
                      sizeof(capabilities));
        return;
    }
    if (command == COMMAND_SELECT &&
        (payload_length == 1u || payload_length == 2u)) {
        const uint16_t target = payload_length == 1u ? payload[0] :
            (uint16_t)(payload[0] | ((uint16_t)payload[1] << 7u));
        ok = synth_editor_select(controlled_synth, target);
    } else if (command == COMMAND_GET &&
               (payload_length == 4u || payload_length == 5u)) {
        const bool extended = payload_length == 5u;
        const uint16_t target = extended
            ? (uint16_t)(payload[1] | ((uint16_t)payload[2] << 7u))
            : payload[1];
        const uint8_t lane = payload[extended ? 3u : 2u];
        const uint8_t parameter = payload[extended ? 4u : 3u];
        uint16_t value;
        ok = synth_editor_get(controlled_synth, payload[0], target,
                              lane, parameter, &value);
        if (ok) {
            uint8_t response[7] = {payload[0], (uint8_t)(target & 0x7fu)};
            uint8_t length = 0u;
            if (extended) {
                response[2] = (uint8_t)((target >> 7u) & 0x7fu);
                response[3] = lane;
                response[4] = parameter;
                response[5] = (uint8_t)(value & 0x7fu);
                response[6] = (uint8_t)((value >> 7u) & 0x7fu);
                length = 7u;
            } else {
                response[2] = lane;
                response[3] = parameter;
                response[4] = (uint8_t)(value & 0x7fu);
                response[5] = (uint8_t)((value >> 7u) & 0x7fu);
                length = 6u;
            }
            send_response(RESPONSE_VALUE, request, response, length);
            return;
        }
    } else if (command == COMMAND_SET &&
               (payload_length == 6u || payload_length == 7u)) {
        const bool extended = payload_length == 7u;
        const uint16_t target = extended
            ? (uint16_t)(payload[1] | ((uint16_t)payload[2] << 7u))
            : payload[1];
        const uint8_t lane_index = extended ? 3u : 2u;
        const uint8_t parameter_index = extended ? 4u : 3u;
        const uint8_t value_index = extended ? 5u : 4u;
        uint16_t value = (uint16_t)(payload[value_index] |
            ((uint16_t)payload[value_index + 1u] << 7u));
        ok = synth_editor_set(controlled_synth, payload[0], target,
                              payload[lane_index], payload[parameter_index],
                              value);
    } else if ((command == COMMAND_COMMIT || command == COMMAND_REVERT ||
                command == COMMAND_RESTORE) &&
               (payload_length == 2u || payload_length == 3u)) {
        const uint16_t target = payload_length == 2u ? payload[1] :
            (uint16_t)(payload[1] | ((uint16_t)payload[2] << 7u));
        if (command == COMMAND_COMMIT) {
            ok = synth_editor_commit(controlled_synth, payload[0], target);
        } else if (command == COMMAND_REVERT) {
            ok = synth_editor_revert(controlled_synth, payload[0], target);
        } else {
            ok = synth_editor_restore(controlled_synth, payload[0], target);
        }
    } else if (command == COMMAND_SENSOR_SNAPSHOT && payload_length == 0u) {
        uint8_t response[8];
        for (uint8_t parameter = 0u; parameter < 4u; ++parameter) {
            uint16_t value = 0u;
            if (!synth_editor_get(controlled_synth,
                                  SYNTH_EDITOR_SCOPE_SENSOR, 0u, 0u,
                                  parameter, &value)) {
                send_nack(request, command, 2u);
                return;
            }
            response[parameter * 2u] = (uint8_t)(value & 0x7fu);
            response[parameter * 2u + 1u] = (uint8_t)((value >> 7u) & 0x7fu);
        }
        send_response(RESPONSE_SENSOR_SNAPSHOT, request, response,
                      sizeof(response));
        return;
    } else {
        send_nack(request, command, 1u);
        return;
    }
    if (ok) send_ack(request, command);
    else send_nack(request, command, 2u);
}

static void receive_byte(uint8_t byte) {
    if (byte == 0xf0u) {
        receive_length = 0u;
        receiving = true;
        return;
    }
    if (!receiving) return;
    if (byte == 0xf7u) {
        receiving = false;
        process_message();
        return;
    }
    if ((byte & 0x80u) != 0u || receive_length >= RX_CAPACITY) {
        receiving = false;
        receive_length = 0u;
        return;
    }
    receive_buffer[receive_length++] = byte;
}

void editor_protocol_init(synth_t *synth) {
    controlled_synth = synth;
    receive_length = 0u;
    receiving = false;
    midi_set_sysex_byte_handler(receive_byte);
}
