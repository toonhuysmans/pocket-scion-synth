#pragma once

#include <stdint.h>

#include "sensor.h"

#define SYNTH_SAMPLE_RATE 48000u
#define SYNTH_VOICE_COUNT 4u
#define SYNTH_RATCHET_EVENT_COUNT 12u

typedef struct {
    uint32_t frames_left;
    uint8_t note;
    uint8_t midi_channel;
    uint8_t active;
    uint8_t midi_note_off_pending;
} synth_note_t;

typedef struct {
    uint32_t due_frame;
    uint32_t duration_frames;
    uint8_t note;
    uint8_t velocity;
    uint8_t midi_channel;
    uint8_t active;
} synth_ratchet_event_t;

typedef struct synth {
    synth_note_t notes[SYNTH_VOICE_COUNT];
    synth_ratchet_event_t ratchets[SYNTH_RATCHET_EVENT_COUNT];
    uint32_t random_state;
    uint32_t dropped_triggers;
    uint32_t transport_frame;
    uint32_t next_step_frame;
    uint32_t rhythm_seed;
    uint32_t next_ratchet_frame;
    uint32_t note_on_counter;
    uint32_t ratchet_fire_counter;
    uint8_t root_note;
    uint8_t program_index;
    uint8_t bank_index;
    uint8_t pitch_bend_enabled;
    uint8_t midi_multichannel;
    uint8_t raw_mode;
    uint8_t sensitivity_index;
    uint8_t volume_index;
    uint8_t duration_index;
    uint8_t sensor_window_counter;
    uint8_t sequence_step;
    uint8_t sequence_bar;
    uint8_t euclid_steps[3];
    uint8_t euclid_pulses[3];
    uint8_t euclid_rotation[3];
    int32_t master_gain_q15;
    uint32_t raw_previous_status;
    uint32_t raw_unchanged_frames;
    int16_t visual_amp_envelope;
    int16_t visual_lfo;
    float sensor_expression;
    float sensor_proximity;
    float sensor_bend;
    float previous_mean_us;
} synth_t;

#ifdef __cplusplus
extern "C" {
#endif

void synth_init(synth_t *synth);
void synth_startup_chord(synth_t *synth);
void synth_set_sensitivity_step(synth_t *synth, int direction);
void synth_set_volume_step(synth_t *synth, int direction);
void synth_set_duration_step(synth_t *synth, int direction);
void synth_set_root_step(synth_t *synth, int direction);
void synth_next_mode(synth_t *synth);
void synth_next_bank(synth_t *synth);
void synth_toggle_pitch_bend(synth_t *synth);
void synth_toggle_midi_mode(synth_t *synth);
void synth_toggle_raw_mode(synth_t *synth);
void synth_sync_midi(const synth_t *synth);
uint8_t synth_program_id(const synth_t *synth);
float synth_sensitivity(const synth_t *synth);
void synth_sensor_window(synth_t *synth, const sensor_stats_t *stats);
void synth_render(synth_t *synth, uint32_t *stereo_frames, uint32_t frame_count);
void synth_service(synth_t *synth);

#ifdef __cplusplus
}
#endif
