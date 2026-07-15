#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_FRAMES_PER_BUFFER 256u

void audio_i2s_init(void);
bool audio_i2s_take_buffer(uint32_t **frames);
void audio_i2s_submit_buffer(uint32_t *frames);
uint32_t audio_i2s_underruns(void);
