#include "preset_store.h"

#include <string.h>

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/platform.h"

#define STORE_MAGIC 0x5343494fu
#define STORE_VERSION 2u
#define STORE_KEY_COUNT PRESET_STORE_KEY_COUNT
// 576 KiB provides 1152 journal pages per half. Its upper half contains the
// former final-256-KiB store region, so existing records remain discoverable
// and migrate naturally on the next compaction.
#define STORE_REGION_SIZE (576u * 1024u)
#define STORE_HALF_SIZE (STORE_REGION_SIZE / 2u)
#define STORE_PAGES_PER_HALF (STORE_HALF_SIZE / FLASH_PAGE_SIZE)
#define STORE_SECTORS_PER_HALF (STORE_HALF_SIZE / FLASH_SECTOR_SIZE)
#define STORE_MIN_FLASH_SIZE (1024u * 1024u)
#define FLASH_JEDEC_ID_COMMAND 0x9fu

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint32_t crc32;
    uint16_t key;
    uint16_t payload_size;
    uint8_t version;
    uint8_t reserved[3];
    uint8_t payload[FLASH_PAGE_SIZE - 20u];
} store_page_t;

_Static_assert(sizeof(store_page_t) == FLASH_PAGE_SIZE,
               "Store records must occupy one flash page");

extern uint8_t __flash_binary_end;

static uint32_t detected_flash_size;
static uint32_t store_base_offset;
static bool store_available;

static uint32_t crc32_bytes(const uint8_t *data, size_t length) {
    uint32_t crc = 0xffffffffu;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

bool preset_store_init(void) {
    // Read JEDEC manufacturer/type/capacity before core 1 or audio starts.
    // Standard SPI NOR capacity codes 0x14..0x18 represent 1..16 MiB.
    const uint8_t transmit[4] = {FLASH_JEDEC_ID_COMMAND, 0u, 0u, 0u};
    uint8_t receive[4] = {0u, 0u, 0u, 0u};
    flash_do_cmd(transmit, receive, sizeof(transmit));
    const uint8_t capacity = receive[3];
    if (capacity >= 0x14u && capacity <= 0x18u) {
        detected_flash_size = 1u << capacity;
    } else {
        // Never fall back to the board's unverified 16 MiB maximum. One MiB
        // is conservative and still leaves room for this firmware + journal.
        detected_flash_size = STORE_MIN_FLASH_SIZE;
    }
    store_base_offset = detected_flash_size - STORE_REGION_SIZE;
    const uintptr_t binary_end = (uintptr_t)&__flash_binary_end - XIP_BASE;
    store_available = detected_flash_size >= STORE_MIN_FLASH_SIZE &&
        binary_end <= store_base_offset;
    return store_available;
}

uint32_t preset_store_flash_size(void) {
    return detected_flash_size;
}

static const store_page_t *page_at(unsigned half, unsigned page) {
    uint32_t offset = store_base_offset + half * STORE_HALF_SIZE +
                      page * FLASH_PAGE_SIZE;
    return (const store_page_t *)(XIP_BASE + offset);
}

static bool page_valid_any_size(const store_page_t *page) {
    return page->magic == STORE_MAGIC && page->version == STORE_VERSION &&
        page->key < STORE_KEY_COUNT &&
        page->payload_size <= sizeof(page->payload) &&
        page->crc32 == crc32_bytes(page->payload, page->payload_size);
}

static bool page_valid(const store_page_t *page, uint16_t key,
                       size_t payload_size) {
    return page_valid_any_size(page) && page->key == key &&
           page->payload_size == payload_size;
}

static bool page_blank(const store_page_t *page) {
    const uint32_t *words = (const uint32_t *)page;
    for (unsigned index = 0u; index < FLASH_PAGE_SIZE / sizeof(uint32_t);
         ++index) {
        if (words[index] != 0xffffffffu) return false;
    }
    return true;
}

static bool newest_page(uint16_t key, size_t payload_size,
                        const store_page_t **result, uint32_t *sequence_out) {
    const store_page_t *newest = NULL;
    uint32_t newest_sequence = 0u;
    for (unsigned half = 0u; half < 2u; ++half) {
        for (unsigned page = 0u; page < STORE_PAGES_PER_HALF; ++page) {
            const store_page_t *candidate = page_at(half, page);
            if (!page_valid_any_size(candidate) || candidate->key != key) {
                continue;
            }
            if (newest == NULL ||
                (int32_t)(candidate->sequence - newest_sequence) > 0) {
                newest = candidate;
                newest_sequence = candidate->sequence;
            }
        }
    }
    if (newest == NULL || newest->payload_size == 0u ||
        newest->payload_size != payload_size) return false;
    if (result != NULL) *result = newest;
    if (sequence_out != NULL) *sequence_out = newest_sequence;
    return true;
}

static uint32_t global_newest_sequence(unsigned *half_out) {
    uint32_t newest = 0u;
    unsigned newest_half = 0u;
    bool found = false;
    for (unsigned half = 0u; half < 2u; ++half) {
        for (unsigned page = 0u; page < STORE_PAGES_PER_HALF; ++page) {
            const store_page_t *candidate = page_at(half, page);
            if (!page_valid_any_size(candidate)) continue;
            if (!found || (int32_t)(candidate->sequence - newest) > 0) {
                newest = candidate->sequence;
                newest_half = half;
                found = true;
            }
        }
    }
    if (half_out != NULL) *half_out = newest_half;
    return newest;
}

bool preset_store_load(uint16_t key, void *payload, size_t payload_size) {
    if (!store_available || key >= STORE_KEY_COUNT || payload == NULL ||
        payload_size > sizeof(((store_page_t *)0)->payload)) return false;
    const store_page_t *page;
    if (!newest_page(key, payload_size, &page, NULL)) return false;
    memcpy(payload, page->payload, payload_size);
    return true;
}

bool preset_store_load_prefix(uint16_t key, void *payload,
                              size_t payload_capacity, size_t *payload_size) {
    if (!store_available || key >= STORE_KEY_COUNT || payload == NULL ||
        payload_capacity > sizeof(((store_page_t *)0)->payload)) return false;
    const store_page_t *page = NULL;
    uint32_t sequence = 0u;
    for (unsigned half = 0u; half < 2u; ++half) {
        for (unsigned index = 0u; index < STORE_PAGES_PER_HALF; ++index) {
            const store_page_t *candidate = page_at(half, index);
            if (!page_valid_any_size(candidate) || candidate->key != key) {
                continue;
            }
            if (page == NULL ||
                (int32_t)(candidate->sequence - sequence) > 0) {
                page = candidate;
                sequence = candidate->sequence;
            }
        }
    }
    if (page == NULL || page->payload_size == 0u ||
        page->payload_size > payload_capacity) return false;
    memcpy(payload, page->payload, page->payload_size);
    if (payload_size != NULL) *payload_size = page->payload_size;
    return true;
}

typedef struct {
    uint32_t offset;
    const uint8_t *page;
    bool erase;
} flash_operation_t;

static void __not_in_flash_func(run_flash_operation)(void *context) {
    flash_operation_t *operation = (flash_operation_t *)context;
    if (operation->erase) {
        flash_range_erase(operation->offset, FLASH_SECTOR_SIZE);
    }
    if (operation->page != NULL) {
        flash_range_program(operation->offset, operation->page,
                            FLASH_PAGE_SIZE);
    }
}

static bool execute_flash_operation(flash_operation_t *operation) {
    return flash_safe_execute(run_flash_operation, operation, 250u) == PICO_OK;
}

static bool erase_half(unsigned half) {
    for (unsigned sector = 0u; sector < STORE_SECTORS_PER_HALF; ++sector) {
        flash_operation_t operation = {
            .offset = store_base_offset + half * STORE_HALF_SIZE +
                      sector * FLASH_SECTOR_SIZE,
            .page = NULL,
            .erase = true,
        };
        if (!execute_flash_operation(&operation)) return false;
    }
    return true;
}

static bool program_page(unsigned half, unsigned page_index,
                         const store_page_t *page) {
    flash_operation_t operation = {
        .offset = store_base_offset + half * STORE_HALF_SIZE +
                  page_index * FLASH_PAGE_SIZE,
        .page = (const uint8_t *)page,
        .erase = false,
    };
    if (!execute_flash_operation(&operation)) return false;
    return page_valid(page_at(half, page_index), page->key,
                      page->payload_size);
}

static bool make_page(store_page_t *page, uint16_t key, const void *payload,
                      size_t payload_size, uint32_t sequence) {
    if (payload_size > sizeof(page->payload)) return false;
    memset(page, 0xff, sizeof(*page));
    page->magic = STORE_MAGIC;
    page->sequence = sequence;
    page->key = key;
    page->payload_size = (uint16_t)payload_size;
    page->version = STORE_VERSION;
    memcpy(page->payload, payload, payload_size);
    page->crc32 = crc32_bytes(page->payload, payload_size);
    return true;
}

static int first_blank_page(unsigned half) {
    for (unsigned page = 0u; page < STORE_PAGES_PER_HALF; ++page) {
        if (page_blank(page_at(half, page))) return (int)page;
    }
    return -1;
}

static bool compact_to(unsigned target_half, uint32_t *sequence) {
    // The old half remains intact until every latest record has been copied.
    if (!erase_half(target_half)) return false;
    unsigned target_page = 0u;
    for (uint16_t key = 0u; key < STORE_KEY_COUNT; ++key) {
        const store_page_t *latest = NULL;
        // Payload size is part of the format, so search by key here.
        for (unsigned half = 0u; half < 2u; ++half) {
            for (unsigned page = 0u; page < STORE_PAGES_PER_HALF; ++page) {
                const store_page_t *candidate = page_at(half, page);
                if (!page_valid_any_size(candidate) || candidate->key != key) {
                    continue;
                }
                if (latest == NULL ||
                    (int32_t)(candidate->sequence - latest->sequence) > 0) {
                    latest = candidate;
                }
            }
        }
        if (latest == NULL) continue;
        store_page_t copy = *latest;
        copy.sequence = ++*sequence;
        copy.crc32 = crc32_bytes(copy.payload, copy.payload_size);
        if (!program_page(target_half, target_page++, &copy)) return false;
    }
    return true;
}

bool preset_store_save(uint16_t key, const void *payload, size_t payload_size) {
    if (!store_available || key >= STORE_KEY_COUNT || payload == NULL ||
        payload_size > sizeof(((store_page_t *)0)->payload)) return false;

    unsigned active_half = 0u;
    uint32_t sequence = global_newest_sequence(&active_half);
    int blank = first_blank_page(active_half);
    if (blank < 0) {
        active_half ^= 1u;
        if (!compact_to(active_half, &sequence)) return false;
        blank = first_blank_page(active_half);
        if (blank < 0) return false;
    }

    store_page_t page;
    if (!make_page(&page, key, payload, payload_size, sequence + 1u)) {
        return false;
    }
    return program_page(active_half, (unsigned)blank, &page);
}

bool preset_store_erase(uint16_t key) {
    if (!store_available || key >= STORE_KEY_COUNT) return false;
    // A zero-length tombstone supersedes earlier records without erasing a
    // shared sector. Loads require their exact payload size and ignore it.
    const uint8_t tombstone = 0u;
    return preset_store_save(key, &tombstone, 0u);
}
