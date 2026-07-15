#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void raw_capture_init(void);
bool raw_capture_copy_latest(uint32_t *destination, uint32_t frame_count);

#ifdef __cplusplus
}
#endif
