#include <stddef.h>
#include <string.h>

#include "tusb.h"

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x2e8a,
    .idProduct = 0x10a1,
    .bcdDevice = 0x0220,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&device_descriptor;
}

enum {
    ITF_NUM_MIDI,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL,
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)
#define EPNUM_MIDI_OUT 0x01
#define EPNUM_MIDI_IN  0x81

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT,
                        EPNUM_MIDI_IN, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return configuration_descriptor;
}

static const char *const strings[] = {
    (const char[]){0x09, 0x04},
    "Instruo",
    "Pocket SCION Synth",
    "172839",
};
static uint16_t string_descriptor[32];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t count;
    if (index == 0u) {
        memcpy(&string_descriptor[1], strings[0], 2u);
        count = 1u;
    } else {
        if (index >= sizeof(strings) / sizeof(strings[0])) return NULL;
        const char *text = strings[index];
        count = strlen(text);
        if (count > 31u) count = 31u;
        for (size_t i = 0; i < count; ++i) {
            string_descriptor[i + 1u] = (uint8_t)text[i];
        }
    }
    string_descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) |
                                      (2u * count + 2u));
    return string_descriptor;
}
