#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SAM_VOICE_PHRASE_COUNT 10u
#define SAM_VOICE_PHRASE_LENGTH 48u
#define SAM_VOICE_PARAMETER_COUNT 10u

typedef struct {
    uint8_t enabled;
    uint8_t level;
    uint8_t speed;
    uint8_t pitch;
    uint8_t mouth;
    uint8_t throat;
} sam_voice_character_t;

typedef struct {
    uint8_t enabled;
    uint8_t level;
    uint8_t speed;
    uint8_t pitch;
    uint8_t mouth;
    uint8_t throat;
    uint8_t density;
    uint8_t sensor_influence;
    uint8_t motion_chance;
    uint8_t motion_amount;
    char phrase[SAM_VOICE_PHRASE_COUNT][SAM_VOICE_PHRASE_LENGTH];
} sam_voice_patch_t;

#ifdef __cplusplus
extern "C" {
#endif

void sam_voice_init(void);
bool sam_voice_request(const char *text,
                       const sam_voice_character_t *parameters);
bool sam_voice_core1_service(void);
bool sam_voice_render_busy(void);
bool sam_voice_active(void);
int16_t sam_voice_next_sample(void);

#ifdef __cplusplus
}
#endif
