#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "editor_protocol.h"

static void (*receive_handler)(uint8_t byte);
static uint8_t response[96];
static uint16_t response_length;
static uint16_t selected_patch;
static uint16_t set_value;
static uint16_t phrase_target;
static char phrase_value[48] = "HELLO PLANT";

uint32_t preset_store_flash_size(void) { return 2u * 1024u * 1024u; }
bool sensor_has_recent_activity(void) { return true; }
uint32_t sensor_dropped_edges(void) { return 3u; }
uint32_t sensor_rejected_edges(void) { return 4u; }
uint16_t sensor_activity_age_ms(void) { return 5u; }

bool midi_usb_send_sysex(const uint8_t *bytes, uint16_t length) {
    assert(length <= sizeof(response));
    memcpy(response, bytes, length);
    response_length = length;
    return true;
}

void midi_set_sysex_byte_handler(void (*handler)(uint8_t byte)) {
    receive_handler = handler;
}

bool synth_editor_select(synth_t *synth, uint16_t patch_id) {
    (void)synth;
    selected_patch = patch_id;
    return patch_id < 256u;
}

bool synth_editor_get(const synth_t *synth, uint8_t scope, uint16_t target,
                      uint8_t lane, uint8_t parameter, uint16_t *value) {
    (void)synth; (void)scope; (void)target; (void)lane; (void)parameter;
    *value = 1000u;
    return true;
}

bool synth_editor_set(synth_t *synth, uint8_t scope, uint16_t target,
                      uint8_t lane, uint8_t parameter, uint16_t value) {
    (void)synth; (void)scope; (void)target; (void)lane; (void)parameter;
    set_value = value;
    return true;
}

bool synth_editor_commit(const synth_t *synth, uint8_t scope, uint16_t target) {
    (void)synth; (void)scope; (void)target; return true;
}
bool synth_editor_revert(synth_t *synth, uint8_t scope, uint16_t target) {
    (void)synth; (void)scope; (void)target; return true;
}
bool synth_editor_restore(synth_t *synth, uint8_t scope, uint16_t target) {
    (void)synth; (void)scope; (void)target; return true;
}
bool synth_editor_get_phrase(synth_t *synth, uint16_t target,
                             uint8_t phrase, char *text, size_t capacity) {
    (void)synth; (void)phrase;
    phrase_target = target;
    strncpy(text, phrase_value, capacity - 1u);
    text[capacity - 1u] = '\0';
    return true;
}
bool synth_editor_set_phrase_chunk(synth_t *synth, uint16_t target,
                                   uint8_t phrase, uint8_t offset,
                                   const uint8_t *text, uint8_t length,
                                   bool final_chunk) {
    (void)synth; (void)target; (void)phrase;
    if (offset == 0u) memset(phrase_value, 0, sizeof(phrase_value));
    memcpy(&phrase_value[offset], text, length);
    if (final_chunk) phrase_value[offset + length] = '\0';
    return true;
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
    uint8_t message[64] = {0xf0u, 0x7du, 0x50u, 0x53u, 1u, command, request};
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
    synth_t synth = {0};
    editor_protocol_init(&synth);
    assert(receive_handler != NULL);

    send_request(0x01u, 7u, NULL, 0u);
    assert_response(0x41u, 7u);
    assert(response[10] == 0u && response[11] == 2u);  // 256 patches.
    assert(response[12] == 16u);
    assert(response[13] == SYNTH_EDITOR_SCENE_PARAMETER_COUNT);
    assert(response[14] == SYNTH_EDITOR_PATCH_SHARED_COUNT);
    assert(response[18] == SYNTH_EDITOR_SPEECH_PARAMETER_COUNT);

    const uint8_t select[] = {42u};
    send_request(0x02u, 8u, select, sizeof(select));
    assert_response(0x40u, 8u);
    assert(selected_patch == 42u);

    const uint8_t select_extended[] = {72u, 1u};  // Patch 200.
    send_request(0x02u, 11u, select_extended, sizeof(select_extended));
    assert_response(0x40u, 11u);
    assert(selected_patch == 200u);

    const uint8_t set[] = {0u, 42u, 2u, 23u, 112u, 1u};
    send_request(0x04u, 9u, set, sizeof(set));
    assert_response(0x40u, 9u);
    assert(set_value == 240u);

    response_length = 0u;
    send_request(0x08u, 10u, NULL, 0u);
    assert_response(0x43u, 10u);
    assert(response_length == 37u);
    for (unsigned parameter = 0u; parameter < 4u; ++parameter) {
        assert(response[7u + parameter * 2u] == (1000u & 0x7fu));
        assert(response[8u + parameter * 2u] == (1000u >> 7u));
    }
    assert(response[27] == 2u);  // Active, not yet calibrated or learning.
    assert(response[29] == 3u);  // Dropped edges.
    assert(response[31] == 4u);  // Rejected fast edges.
    assert(response[33] == 5u);  // Last accepted edge age in milliseconds.

    const uint8_t get_phrase[] = {42u, 0u, 1u};
    send_request(0x09u, 12u, get_phrase, sizeof(get_phrase));
    assert_response(0x44u, 12u);
    assert(response[7] == 1u && response[8] == 11u);
    assert(phrase_target == 42u);

    const uint8_t get_active_phrase[] = {127u, 127u, 1u};
    send_request(0x09u, 14u, get_active_phrase,
                 sizeof(get_active_phrase));
    assert_response(0x44u, 14u);
    assert(phrase_target == 0x3fffu);

    const uint8_t set_phrase[] = {42u, 0u, 2u, 0u, 1u, 'G', 'R', 'O', 'W'};
    send_request(0x0au, 13u, set_phrase, sizeof(set_phrase));
    assert_response(0x40u, 13u);
    assert(strcmp(phrase_value, "GROW") == 0);

    uint8_t corrupt[] = {0xf0u, 0x7du, 0x50u, 0x53u, 1u, 1u, 10u, 1u, 0xf7u};
    response_length = 0u;
    for (unsigned index = 0u; index < sizeof(corrupt); ++index) {
        receive_handler(corrupt[index]);
    }
    assert(response_length == 0u);

    puts("editor protocol: all tests passed");
    return 0;
}
