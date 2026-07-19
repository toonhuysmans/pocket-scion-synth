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
    PRESET_STORE_KEY_COUNT = 273,
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
