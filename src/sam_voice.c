#include "sam_voice.h"

#include <ctype.h>
#include <string.h>

#include "hardware/sync.h"
#include "pico.h"

#include "reciter.h"
#include "render.h"
#include "sam.h"

#define SAM_BUFFER_CAPACITY 512u
#define SAM_TEXT_CAPACITY 256u
#define SAM_SOURCE_RATE 22050u
#define SAM_TARGET_RATE 48000u
#define SAM_MAX_ACTIVE_FRAMES (SAM_TARGET_RATE * 60u)
#define SAM_MAX_STALLED_FRAMES (SAM_TARGET_RATE * 5u)

enum {
    VOICE_IDLE = 0u,
    VOICE_REQUESTED,
    VOICE_RENDERING,
    VOICE_READY,
    VOICE_PLAYING,
};

int debug = 0;

static char __scratch_x("sam_stream") output_buffer[SAM_BUFFER_CAPACITY];
// TextToPhonemes expands English text into SAM phonemes in place. A phrase may
// be only 47 characters but its phonetic representation can be several times
// longer, especially for punctuation-separated letters such as "A. B. C.".
static char __scratch_x("sam_text") requested_text[SAM_TEXT_CAPACITY];
static volatile uint32_t voice_state;
static volatile uint32_t produced_samples;
static volatile uint32_t consumed_source_sample;
static volatile uint32_t rendered_samples;
static volatile bool voice_cancelled;
static uint32_t active_target_frames;
static uint32_t stalled_target_frames;
static uint32_t watchdog_produced_samples;
static uint32_t watchdog_playback_position;
static uint32_t watchdog_voice_state;
static uint32_t playback_position_q16;
static uint32_t playback_step_q16;
static uint8_t requested_speed;
static uint8_t requested_pitch;
static uint8_t requested_mouth;
static uint8_t requested_throat;
static uint8_t requested_level;

void sam_voice_init(void) {
    memset(output_buffer, 128, sizeof(output_buffer));
    SetOutputBuffer(output_buffer, (int)sizeof(output_buffer));
    playback_step_q16 = (uint32_t)(((uint64_t)SAM_SOURCE_RATE << 16u) /
                                   SAM_TARGET_RATE);
    voice_state = VOICE_IDLE;
}

static void stream_sample(int position, unsigned char sample) {
    if (position < 0) return;
    uint32_t absolute = (uint32_t)position;
    while (absolute >= consumed_source_sample + SAM_BUFFER_CAPACITY - 8u) {
        if (voice_cancelled) return;
        tight_loop_contents();
    }
    if (voice_cancelled) return;
    output_buffer[absolute % SAM_BUFFER_CAPACITY] = (char)sample;
    __dmb();
    if (absolute + 1u > produced_samples) produced_samples = absolute + 1u;
}

bool sam_voice_request(const char *text,
                       const sam_voice_character_t *parameters) {
    if (text == NULL || parameters == NULL || voice_state != VOICE_IDLE ||
        parameters->enabled == 0u || text[0] == '\0') return false;
    size_t length = strnlen(text, SAM_VOICE_PHRASE_LENGTH - 1u);
    memset(requested_text, 0, sizeof(requested_text));
    for (size_t index = 0u; index < length; ++index) {
        unsigned char character = (unsigned char)text[index];
        requested_text[index] = (char)(isprint(character) ?
            toupper(character) : ' ');
    }
    requested_text[length] = '[';
    requested_text[length + 1u] = '\0';
    requested_speed = parameters->speed == 0u ? 1u : parameters->speed;
    requested_pitch = parameters->pitch;
    requested_mouth = parameters->mouth;
    requested_throat = parameters->throat;
    requested_level = parameters->level;
    produced_samples = 0u;
    consumed_source_sample = 0u;
    rendered_samples = 0u;
    active_target_frames = 0u;
    stalled_target_frames = 0u;
    watchdog_produced_samples = 0u;
    watchdog_playback_position = 0u;
    watchdog_voice_state = VOICE_REQUESTED;
    voice_cancelled = false;
    playback_position_q16 = 0u;
    __dmb();
    voice_state = VOICE_REQUESTED;
    return true;
}

bool sam_voice_core1_service(void) {
    if (voice_state != VOICE_REQUESTED) return false;
    voice_state = VOICE_RENDERING;
    __dmb();
    memset(output_buffer, 128, sizeof(output_buffer));
    Init();
    SetOutputBuffer(output_buffer, (int)sizeof(output_buffer));
    SetOutputWriter(stream_sample);
    SetSpeed(requested_speed);
    SetPitch(requested_pitch);
    SetMouth(requested_mouth);
    SetThroat(requested_throat);
    int ok = TextToPhonemes((unsigned char *)requested_text);
    if (ok != 0) {
        SetInput(requested_text);
        ok = SAMMain();
    }
    uint32_t samples = ok != 0 && !voice_cancelled
        ? (uint32_t)(GetBufferLength() / 50) : 0u;
    rendered_samples = samples;
    SetOutputWriter(NULL);
    __dmb();
    voice_state = samples != 0u ? VOICE_READY : VOICE_IDLE;
    return true;
}

bool sam_voice_render_busy(void) {
    return voice_state == VOICE_REQUESTED || voice_state == VOICE_RENDERING;
}

bool sam_voice_active(void) {
    return voice_state != VOICE_IDLE;
}

int16_t sam_voice_next_sample(void) {
    const uint32_t state = voice_state;
    if (state != VOICE_IDLE) {
        ++active_target_frames;
        const uint32_t playback = playback_position_q16;
        const uint32_t produced = produced_samples;
        if (state != watchdog_voice_state ||
            playback != watchdog_playback_position ||
            produced != watchdog_produced_samples) {
            stalled_target_frames = 0u;
        } else {
            ++stalled_target_frames;
        }
        watchdog_voice_state = state;
        watchdog_playback_position = playback;
        watchdog_produced_samples = produced;
    }
    if (state != VOICE_IDLE &&
        (active_target_frames > SAM_MAX_ACTIVE_FRAMES ||
         stalled_target_frames > SAM_MAX_STALLED_FRAMES)) {
        voice_cancelled = true;
        __dmb();
        // RENDERING must be allowed to unwind from stream_sample on core 1.
        // Any other invalid/stale state is safe to release immediately.
        if (voice_state != VOICE_RENDERING && voice_state != VOICE_REQUESTED) {
            voice_state = VOICE_IDLE;
        }
        return 0;
    }
    if (voice_state == VOICE_RENDERING && produced_samples < 64u) return 0;
    if (voice_state == VOICE_READY) voice_state = VOICE_PLAYING;
    if (voice_state != VOICE_PLAYING && voice_state != VOICE_RENDERING) return 0;
    uint32_t index = playback_position_q16 >> 16u;
    if (voice_state == VOICE_RENDERING && index + 1u >= produced_samples) return 0;
    if (voice_state == VOICE_PLAYING && index >= rendered_samples) {
        voice_state = VOICE_IDLE;
        return 0;
    }
    uint32_t next = index + 1u;
    if (voice_state == VOICE_PLAYING && next >= rendered_samples) next = index;
    int32_t first = (int32_t)(uint8_t)output_buffer[index % SAM_BUFFER_CAPACITY] - 128;
    int32_t second = (int32_t)(uint8_t)output_buffer[next % SAM_BUFFER_CAPACITY] - 128;
    uint32_t fraction = playback_position_q16 & 0xffffu;
    int32_t interpolated = first +
        (((second - first) * (int32_t)fraction) >> 16u);
    playback_position_q16 += playback_step_q16;
    consumed_source_sample = playback_position_q16 >> 16u;
    return (int16_t)((interpolated * (int32_t)requested_level) << 1u);
}
