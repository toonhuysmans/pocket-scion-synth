#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PRESET_STORE_PATCH_COUNT = 256,
    PRESET_STORE_BANK_COUNT = 16,
    PRESET_STORE_GLOBAL_KEY = 136,
    PRESET_STORE_EXTENDED_PATCH_BASE = 137,
    PRESET_STORE_EXTENDED_BANK_BASE = 265,
    PRESET_STORE_SPEECH_PATCH_BASE = 273,
    PRESET_STORE_SPEECH_EXTENSION_A_BASE = 529,
    PRESET_STORE_SPEECH_EXTENSION_B_BASE = 785,
    PRESET_STORE_KEY_COUNT = 1041,
};

// Preserve every v2.4 key while placing the upper 128 patches and eight banks
// after the legacy global record. UINT16_MAX reports an out-of-range ID.
static inline uint16_t preset_store_patch_key(uint16_t patch_id) {
    if (patch_id >= PRESET_STORE_PATCH_COUNT) return UINT16_MAX;
    if (patch_id < 128u) return patch_id;
    return (uint16_t)(PRESET_STORE_EXTENDED_PATCH_BASE + patch_id - 128u);
}

static inline uint16_t preset_store_bank_key(uint8_t bank_id) {
    if (bank_id >= PRESET_STORE_BANK_COUNT) return UINT16_MAX;
    if (bank_id < 8u) return (uint16_t)(128u + bank_id);
    return (uint16_t)(PRESET_STORE_EXTENDED_BANK_BASE + bank_id - 8u);
}

static inline uint16_t preset_store_speech_patch_key(uint16_t patch_id) {
    return patch_id < PRESET_STORE_PATCH_COUNT
        ? (uint16_t)(PRESET_STORE_SPEECH_PATCH_BASE + patch_id)
        : UINT16_MAX;
}

static inline uint16_t preset_store_speech_extension_key(uint16_t patch_id,
                                                         uint8_t extension) {
    if (patch_id >= PRESET_STORE_PATCH_COUNT || extension > 1u) {
        return UINT16_MAX;
    }
    const uint16_t base = extension == 0u
        ? PRESET_STORE_SPEECH_EXTENSION_A_BASE
        : PRESET_STORE_SPEECH_EXTENSION_B_BASE;
    return (uint16_t)(base + patch_id);
}

bool preset_store_init(void);
uint32_t preset_store_flash_size(void);
bool preset_store_load(uint16_t key, void *payload, size_t payload_size);
// Loads the newest record when it fits in payload_capacity and reports its
// original size. Callers can prefill appended fields with compiled defaults,
// allowing older prefix-compatible records to migrate without being erased.
bool preset_store_load_prefix(uint16_t key, void *payload,
                              size_t payload_capacity, size_t *payload_size);
bool preset_store_save(uint16_t key, const void *payload, size_t payload_size);
bool preset_store_erase(uint16_t key);

#ifdef __cplusplus
}
#endif
