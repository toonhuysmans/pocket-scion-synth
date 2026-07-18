#pragma once

#include <stdint.h>

#include "sensor.h"

#define SYNTH_SAMPLE_RATE 48000u
#define SYNTH_LANE_COUNT 3u
#define SYNTH_VOICE_COUNT 3u
#define SYNTH_RATCHET_EVENT_COUNT 12u

#define SYNTH_EDITOR_SCOPE_PATCH 0u
#define SYNTH_EDITOR_SCOPE_BANK 1u
#define SYNTH_EDITOR_SCOPE_GLOBAL 2u
#define SYNTH_EDITOR_SCOPE_SENSOR 3u
#define SYNTH_EDITOR_SCENE_PARAMETER_COUNT 47u
#define SYNTH_EDITOR_PATCH_SHARED_COUNT 101u
#define SYNTH_EDITOR_BANK_PARAMETER_COUNT 19u
#define SYNTH_EDITOR_GLOBAL_PARAMETER_COUNT 7u

typedef struct {
    uint32_t frames_left;
    uint8_t note;
    uint8_t midi_channel;
    uint8_t lane;
    uint8_t active;
    uint8_t midi_note_off_pending;
} synth_note_t;

typedef struct {
    uint32_t due_frame;
    uint32_t duration_frames;
    uint8_t note;
    uint8_t velocity;
    uint8_t midi_channel;
    uint8_t lane;
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
    uint32_t chord_held_notes[4];
    uint32_t chord_capture_deadline;
    uint16_t chord_pitch_classes;
    uint16_t chord_pending_pitch_classes;
    uint8_t chord_capture_pending;
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
    uint8_t sensor_stats_valid;
    uint8_t euclid_steps[3];
    uint8_t euclid_pulses[3];
    uint8_t euclid_rotation[3];
    int32_t master_gain_q15;
    uint32_t raw_previous_status;
    uint32_t raw_unchanged_frames;
    uint32_t sensor_next_hold_frame;
    int16_t visual_amp_envelope;
    int16_t visual_lfo;
    float sensor_expression;
    float sensor_proximity;
    float sensor_transient;
    float sensor_bend;
    float previous_mean_us;
    float adaptive_mean_low_us;
    float adaptive_mean_high_us;
    float adaptive_variation_low;
    float adaptive_variation_high;
    sensor_stats_t last_sensor_stats;
} synth_t;

#ifdef __cplusplus
extern "C" {
#endif

void synth_init(synth_t *synth);
void synth_startup_chord(synth_t *synth);
void synth_midi_chord_note(synth_t *synth, uint8_t note, bool pressed);
void synth_midi_chord_release(synth_t *synth);
void synth_midi_chord_clear(synth_t *synth);
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
void synth_sensor_tick(synth_t *synth, bool input_active);
void synth_render(synth_t *synth, uint32_t *stereo_frames, uint32_t frame_count);
void synth_service(synth_t *synth);
bool synth_editor_select(synth_t *synth, uint8_t patch_id);
bool synth_editor_get(const synth_t *synth, uint8_t scope, uint8_t target,
                      uint8_t lane, uint8_t parameter, uint16_t *value);
bool synth_editor_set(synth_t *synth, uint8_t scope, uint8_t target,
                      uint8_t lane, uint8_t parameter, uint16_t value);
bool synth_editor_commit(const synth_t *synth, uint8_t scope, uint8_t target);
bool synth_editor_revert(synth_t *synth, uint8_t scope, uint8_t target);
bool synth_editor_restore(synth_t *synth, uint8_t scope, uint8_t target);

#ifdef __cplusplus
}
#endif
