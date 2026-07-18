#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "editor_protocol.h"

static void (*receive_handler)(uint8_t byte);
static uint8_t response[96];
static uint16_t response_length;
static uint8_t selected_patch;
static uint16_t set_value;

uint32_t preset_store_flash_size(void) { return 2u * 1024u * 1024u; }

bool midi_usb_send_sysex(const uint8_t *bytes, uint16_t length) {
    assert(length <= sizeof(response));
    memcpy(response, bytes, length);
    response_length = length;
    return true;
}

void midi_set_sysex_byte_handler(void (*handler)(uint8_t byte)) {
    receive_handler = handler;
}

bool synth_editor_select(synth_t *synth, uint8_t patch_id) {
    (void)synth;
    selected_patch = patch_id;
    return patch_id < 128u;
}

bool synth_editor_get(const synth_t *synth, uint8_t scope, uint8_t target,
                      uint8_t lane, uint8_t parameter, uint16_t *value) {
    (void)synth; (void)scope; (void)target; (void)lane; (void)parameter;
    *value = 1000u;
    return true;
}

bool synth_editor_set(synth_t *synth, uint8_t scope, uint8_t target,
                      uint8_t lane, uint8_t parameter, uint16_t value) {
    (void)synth; (void)scope; (void)target; (void)lane; (void)parameter;
    set_value = value;
    return true;
}

bool synth_editor_commit(const synth_t *synth, uint8_t scope, uint8_t target) {
    (void)synth; (void)scope; (void)target; return true;
}
bool synth_editor_revert(synth_t *synth, uint8_t scope, uint8_t target) {
    (void)synth; (void)scope; (void)target; return true;
}
bool synth_editor_restore(synth_t *synth, uint8_t scope, uint8_t target) {
    (void)synth; (void)scope; (void)target; return true;
}

static uint8_t checksum(const uint8_t *bytes, unsigned length) {
    uint8_t sum = 0u;
    for (unsigned index = 0u; index < length; ++index) {
        sum = (uint8_t)((sum + bytes[index]) & 0x7fu);
    }
    return (uint8_t)((128u - sum) & 0x7fu);
}

static void send_request(uint8_t command, uint8_t request,
                         const uint8_t *payload, uint8_t payload_length) {
    uint8_t message[32] = {0xf0u, 0x7du, 0x50u, 0x53u, 1u, command, request};
    memcpy(&message[7], payload, payload_length);
    unsigned length = 7u + payload_length;
    message[length] = checksum(&message[1], length - 1u);
    message[length + 1u] = 0xf7u;
    for (unsigned index = 0u; index < length + 2u; ++index) {
        receive_handler(message[index]);
    }
}

static void assert_response(uint8_t command, uint8_t request) {
    assert(response_length >= 9u);
    assert(response[0] == 0xf0u && response[response_length - 1u] == 0xf7u);
    assert(response[1] == 0x7du && response[2] == 0x50u &&
           response[3] == 0x53u && response[4] == 1u);
    assert(response[5] == command && response[6] == request);
    assert(checksum(&response[1], response_length - 2u) == 0u);
}

int main(void) {
    synth_t synth;
    editor_protocol_init(&synth);
    assert(receive_handler != NULL);

    send_request(0x01u, 7u, NULL, 0u);
    assert_response(0x41u, 7u);
    assert(response[10] == 0u && response[11] == 1u);  // 128 patches.
    assert(response[12] == 8u);
    assert(response[13] == SYNTH_EDITOR_SCENE_PARAMETER_COUNT);
    assert(response[14] == SYNTH_EDITOR_PATCH_SHARED_COUNT);

    const uint8_t select[] = {42u};
    send_request(0x02u, 8u, select, sizeof(select));
    assert_response(0x40u, 8u);
    assert(selected_patch == 42u);

    const uint8_t set[] = {0u, 42u, 2u, 23u, 112u, 1u};
    send_request(0x04u, 9u, set, sizeof(set));
    assert_response(0x40u, 9u);
    assert(set_value == 240u);

    response_length = 0u;
    send_request(0x08u, 10u, NULL, 0u);
    assert_response(0x43u, 10u);
    assert(response_length == 17u);
    for (unsigned parameter = 0u; parameter < 4u; ++parameter) {
        assert(response[7u + parameter * 2u] == (1000u & 0x7fu));
        assert(response[8u + parameter * 2u] == (1000u >> 7u));
    }

    uint8_t corrupt[] = {0xf0u, 0x7du, 0x50u, 0x53u, 1u, 1u, 10u, 1u, 0xf7u};
    response_length = 0u;
    for (unsigned index = 0u; index < sizeof(corrupt); ++index) {
        receive_handler(corrupt[index]);
    }
    assert(response_length == 0u);

    puts("editor protocol: all tests passed");
    return 0;
}
