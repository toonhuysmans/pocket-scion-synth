#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PRESET_STORE_PATCH_COUNT = 128,
    PRESET_STORE_BANK_COUNT = 8,
    PRESET_STORE_GLOBAL_KEY = 136,
};

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
