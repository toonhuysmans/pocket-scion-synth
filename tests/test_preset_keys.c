#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "preset_store.h"

int main(void) {
    bool seen[PRESET_STORE_KEY_COUNT] = {false};
    for (uint16_t patch = 0u; patch < PRESET_STORE_PATCH_COUNT; ++patch) {
        uint16_t key = preset_store_patch_key(patch);
        assert(key < PRESET_STORE_KEY_COUNT && !seen[key]);
        seen[key] = true;
        if (patch < 128u) assert(key == patch);
    }
    for (uint16_t patch = 0u; patch < PRESET_STORE_PATCH_COUNT; ++patch) {
        uint16_t key = preset_store_speech_patch_key(patch);
        assert(key < PRESET_STORE_KEY_COUNT && !seen[key]);
        seen[key] = true;
    }
    for (uint8_t extension = 0u; extension < 2u; ++extension) {
        for (uint16_t patch = 0u; patch < PRESET_STORE_PATCH_COUNT; ++patch) {
            uint16_t key = preset_store_speech_extension_key(patch, extension);
            assert(key < PRESET_STORE_KEY_COUNT && !seen[key]);
            seen[key] = true;
        }
    }
    for (uint8_t bank = 0u; bank < PRESET_STORE_BANK_COUNT; ++bank) {
        uint16_t key = preset_store_bank_key(bank);
        assert(key < PRESET_STORE_KEY_COUNT && !seen[key]);
        seen[key] = true;
        if (bank < 8u) assert(key == (uint16_t)(128u + bank));
    }
    assert(!seen[PRESET_STORE_GLOBAL_KEY]);
    seen[PRESET_STORE_GLOBAL_KEY] = true;
    for (uint16_t key = 0u; key < PRESET_STORE_KEY_COUNT; ++key) {
        assert(seen[key]);
    }
    assert(preset_store_patch_key(256u) == UINT16_MAX);
    assert(preset_store_bank_key(16u) == UINT16_MAX);
    assert(preset_store_speech_extension_key(256u, 0u) == UINT16_MAX);
    assert(preset_store_speech_extension_key(0u, 2u) == UINT16_MAX);
    puts("preset keys: all tests passed");
    return 0;
}
