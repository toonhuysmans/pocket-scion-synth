#include "synth.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "midi_uart.h"
#include "preset_store.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/platform.h"
#include "raw_capture.h"
#include "sam_voice.h"
#include "status_rgb.h"

// PRA32-U uses Arduino's boolean alias in its otherwise portable DSP headers.
using boolean = bool;
uint8_t g_midi_ch = 0;
// Keep PRA32-U's four-voice modes, but render each engine wholly on the core
// assigned by this firmware. Enabling PRA32-U's internal core split here
// deadlocks bass/pad because core 1 is already dedicated to the complete lead
// engine rather than to each instance's secondary_core_process() handshake.
#define PRA32_U_EMULATION
#include "pra32-u-synth.h"

namespace {

// Three role-specific monophonic timbres. The pad owns chorus/delay; the dry
// bass/percussion and lead roles feed that stage.
PRA32_U_Synth<true> bass_engine;
PRA32_U_Synth<false> middle_engine;
PRA32_U_Synth<true> upper_engine;

constexpr uint8_t bass_lane = 0u;
constexpr uint8_t middle_lane = 1u;
constexpr uint8_t upper_lane = 2u;

volatile uint32_t upper_render_request = 0u;
volatile int16_t upper_render_result = 0;
volatile uint8_t upper_render_disabled = 0u;

constexpr float sensitivity_values[] = {
    2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f, 6.5f, 8.0f
};

constexpr int32_t volume_gain_q15[] = {
    0, 16384, 26216, 32768, 45876, 58984,
    72088, 111412, 114688, 117964, 124520, 131072
};

constexpr float duration_multipliers[] = {
    0.25f, 0.40f, 0.65f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f
};

uint8_t editor_scene_bpm(uint8_t mode);
uint8_t editor_bank_tempo(uint8_t bank);
uint8_t editor_euclid_length(uint8_t mode, uint8_t lane);
uint8_t editor_motif_degree(uint8_t mode, uint8_t step);
int8_t editor_scale_offset(uint8_t mode, uint8_t degree);
uint8_t editor_swing_percent(uint8_t mode);

constexpr int8_t scales[][7] = {
    {0, 2, 3, 7, 10, 12, 14},
    {0, 2, 4, 7, 9, 12, 16},
    {0, 1, 5, 7, 8, 12, 13},
    {0, 3, 5, 6, 10, 12, 15},
    {0, 2, 4, 7, 9, 11, 14},
    {0, 2, 4, 6, 8, 10, 12},
    {0, 2, 3, 5, 7, 8, 11},
    {0, 2, 4, 6, 7, 9, 11},
    {0, 1, 2, 3, 6, 7, 10},
    {0, 1, 3, 4, 6, 7, 9},
    {0, 2, 4, 5, 7, 9, 11},
    {0, 3, 5, 7, 10, 12, 15},
    {0, 2, 5, 7, 9, 12, 14},
    {0, 1, 3, 6, 7, 10, 12},
    {0, 2, 4, 7, 9, 11, 12},
    {0, 1, 2, 5, 6, 9, 11},
};

// Each scene has a distinct tempo and melodic contour. Three sensor-controlled
// Euclidean lanes create bass, melody and upper-voice rhythms.
constexpr uint8_t scene_bpm[] = {
    92, 106, 84, 118, 72, 124, 78, 68, 132, 148, 104, 96, 88, 120, 82, 156
};
constexpr uint8_t bank_tempo_percent[] = {
    100, 100, 90, 125, 108, 70, 112, 135,
    100, 100, 100, 100, 100, 100, 100, 100,
};
constexpr uint8_t euclid_lengths[][3] = {
    {16, 12,  7},
    {16, 15,  9},
    {16, 14, 11},
    {16, 11, 13},
    {16, 13,  8},
    {16,  9,  7},
    {16, 15, 11},
    {16, 13, 15},
    {16,  7,  5},
    {16, 11,  9},
    {16, 13, 10},
    {16, 12, 16},
    {16, 15,  8},
    {16,  8,  6},
    {16, 14,  9},
    {16,  7, 13},
};
constexpr uint8_t melodic_motifs[][16] = {
    {0, 2, 1, 4, 0, 3, 2, 5, 1, 4, 3, 6, 2, 5, 1, 3},
    {0, 1, 3, 2, 4, 2, 5, 3, 1, 3, 6, 4, 2, 5, 3, 1},
    {0, 3, 1, 2, 0, 4, 2, 1, 3, 5, 2, 4, 1, 3, 6, 2},
    {0, 2, 4, 1, 3, 5, 2, 6, 1, 4, 2, 5, 3, 6, 4, 2},
    {0, 4, 2, 5, 1, 3, 6, 2, 4, 1, 5, 3, 2, 6, 1, 4},
    {0, 3, 6, 2, 5, 1, 4, 0, 2, 5, 1, 6, 3, 0, 4, 2},
    {0, 2, 1, 5, 3, 2, 6, 4, 1, 3, 0, 5, 2, 4, 6, 1},
    {0, 4, 1, 6, 2, 5, 3, 1, 5, 2, 6, 3, 4, 0, 2, 5},
    {0, 1, 4, 2, 5, 3, 6, 1, 3, 5, 2, 4, 6, 0, 5, 2},
    {0, 6, 2, 5, 1, 4, 3, 6, 2, 0, 5, 3, 1, 6, 4, 2},
    {0, 2, 4, 3, 1, 5, 2, 6, 3, 1, 4, 2, 5, 0, 3, 1},
    {0, 3, 0, 4, 1, 5, 2, 6, 0, 4, 2, 5, 1, 6, 3, 0},
    {0, 5, 2, 6, 3, 1, 4, 2, 5, 0, 3, 6, 1, 4, 2, 5},
    {0, 1, 6, 2, 5, 3, 4, 0, 6, 1, 5, 2, 4, 3, 6, 1},
    {0, 2, 3, 5, 1, 4, 6, 2, 0, 5, 3, 1, 6, 4, 2, 3},
    {0, 6, 1, 5, 2, 4, 3, 0, 5, 1, 6, 2, 4, 0, 3, 6},
};

uint32_t rhythmic_step_frames(uint8_t mode, uint8_t step) {
    const uint8_t bank = mode / 16u;
    uint32_t bpm = (static_cast<uint32_t>(editor_scene_bpm(mode)) *
                    editor_bank_tempo(bank)) / 100u;
    const uint32_t straight = (SYNTH_SAMPLE_RATE * 15u) / bpm;
    const uint32_t swing = editor_swing_percent(mode);
    // The pair remains exactly two straight sixteenth notes long.
    return (step & 1u) ? (straight * (200u - swing * 2u)) / 100u
                       : (straight * swing * 2u) / 100u;
}

void advance_sequence(synth_t *synth) {
    synth->sequence_step = static_cast<uint8_t>((synth->sequence_step + 1u) & 15u);
    if (synth->sequence_step != 0u) return;
    synth->sequence_bar = static_cast<uint8_t>((synth->sequence_bar + 1u) & 3u);
    synth->rhythm_seed ^= synth->rhythm_seed << 13;
    synth->rhythm_seed ^= synth->rhythm_seed >> 17;
    synth->rhythm_seed ^= synth->rhythm_seed << 5;
}

bool euclidean_hit(uint8_t tick, uint8_t pulses, uint8_t steps,
                   uint8_t rotation) {
    if (pulses == 0u || steps == 0u) return false;
    const uint8_t position = static_cast<uint8_t>((tick + rotation) % steps);
    return static_cast<uint8_t>((position * pulses) % steps) < pulses;
}

struct scene_t {
    uint8_t osc1_wave, osc1_shape, osc1_morph, sub_mix;
    uint8_t osc2_wave, osc2_coarse, osc2_pitch, osc_mix;
    uint8_t cutoff, resonance, filter_eg, filter_key_track;
    uint8_t eg_attack, eg_decay, eg_sustain, eg_release;
    uint8_t eg_osc_amt, eg_osc_dst, voice_mode, portamento;
    uint8_t lfo_wave, lfo_rate, lfo_depth, lfo_fade;
    uint8_t lfo_osc_amt, lfo_osc_dst, lfo_filter_amt, amp_gain;
    uint8_t amp_attack, amp_decay, amp_sustain, amp_release;
    uint8_t filter_mode, eg_amp_mod, release_equals_decay, pitch_bend_range;
    uint8_t breath_filter_amt, breath_amp_mod, eg_velocity, amp_velocity;
    uint8_t voice_assignment;
    uint8_t chorus_mix, chorus_rate, chorus_depth;
    uint8_t delay_feedback, delay_time, delay_mode;
};

static_assert(sizeof(scene_t) == 47u, "Scene parameters must remain packed");

enum : uint8_t {
    LOW_MODE_INHERIT = 0u,
    LOW_MODE_BASS = 1u,
    LOW_MODE_PERCUSSION = 2u,
    LOW_MODE_HYBRID = 3u,
};

enum : uint8_t {
    PERC_KICK = 0u,
    PERC_TOM = 1u,
    PERC_SNARE = 2u,
    PERC_CLOSED_HAT = 3u,
    PERC_OPEN_HAT = 4u,
    PERC_CLAP = 5u,
    PERC_RIM_WOOD = 6u,
    PERC_SHAKER_METAL = 7u,
};

enum : uint8_t {
    PERC_ROLE_ANCHOR = 0u,
    PERC_ROLE_BACKBEAT = 1u,
    PERC_ROLE_OFFBEAT = 2u,
    PERC_ROLE_FILL = 3u,
    PERC_ROLE_FREE = 4u,
};

struct articulation_slot_t {
    // Algorithm occupies bits 0..2 and rhythmic role bits 3..5. Packing these
    // leaves room for six expressive slots in one 236-byte flash payload.
    uint8_t algorithm_role;
    uint8_t weight;
    uint8_t level;
    uint8_t tune;
    uint8_t tone;
    uint8_t noise;
    uint8_t decay;
    uint8_t transient;
    uint8_t ratchet;
};

static_assert(sizeof(articulation_slot_t) == 9u,
              "Articulation slots must remain packed");

struct patch_record_t {
    scene_t lane[3];
    int8_t scale[7];
    uint8_t motif[16];
    uint8_t bpm;
    uint8_t euclid_length[3];
    uint8_t swing_percent;
    uint8_t gate_q5[3];
    int8_t density_bias;
    uint8_t ratchet_percent[3];
    uint8_t low_mode;
    uint8_t low_balance;
    uint8_t low_sensor;
    uint8_t low_variation;
    articulation_slot_t articulation[6];
    int8_t density_bias_pad;
    int8_t density_bias_lead;
};

// Bass and lead bypass the shared effects engine, so their twelve legacy FX
// bytes are patch-owned storage. Schema 4 gives those bytes an explicit,
// editable purpose without growing the flash payload or invalidating older
// prefix-compatible records. Pad FX remain the audible shared effects.
constexpr uint8_t patch_behavior_marker = 0xa5u;

enum : uint8_t {
    PATCH_BREATH_OVERRIDE = 101u,
    PATCH_BEND_PERCENT = 102u,
    PATCH_RATCHET_PERCENT = 103u,
    PATCH_AMP_DECAY_MOTION = 104u,
    PATCH_AMP_SUSTAIN_MOTION = 105u,
    PATCH_AMP_RELEASE_MOTION = 106u,
    PATCH_PROXIMITY_OCTAVES = 107u,
    PATCH_EXPRESSION_OCTAVE_THRESHOLD = 108u,
    PATCH_CUTOFF_PERCENT = 109u,
    PATCH_RESONANCE_PERCENT = 110u,
    PATCH_MORPH_PERCENT = 111u,
    PATCH_LFO_RATE_PERCENT = 112u,
};

struct bank_record_t {
    uint8_t tempo_percent;
    uint8_t breath_max;
    uint8_t modulation_max;
    uint8_t cutoff_range;
    uint8_t resonance_range;
    uint8_t morph_range;
    uint8_t lfo_rate_range;
    uint8_t bend_percent;
    int8_t density_offset;
    uint8_t ratchet_percent;
    uint8_t gate_percent;
    uint8_t lane_motion_percent[3];
    uint8_t led_red;
    uint8_t led_green;
    uint8_t led_blue;
    uint8_t low_mode;
    uint8_t low_balance;
};

struct global_record_t {
    uint8_t root_note;
    uint8_t sensitivity_index;
    uint8_t volume_index;
    uint8_t duration_index;
    uint8_t pitch_bend_enabled;
    uint8_t midi_multichannel;
    uint8_t led_brightness;
    uint8_t reserved;
    uint16_t sensor_minimum_interval_us;
    uint16_t sensor_activity_timeout_ms;
    uint8_t sensor_window_size;
    uint8_t sensor_adaptive_percent;
    uint8_t sensor_pressure_smoothing;
    uint8_t sensor_expression_smoothing;
    uint8_t sensor_variation_gain_tenths;
    uint8_t sensor_transient_gain_percent;
    uint8_t sensor_transient_decay_percent;
    uint8_t sensor_calibration_learning;
    uint8_t sensor_calibration_recovery_tenths_percent;
};

static_assert(sizeof(patch_record_t) == 236u,
              "Patch record must fill but not exceed one flash payload");
static_assert(sizeof(global_record_t) == 22u,
              "Global record layout must preserve its eight-byte prefix");

patch_record_t active_patch;
bank_record_t active_bank;
sam_voice_patch_t active_speech;
uint8_t active_patch_id = 0xffu;
uint8_t active_bank_id = 0xffu;
bool active_patch_dirty = false;
bool active_bank_dirty = false;
bool globals_dirty = false;
uint8_t editor_led_brightness = 127u;

struct speech_base_record_t {
    uint8_t enabled, level, speed, pitch, mouth, throat, density;
    uint8_t sensor_influence;
    char phrase[4][SAM_VOICE_PHRASE_LENGTH];
    uint8_t motion_chance, motion_amount;
};

struct speech_extension_record_t {
    char phrase[3][SAM_VOICE_PHRASE_LENGTH];
};

static_assert(sizeof(speech_base_record_t) <= 236u,
              "Base speech record must fit one flash payload");
static_assert(sizeof(speech_extension_record_t) <= 236u,
              "Speech extension must fit one flash payload");

void build_default_speech(uint8_t id, sam_voice_patch_t *record) {
    static constexpr const char *phrases[SAM_VOICE_PHRASE_COUNT] = {
        "Happy Birthday to you",
        "Happy Birthday dear friend",
        "Dance, Dance",
        "How old are you now?",
        "one thousand years!",
        "Metronome",
        "Happy, Happy",
        "Music makes me dance",
        "Acid!",
        "Pants on?! Pants off!?",
    };
    const uint8_t bank = static_cast<uint8_t>(id / 16u);
    const uint8_t program = static_cast<uint8_t>(id % 16u);
    std::memset(record, 0, sizeof(*record));
    record->enabled = 1u;
    record->level = 25u;
    record->speed = static_cast<uint8_t>(62u + (program % 7u) * 4u);
    record->pitch = static_cast<uint8_t>(48u + ((bank * 11u + program * 5u) % 46u));
    record->mouth = static_cast<uint8_t>(96u + ((bank * 13u + program * 3u) % 80u));
    record->throat = static_cast<uint8_t>(92u + ((bank * 7u + program * 9u) % 88u));
    record->density = static_cast<uint8_t>(20u + (program % 5u) * 9u);
    record->sensor_influence = static_cast<uint8_t>(64u + (bank % 4u) * 16u);
    record->motion_chance = 34u;
    record->motion_amount = 22u;
    for (uint8_t phrase = 0u; phrase < SAM_VOICE_PHRASE_COUNT; ++phrase) {
        std::strncpy(record->phrase[phrase], phrases[phrase],
                     SAM_VOICE_PHRASE_LENGTH - 1u);
    }
}

bool load_speech_override(uint8_t id, sam_voice_patch_t *record) {
    speech_base_record_t base = {
        record->enabled, record->level, record->speed, record->pitch,
        record->mouth, record->throat, record->density,
        record->sensor_influence, {}, record->motion_chance,
        record->motion_amount,
    };
    for (uint8_t phrase = 0u; phrase < 4u; ++phrase) {
        std::memcpy(base.phrase[phrase], record->phrase[phrase],
                    SAM_VOICE_PHRASE_LENGTH);
    }
    bool loaded = preset_store_load_prefix(preset_store_speech_patch_key(id),
                                           &base, sizeof(base), nullptr);
    record->enabled = base.enabled; record->level = base.level;
    record->speed = base.speed; record->pitch = base.pitch;
    record->mouth = base.mouth; record->throat = base.throat;
    record->density = base.density;
    record->sensor_influence = base.sensor_influence;
    record->motion_chance = base.motion_chance;
    record->motion_amount = base.motion_amount;
    for (uint8_t phrase = 0u; phrase < 4u; ++phrase) {
        std::memcpy(record->phrase[phrase], base.phrase[phrase],
                    SAM_VOICE_PHRASE_LENGTH);
    }
    for (uint8_t extension = 0u; extension < 2u; ++extension) {
        speech_extension_record_t extra = {};
        const uint8_t first = static_cast<uint8_t>(4u + extension * 3u);
        for (uint8_t index = 0u; index < 3u; ++index) {
            std::memcpy(extra.phrase[index], record->phrase[first + index],
                        SAM_VOICE_PHRASE_LENGTH);
        }
        const bool extension_loaded = preset_store_load_prefix(
            preset_store_speech_extension_key(id, extension), &extra,
            sizeof(extra), nullptr);
        loaded = loaded || extension_loaded;
        for (uint8_t index = 0u; index < 3u; ++index) {
            std::memcpy(record->phrase[first + index], extra.phrase[index],
                        SAM_VOICE_PHRASE_LENGTH);
        }
    }
    return loaded;
}

bool save_speech_override(uint8_t id, const sam_voice_patch_t &record) {
    speech_base_record_t base = {
        record.enabled, record.level, record.speed, record.pitch,
        record.mouth, record.throat, record.density, record.sensor_influence,
        {}, record.motion_chance, record.motion_amount,
    };
    for (uint8_t phrase = 0u; phrase < 4u; ++phrase) {
        std::memcpy(base.phrase[phrase], record.phrase[phrase],
                    SAM_VOICE_PHRASE_LENGTH);
    }
    bool ok = preset_store_save(preset_store_speech_patch_key(id), &base,
                                sizeof(base));
    for (uint8_t extension = 0u; extension < 2u && ok; ++extension) {
        speech_extension_record_t extra = {};
        const uint8_t first = static_cast<uint8_t>(4u + extension * 3u);
        for (uint8_t index = 0u; index < 3u; ++index) {
            std::memcpy(extra.phrase[index], record.phrase[first + index],
                        SAM_VOICE_PHRASE_LENGTH);
        }
        ok = preset_store_save(
            preset_store_speech_extension_key(id, extension), &extra,
            sizeof(extra));
    }
    return ok;
}

global_record_t default_global_record() {
    return {
        45u, 4u, 7u, 3u, 0u, 0u, 127u, 0u,
        2500u, 1500u, 10u, 78u, 18u, 22u, 80u, 100u, 24u, 1u, 10u,
    };
}

void reset_sensor_calibration(synth_t *synth) {
    synth->previous_mean_us = 0.0f;
    synth->adaptive_mean_low_us = 0.0f;
    synth->adaptive_mean_high_us = 0.0f;
    synth->adaptive_variation_low = 0.0f;
    synth->adaptive_variation_high = 0.0f;
}

void apply_global_record(synth_t *synth, const global_record_t &globals) {
    synth->root_note = static_cast<uint8_t>(
        globals.root_note < 24u ? 24u :
        globals.root_note > 72u ? 72u : globals.root_note);
    synth->sensitivity_index = static_cast<uint8_t>(
        globals.sensitivity_index > 7u ? 7u : globals.sensitivity_index);
    synth->volume_index = static_cast<uint8_t>(
        globals.volume_index > 11u ? 11u : globals.volume_index);
    synth->duration_index = static_cast<uint8_t>(
        globals.duration_index > 7u ? 7u : globals.duration_index);
    synth->pitch_bend_enabled = globals.pitch_bend_enabled != 0u;
    synth->midi_multichannel = globals.midi_multichannel != 0u;
    editor_led_brightness = globals.led_brightness;

    synth->sensor_window_size = static_cast<uint8_t>(
        globals.sensor_window_size < 4u ? 4u :
        globals.sensor_window_size > 24u ? 24u : globals.sensor_window_size);
    synth->sensor_minimum_interval_us = static_cast<uint16_t>(
        globals.sensor_minimum_interval_us < 500u ? 500u :
        globals.sensor_minimum_interval_us > 10000u ? 10000u :
        globals.sensor_minimum_interval_us);
    synth->sensor_activity_timeout_ms = static_cast<uint16_t>(
        globals.sensor_activity_timeout_ms < 100u ? 100u :
        globals.sensor_activity_timeout_ms > 10000u ? 10000u :
        globals.sensor_activity_timeout_ms);
    synth->sensor_adaptive_percent = static_cast<uint8_t>(
        globals.sensor_adaptive_percent > 100u ? 100u :
        globals.sensor_adaptive_percent);
    synth->sensor_pressure_smoothing = static_cast<uint8_t>(
        globals.sensor_pressure_smoothing < 1u ? 1u :
        globals.sensor_pressure_smoothing > 100u ? 100u :
        globals.sensor_pressure_smoothing);
    synth->sensor_expression_smoothing = static_cast<uint8_t>(
        globals.sensor_expression_smoothing < 1u ? 1u :
        globals.sensor_expression_smoothing > 100u ? 100u :
        globals.sensor_expression_smoothing);
    synth->sensor_variation_gain_tenths = static_cast<uint8_t>(
        globals.sensor_variation_gain_tenths < 1u ? 1u :
        globals.sensor_variation_gain_tenths > 200u ? 200u :
        globals.sensor_variation_gain_tenths);
    synth->sensor_transient_gain_percent = static_cast<uint8_t>(
        globals.sensor_transient_gain_percent > 200u ? 200u :
        globals.sensor_transient_gain_percent);
    synth->sensor_transient_decay_percent = static_cast<uint8_t>(
        globals.sensor_transient_decay_percent < 1u ? 1u :
        globals.sensor_transient_decay_percent > 100u ? 100u :
        globals.sensor_transient_decay_percent);
    synth->sensor_calibration_learning =
        globals.sensor_calibration_learning != 0u;
    synth->sensor_calibration_recovery_tenths_percent = static_cast<uint8_t>(
        globals.sensor_calibration_recovery_tenths_percent < 1u ? 1u :
        globals.sensor_calibration_recovery_tenths_percent > 50u ? 50u :
        globals.sensor_calibration_recovery_tenths_percent);
    sensor_configure(synth->sensor_window_size,
                     synth->sensor_minimum_interval_us,
                     synth->sensor_activity_timeout_ms);
}

global_record_t capture_global_record(const synth_t *synth) {
    return {
        synth->root_note, synth->sensitivity_index, synth->volume_index,
        synth->duration_index, synth->pitch_bend_enabled,
        synth->midi_multichannel, editor_led_brightness, 0u,
        synth->sensor_minimum_interval_us, synth->sensor_activity_timeout_ms,
        synth->sensor_window_size, synth->sensor_adaptive_percent,
        synth->sensor_pressure_smoothing, synth->sensor_expression_smoothing,
        synth->sensor_variation_gain_tenths,
        synth->sensor_transient_gain_percent,
        synth->sensor_transient_decay_percent,
        synth->sensor_calibration_learning,
        synth->sensor_calibration_recovery_tenths_percent,
    };
}

struct sensor_route_t;
scene_t make_generated_lane_scene(uint8_t id, uint8_t lane);
sensor_route_t generated_sensor_route(uint8_t bank);
void load_active_patch(uint8_t id);
void load_active_bank(uint8_t bank);

constexpr uint8_t scene_count = 16u;
constexpr uint8_t bank_count = PRESET_STORE_BANK_COUNT;
static_assert(sizeof(bank_tempo_percent) / sizeof(bank_tempo_percent[0]) ==
              bank_count, "Every bank needs a tempo profile");
static_assert(sizeof(scales) / sizeof(scales[0]) == scene_count);
static_assert(sizeof(scene_bpm) / sizeof(scene_bpm[0]) == scene_count);
static_assert(sizeof(euclid_lengths) / sizeof(euclid_lengths[0]) == scene_count);
static_assert(sizeof(melodic_motifs) / sizeof(melodic_motifs[0]) == scene_count);

uint32_t random_u32(synth_t *synth) {
    uint32_t x = synth->random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    synth->random_state = x;
    return x;
}

float clampf(float value, float low, float high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

uint8_t clamp_u7(int value) {
    if (value < 0) return 0;
    if (value > 127) return 127;
    return static_cast<uint8_t>(value);
}

unsigned pitch_class_count(uint16_t pitch_classes) {
    unsigned count = 0u;
    while (pitch_classes != 0u) {
        count += pitch_classes & 1u;
        pitch_classes = static_cast<uint16_t>(pitch_classes >> 1u);
    }
    return count;
}

int constrain_note_to_chord(int note, uint16_t pitch_classes) {
    if (pitch_classes == 0u) return note;
    for (int distance = 0; distance <= 6; ++distance) {
        const int lower = note - distance;
        if (lower >= 0 &&
            (pitch_classes & (1u << (static_cast<unsigned>(lower) % 12u))) != 0u) {
            return lower;
        }
        const int upper = note + distance;
        if (distance != 0 && upper <= 127 &&
            (pitch_classes & (1u << (static_cast<unsigned>(upper) % 12u))) != 0u) {
            return upper;
        }
    }
    return note;
}

scene_t make_foundation_scene(uint8_t index) {
    scene_t s = {
        75, 64, 64, 64,  25, 64, 72, 42,
        76, 18, 70, 64,  8, 62, 45, 42,
        64, 0, 0, 0,  LFO_WAVE_SINE, 45, 10, 20,
        70, 0, 68, 86,  0, 54, 104, 42,
        0, 127, 0, 12,  72, 24, 72, 82,
        0,  58, 54, 58,  42, 58, 0,
    };

    switch (index % scene_count) {
        case 0:  // Rooted poly: stable reference voice.
            break;
        case 1:  // Bright bloom: animated major shimmer.
            s.osc1_wave = 25; s.osc2_wave = 75; s.osc2_pitch = 78;
            s.cutoff = 98; s.filter_eg = 82; s.eg_attack = 28;
            s.amp_attack = 12; s.amp_release = 68;
            s.chorus_mix = 82; s.chorus_depth = 76; s.delay_feedback = 52;
            break;
        case 2:  // Subterranean pulse: weight and short rhythmic envelopes.
            s.osc1_wave = 0; s.sub_mix = 112; s.osc_mix = 28;
            s.osc2_coarse = 52; s.cutoff = 50; s.resonance = 38;
            s.eg_decay = 44; s.eg_sustain = 18; s.amp_decay = 40;
            s.amp_sustain = 72; s.amp_release = 26; s.amp_gain = 104;
            s.breath_filter_amt = 86;
            break;
        case 3:  // Organic S&H: living filter and shape movement.
            s.osc1_shape = 96; s.osc1_morph = 80; s.osc2_wave = 100;
            s.osc_mix = 38; s.lfo_wave = LFO_WAVE_S_AND_H;
            s.lfo_rate = 38; s.lfo_depth = 28; s.lfo_fade = 36;
            s.lfo_osc_dst = 127; s.lfo_filter_amt = 90;
            s.chorus_mix = 76; s.delay_mode = 64;
            break;
        case 4:  // Glass pluck: pitch transient, high register and long sparkle.
            s.osc1_wave = 100; s.osc1_shape = 110; s.osc2_pitch = 84;
            s.cutoff = 112; s.resonance = 12; s.filter_eg = 48;
            s.eg_decay = 28; s.eg_sustain = 0; s.eg_osc_amt = 86;
            s.eg_osc_dst = 64; s.amp_decay = 55; s.amp_sustain = 60;
            s.amp_release = 85; s.chorus_mix = 92; s.delay_feedback = 62;
            break;
        case 5:  // Whole-tone drift: detuned, slow and wide.
            s.osc1_wave = 75; s.osc2_wave = 75; s.osc2_pitch = 86;
            s.osc_mix = 64; s.amp_attack = 18; s.amp_release = 86;
            s.lfo_wave = LFO_WAVE_TRIANGLE; s.lfo_rate = 30;
            s.lfo_depth = 24; s.lfo_fade = 70; s.lfo_osc_dst = 64;
            s.chorus_mix = 104; s.chorus_depth = 102; s.delay_mode = 64;
            break;
        case 6:  // Shadow pad: dark harmonic-minor swell.
            s.sub_mix = 88; s.osc2_coarse = 58; s.cutoff = 58;
            s.resonance = 54; s.eg_attack = 60; s.eg_release = 98;
            s.amp_attack = 52; s.amp_release = 104;
            s.release_equals_decay = 127; s.breath_amp_mod = 52;
            s.chorus_mix = 86; s.delay_feedback = 78; s.delay_time = 72;
            break;
        case 7:  // Deep space: slow paraphonic spectral cloud.
            s.voice_mode = 1; s.osc1_shape = 84; s.osc2_pitch = 80;
            s.filter_key_track = 32; s.amp_attack = 44; s.amp_release = 116;
            s.lfo_rate = 22; s.lfo_depth = 34; s.lfo_fade = 84;
            s.chorus_mix = 120; s.chorus_rate = 30; s.chorus_depth = 116;
            s.delay_feedback = 96; s.delay_time = 84; s.delay_mode = 64;
            break;
        case 8:  // Acid teeth: resonant low-pass bite.
            s.osc1_wave = 0; s.osc1_shape = 118; s.osc2_wave = 127;
            s.osc_mix = 50; s.cutoff = 38; s.resonance = 118;
            s.filter_eg = 120; s.eg_decay = 38; s.eg_sustain = 0;
            s.amp_decay = 60; s.amp_sustain = 80; s.amp_release = 48;
            s.breath_filter_amt = 112; s.amp_velocity = 104;
            break;
        case 9:  // Chaos garden: fast random routing and controlled noise.
            s.osc1_wave = 100; s.osc1_shape = 122; s.osc2_wave = 100;
            s.osc_mix = 24; s.cutoff = 66; s.resonance = 84;
            s.filter_mode = 64; s.lfo_wave = LFO_WAVE_S_AND_H;
            s.lfo_rate = 108; s.lfo_depth = 62; s.lfo_osc_dst = 127;
            s.lfo_filter_amt = 108; s.chorus_mix = 100;
            s.delay_feedback = 106; s.delay_mode = 64;
            break;
        case 10:  // Mono glide lead: legato portamento and delayed vibrato.
            s.voice_mode = 4; s.portamento = 78; s.osc1_wave = 127;
            s.osc1_shape = 74; s.osc2_pitch = 82; s.cutoff = 88;
            s.resonance = 52; s.lfo_rate = 62; s.lfo_depth = 26;
            s.lfo_fade = 88; s.lfo_osc_amt = 92; s.pitch_bend_range = 24;
            s.delay_feedback = 54; s.delay_mode = 64;
            break;
        case 11:  // Paraphonic organ: shared envelope, stable harmonics.
            s.voice_mode = 1; s.osc1_wave = 25; s.osc2_wave = 127;
            s.osc2_coarse = 76; s.osc_mix = 60; s.cutoff = 102;
            s.filter_key_track = 96; s.eg_amp_mod = 0;
            s.amp_attack = 4; s.amp_sustain = 127; s.amp_release = 28;
            s.chorus_mix = 98; s.chorus_rate = 42; s.delay_feedback = 28;
            break;
        case 12:  // High-pass air: breath-controlled, thin and floating.
            s.osc1_wave = 75; s.osc2_wave = 25; s.filter_mode = 64;
            s.cutoff = 48; s.resonance = 32; s.filter_eg = 46;
            s.breath_filter_amt = 116; s.breath_amp_mod = 72;
            s.amp_attack = 28; s.amp_release = 76;
            s.chorus_mix = 108; s.delay_feedback = 64;
            break;
        case 13:  // Pitch-envelope percussion: tuned impact and ratchets.
            s.osc1_wave = 0; s.osc2_wave = 75; s.osc2_coarse = 48;
            s.cutoff = 74; s.resonance = 70; s.eg_attack = 0;
            s.eg_decay = 24; s.eg_sustain = 0; s.eg_release = 16;
            s.eg_osc_amt = 112; s.eg_osc_dst = 127;
            s.amp_decay = 48; s.amp_sustain = 35; s.amp_release = 52;
            s.eg_amp_mod = 127; s.release_equals_decay = 127;
            s.chorus_mix = 28; s.delay_feedback = 46;
            break;
        case 14:  // Breath reed: sensor becomes expression controller.
            s.osc1_wave = 127; s.osc1_shape = 92; s.osc1_morph = 78;
            s.osc2_wave = 75; s.osc_mix = 34; s.cutoff = 70;
            s.resonance = 44; s.breath_filter_amt = 124;
            s.breath_amp_mod = 112; s.eg_velocity = 30; s.amp_velocity = 36;
            s.amp_attack = 22; s.amp_release = 58; s.voice_assignment = 64;
            s.chorus_mix = 70; s.delay_feedback = 38;
            break;
        default:  // Maximum mutation: deliberately extreme full routing.
            s.osc1_wave = 127; s.osc1_shape = 127; s.osc1_morph = 110;
            s.sub_mix = 104; s.osc2_wave = 100; s.osc2_coarse = 84;
            s.osc2_pitch = 96; s.osc_mix = 68; s.cutoff = 54;
            s.resonance = 112; s.filter_eg = 118; s.filter_key_track = 96;
            s.eg_attack = 2; s.eg_decay = 48; s.eg_sustain = 20;
            s.eg_osc_amt = 118; s.eg_osc_dst = 127;
            s.lfo_wave = LFO_WAVE_S_AND_H; s.lfo_rate = 118;
            s.lfo_depth = 94; s.lfo_fade = 8; s.lfo_osc_amt = 116;
            s.lfo_osc_dst = 127; s.lfo_filter_amt = 116;
            s.filter_mode = 64; s.breath_filter_amt = 120;
            s.breath_amp_mod = 92; s.voice_assignment = 64;
            s.chorus_mix = 120; s.chorus_depth = 120;
            s.delay_feedback = 112; s.delay_time = 48; s.delay_mode = 64;
            s.pitch_bend_range = 36;
            break;
    }
    return s;
}

uint8_t adjust_u7(uint8_t value, int amount) {
    return clamp_u7(static_cast<int>(value) + amount);
}

scene_t make_legacy_scene(uint8_t program) {
    if (program >= 10u) return make_foundation_scene(program);

    struct legacy_values_t {
        uint8_t osc1_wave, osc2_wave, osc_mix, sub_mix, osc2_pitch;
        uint8_t cutoff, resonance, filter_eg, amp_attack, amp_release;
        uint8_t lfo_wave, lfo_rate, lfo_depth;
        uint8_t chorus_mix, chorus_depth, delay_feedback, delay_time;
    };
    static constexpr legacy_values_t legacy[10] = {
        {75,25,42, 64,72, 76, 18, 70,0,42,   0, 45,10, 58, 58, 42,58},
        {25,75,56, 64,74, 88, 12, 62,0,64,   0, 52,12, 72, 70, 58,55},
        { 0,75,45,100,69, 54, 28, 80,0,38,   0, 36, 8, 38, 42, 40,66},
        {75,100,62,72,72, 58, 48, 96,2,80, 100, 40,18, 78, 82, 66,50},
        {100,25,50,64,79,108,  8, 44,0,70,   0, 62,14, 88, 76, 48,60},
        {75,75,64, 64,82, 74, 22, 54,3,56,   0, 48,22, 92, 94, 58,52},
        {25,100,48,84,67,62, 62,108,8,92,    0, 32,16, 66, 72, 76,70},
        {75,25,58, 64,76, 82, 30, 74,6,110,  0, 28,26,112,110, 92,78},
        { 0,75,58, 64,75, 40,112,118,0,24,   0, 70,30, 54, 48, 50,42},
        {100,0,64, 70,85, 66, 90,100,0,48, 100,100,54,100,120,108,46},
    };

    scene_t s = make_foundation_scene(0);
    const legacy_values_t &v = legacy[program];
    s.osc1_wave = v.osc1_wave; s.osc1_shape = 0; s.osc1_morph = 64;
    s.osc2_wave = v.osc2_wave; s.osc2_coarse = 64;
    s.osc2_pitch = v.osc2_pitch; s.osc_mix = v.osc_mix; s.sub_mix = v.sub_mix;
    s.cutoff = v.cutoff; s.resonance = v.resonance; s.filter_eg = v.filter_eg;
    s.amp_attack = v.amp_attack; s.amp_release = v.amp_release;
    s.eg_release = v.amp_release; s.lfo_wave = v.lfo_wave;
    s.lfo_rate = v.lfo_rate; s.lfo_depth = v.lfo_depth; s.lfo_fade = 0;
    s.chorus_mix = v.chorus_mix; s.chorus_depth = v.chorus_depth;
    s.delay_feedback = v.delay_feedback; s.delay_time = v.delay_time;
    s.delay_mode = (program & 1u) ? 64 : 0;
    s.filter_mode = 0; s.breath_filter_amt = 64; s.breath_amp_mod = 0;
    s.pitch_bend_range = 12; s.voice_mode = 0; s.voice_assignment = 0;
    return s;
}

scene_t make_scene(uint8_t id) {
    const uint8_t bank = static_cast<uint8_t>(id / scene_count);
    const uint8_t program = id % scene_count;
    if (bank == 0u) return make_legacy_scene(program);

    scene_t s = make_foundation_scene(program);
    switch (bank) {
        case 1:  // Foundation: authored full-capability reference bank.
            return s;
        case 2:  // Organic: breath, slow irregular movement, softer edges.
            s.osc1_wave = (program & 1u) ? 25 : 75;
            s.osc1_shape = adjust_u7(s.osc1_shape, 12 + (program % 4u) * 6);
            s.cutoff = adjust_u7(s.cutoff, -8);
            s.resonance = adjust_u7(s.resonance, -6);
            s.amp_attack = adjust_u7(s.amp_attack, 12 + (program % 3u) * 8);
            s.amp_release = adjust_u7(s.amp_release, 18);
            s.lfo_wave = (program % 4u == 3u) ? LFO_WAVE_S_AND_H
                                               : LFO_WAVE_TRIANGLE;
            s.lfo_rate = adjust_u7(s.lfo_rate, -14);
            s.lfo_depth = adjust_u7(s.lfo_depth, 14);
            s.lfo_fade = adjust_u7(s.lfo_fade, 28);
            s.breath_filter_amt = adjust_u7(s.breath_filter_amt, 28);
            if (program % 3u == 0u) s.breath_amp_mod = 64;
            s.chorus_mix = adjust_u7(s.chorus_mix, 14);
            s.chorus_depth = adjust_u7(s.chorus_depth, 12);
            s.delay_feedback = adjust_u7(s.delay_feedback, 8);
            s.amp_gain = 82;
            break;
        case 3:  // Percussive: impacts, plucks, tuned transients and ratchets.
            s.osc1_wave = (program & 1u) ? 127 : 0;
            s.amp_attack = 0;
            s.amp_decay = static_cast<uint8_t>(28u + (program % 4u) * 12u);
            s.amp_sustain = static_cast<uint8_t>((program % 3u) * 22u);
            s.amp_release = static_cast<uint8_t>(28u + (program % 5u) * 8u);
            s.eg_attack = 0; s.eg_decay = adjust_u7(s.amp_decay, -8);
            s.eg_sustain = 0; s.eg_release = s.amp_release;
            s.eg_osc_amt = static_cast<uint8_t>(78u + (program % 4u) * 12u);
            s.eg_osc_dst = (program & 1u) ? 127 : 64;
            s.release_equals_decay = (program & 2u) ? 127 : 0;
            s.cutoff = adjust_u7(s.cutoff, 8); s.filter_eg = adjust_u7(s.filter_eg, 20);
            s.breath_amp_mod = 0; s.chorus_mix = adjust_u7(s.chorus_mix, -20);
            s.delay_feedback = adjust_u7(s.delay_feedback, -8); s.amp_gain = 96;
            break;
        case 4:  // Bass & Lead: sub weight, acid bite and glide articulation.
            s.sub_mix = static_cast<uint8_t>(92u + (program % 4u) * 8u);
            s.osc2_coarse = (program < 8u) ? 52 : 64;
            s.osc2_pitch = adjust_u7(s.osc2_pitch, (program & 1u) ? -7 : 7);
            s.cutoff = adjust_u7(s.cutoff, -18);
            s.resonance = adjust_u7(s.resonance, 14 + (program % 3u) * 6);
            s.filter_eg = adjust_u7(s.filter_eg, 18);
            if (program >= 8u) {
                s.voice_mode = (program & 1u) ? 4 : 5;
                s.portamento = static_cast<uint8_t>(46u + (program % 4u) * 18u);
                s.pitch_bend_range = 24;
            }
            s.amp_attack = (program < 8u) ? 0 : 6;
            s.amp_release = adjust_u7(s.amp_release, -10);
            s.delay_feedback = adjust_u7(s.delay_feedback, -10); s.amp_gain = 92;
            break;
        case 5:  // Atmosphere: sparse long envelopes and deep stereo space.
            s.amp_attack = static_cast<uint8_t>(54u + (program % 4u) * 12u);
            s.amp_decay = adjust_u7(s.amp_decay, 18); s.amp_sustain = 112;
            s.amp_release = static_cast<uint8_t>(92u + (program % 4u) * 10u);
            s.eg_attack = adjust_u7(s.amp_attack, 6); s.eg_release = s.amp_release;
            s.lfo_rate = adjust_u7(s.lfo_rate, -20); s.lfo_depth = adjust_u7(s.lfo_depth, 16);
            s.lfo_fade = adjust_u7(s.lfo_fade, 48);
            s.chorus_mix = adjust_u7(s.chorus_mix, 34);
            s.chorus_depth = adjust_u7(s.chorus_depth, 30);
            s.delay_feedback = adjust_u7(s.delay_feedback, 34);
            s.delay_time = adjust_u7(s.delay_time, 18); s.delay_mode = 64;
            s.amp_gain = 76;
            break;
        case 6:  // Spectral: high-pass, glass, noise and pitch geometry.
            s.filter_mode = (program & 1u) ? 64 : 0;
            s.osc1_wave = static_cast<uint8_t>((program % 5u) * 25u);
            s.osc2_wave = (program % 4u == 0u) ? 100 :
                          static_cast<uint8_t>(((program + 2u) % 5u) * 25u);
            s.osc1_shape = static_cast<uint8_t>(48u + (program % 5u) * 18u);
            s.osc2_pitch = adjust_u7(72, static_cast<int>(program % 5u) * 7 - 14);
            s.eg_osc_amt = static_cast<uint8_t>(72u + (program % 5u) * 11u);
            s.eg_osc_dst = program % 3u == 0u ? 0 :
                           program % 3u == 1u ? 64 : 127;
            s.lfo_osc_dst = (program & 2u) ? 127 : 64;
            s.lfo_wave = (program % 3u == 0u) ? LFO_WAVE_S_AND_H : LFO_WAVE_TRIANGLE;
            s.resonance = adjust_u7(s.resonance, 20);
            s.chorus_mix = adjust_u7(s.chorus_mix, 22); s.amp_gain = 80;
            break;
        case 7:  // Extreme: bounded edge cases across the complete engine.
            s.osc1_wave = static_cast<uint8_t>((program % 6u) < 5u ?
                (program % 6u) * 25u : 127u);
            s.osc1_shape = static_cast<uint8_t>(96u + (program % 4u) * 10u);
            s.sub_mix = (program & 1u) ? 120 : 70;
            s.osc2_wave = (program % 3u == 0u) ? 100 : 127;
            s.osc2_coarse = (program & 2u) ? 88 : 44;
            s.osc2_pitch = (program & 1u) ? 96 : 48;
            s.cutoff = static_cast<uint8_t>(28u + (program % 5u) * 18u);
            s.resonance = static_cast<uint8_t>(96u + (program % 4u) * 10u);
            s.filter_mode = (program & 1u) ? 64 : 0;
            s.eg_osc_amt = 120; s.eg_osc_dst = (program & 2u) ? 127 : 64;
            s.lfo_wave = (program & 1u) ? LFO_WAVE_S_AND_H : LFO_WAVE_SQUARE;
            s.lfo_rate = static_cast<uint8_t>(88u + (program % 4u) * 12u);
            s.lfo_depth = static_cast<uint8_t>(72u + (program % 5u) * 11u);
            s.lfo_osc_dst = (program & 2u) ? 127 : 64; s.lfo_filter_amt = 116;
            s.breath_filter_amt = 120; s.breath_amp_mod = (program & 4u) ? 96 : 0;
            s.chorus_mix = 112; s.chorus_depth = 116;
            s.delay_feedback = static_cast<uint8_t>(88u + (program % 4u) * 10u);
            s.delay_mode = 64; s.pitch_bend_range = 36; s.amp_gain = 70;
            break;
        case 8:  // Dub Techno: dark chords, sub weight and spacious echoes.
            s.osc1_wave = (program & 1u) ? 25u : 0u;
            s.sub_mix = adjust_u7(s.sub_mix, 22);
            s.cutoff = adjust_u7(s.cutoff, -22);
            s.resonance = adjust_u7(s.resonance, 12);
            s.amp_attack = (program & 3u) == 0u ? 0u : 10u;
            s.amp_release = adjust_u7(s.amp_release, 12);
            s.chorus_mix = adjust_u7(s.chorus_mix, 18);
            s.delay_feedback = static_cast<uint8_t>(72u + (program & 3u) * 9u);
            s.delay_time = static_cast<uint8_t>(52u + (program % 5u) * 9u);
            s.delay_mode = 64u;
            break;
        case 9:  // Motorik: stable pulse, clean harmonics and sequencer motion.
            s.osc1_wave = (program & 1u) ? 75u : 25u;
            s.osc2_wave = 75u;
            s.cutoff = adjust_u7(s.cutoff, 8);
            s.resonance = adjust_u7(s.resonance, -6);
            s.amp_attack = (program & 3u) == 3u ? 8u : 0u;
            s.amp_release = adjust_u7(s.amp_release, -8);
            s.lfo_wave = LFO_WAVE_TRIANGLE;
            s.lfo_rate = static_cast<uint8_t>(34u + (program & 3u) * 8u);
            s.delay_feedback = adjust_u7(s.delay_feedback, -12);
            break;
        case 10:  // Polyrhythmic Organic: woody bodies and breathing movement.
            s.osc1_wave = 25u;
            s.osc2_wave = (program & 1u) ? 75u : 100u;
            s.osc1_shape = adjust_u7(s.osc1_shape, 18);
            s.cutoff = adjust_u7(s.cutoff, -8);
            s.amp_attack = adjust_u7(s.amp_attack, 8);
            s.amp_release = adjust_u7(s.amp_release, 14);
            s.lfo_wave = (program & 3u) == 3u ? LFO_WAVE_S_AND_H
                                               : LFO_WAVE_TRIANGLE;
            s.lfo_rate = adjust_u7(s.lfo_rate, -12);
            s.breath_filter_amt = adjust_u7(s.breath_filter_amt, 26);
            s.chorus_mix = adjust_u7(s.chorus_mix, 12);
            break;
        case 11:  // Cinematic: slow contours, harmonic breadth and deep space.
            s.osc1_wave = (program & 1u) ? 75u : 25u;
            s.osc2_coarse = (program & 2u) ? 76u : 52u;
            s.amp_attack = static_cast<uint8_t>(32u + (program & 3u) * 14u);
            s.amp_sustain = 108u;
            s.amp_release = static_cast<uint8_t>(82u + (program & 3u) * 10u);
            s.lfo_rate = adjust_u7(s.lfo_rate, -22);
            s.lfo_fade = adjust_u7(s.lfo_fade, 42);
            s.chorus_mix = 108u;
            s.chorus_depth = 104u;
            s.delay_feedback = static_cast<uint8_t>(70u + (program & 3u) * 8u);
            s.delay_time = 82u;
            s.delay_mode = 64u;
            s.amp_gain = 76u;
            break;
        case 12:  // Acid & Electro: resonant envelopes and machine articulation.
            s.osc1_wave = 0u;
            s.osc1_shape = static_cast<uint8_t>(88u + (program & 3u) * 10u);
            s.osc2_wave = 127u;
            s.sub_mix = adjust_u7(s.sub_mix, 16);
            s.cutoff = static_cast<uint8_t>(34u + (program % 5u) * 9u);
            s.resonance = static_cast<uint8_t>(86u + (program & 3u) * 10u);
            s.filter_eg = 112u;
            s.eg_decay = static_cast<uint8_t>(30u + (program & 3u) * 9u);
            s.eg_sustain = 0u;
            s.amp_attack = 0u;
            s.amp_release = adjust_u7(s.amp_release, -16);
            s.delay_feedback = adjust_u7(s.delay_feedback, -14);
            break;
        case 13:  // Broken Beat & IDM: angular transients and controlled S&H.
            s.osc1_wave = (program & 1u) ? 100u : 127u;
            s.osc2_wave = (program & 2u) ? 25u : 100u;
            s.osc2_pitch = static_cast<uint8_t>(54u + (program % 5u) * 9u);
            s.filter_mode = (program & 1u) ? 64u : 0u;
            s.resonance = adjust_u7(s.resonance, 16);
            s.amp_attack = 0u;
            s.amp_release = adjust_u7(s.amp_release, -18);
            s.lfo_wave = LFO_WAVE_S_AND_H;
            s.lfo_rate = static_cast<uint8_t>(62u + (program & 3u) * 13u);
            s.lfo_depth = static_cast<uint8_t>(30u + (program & 3u) * 10u);
            s.delay_feedback = static_cast<uint8_t>(44u + (program & 3u) * 12u);
            break;
        case 14:  // Minimal Phase: restrained tone with slow cumulative motion.
            s.osc1_wave = 75u;
            s.osc2_wave = 25u;
            s.osc2_pitch = (program & 1u) ? 76u : 72u;
            s.resonance = adjust_u7(s.resonance, -8);
            s.amp_attack = adjust_u7(s.amp_attack, 12);
            s.amp_release = adjust_u7(s.amp_release, 18);
            s.lfo_wave = LFO_WAVE_TRIANGLE;
            s.lfo_rate = static_cast<uint8_t>(22u + (program & 3u) * 6u);
            s.lfo_depth = adjust_u7(s.lfo_depth, 8);
            s.chorus_mix = adjust_u7(s.chorus_mix, 10);
            s.delay_feedback = adjust_u7(s.delay_feedback, 4);
            break;
        default:  // Chiptune: pulse geometry, noise accents and bright arps.
            s.osc1_wave = (program & 1u) ? 127u : 0u;
            s.osc1_shape = static_cast<uint8_t>(36u + (program & 3u) * 24u);
            s.osc2_wave = 127u;
            s.osc2_coarse = (program & 2u) ? 76u : 64u;
            s.osc2_pitch = (program & 1u) ? 84u : 72u;
            s.cutoff = adjust_u7(s.cutoff, 20);
            s.resonance = adjust_u7(s.resonance, -10);
            s.amp_attack = 0u;
            s.amp_decay = static_cast<uint8_t>(34u + (program & 3u) * 8u);
            s.amp_sustain = static_cast<uint8_t>(50u + (program & 3u) * 14u);
            s.amp_release = static_cast<uint8_t>(24u + (program & 3u) * 8u);
            s.lfo_wave = (program & 2u) ? LFO_WAVE_SQUARE : LFO_WAVE_TRIANGLE;
            s.chorus_mix = adjust_u7(s.chorus_mix, -24);
            s.delay_feedback = adjust_u7(s.delay_feedback, -18);
            break;
    }
    return s;
}

struct sensor_route_t {
    uint8_t breath_max;
    uint8_t modulation_max;
    float cutoff_range;
    float resonance_range;
    float morph_range;
    float lfo_rate_range;
    float bend_scale;
    int8_t density_offset;
    float ratchet_scale;
};

static uint8_t &patch_behavior_storage(patch_record_t &record,
                                       uint8_t parameter) {
    switch (parameter) {
        case PATCH_BREATH_OVERRIDE: return record.lane[bass_lane].chorus_rate;
        case PATCH_BEND_PERCENT: return record.lane[bass_lane].chorus_depth;
        case PATCH_RATCHET_PERCENT: return record.lane[bass_lane].delay_feedback;
        case PATCH_AMP_DECAY_MOTION: return record.lane[bass_lane].delay_time;
        case PATCH_AMP_SUSTAIN_MOTION: return record.lane[bass_lane].delay_mode;
        case PATCH_AMP_RELEASE_MOTION: return record.lane[upper_lane].chorus_mix;
        case PATCH_CUTOFF_PERCENT: return record.lane[upper_lane].chorus_depth;
        case PATCH_RESONANCE_PERCENT: return record.lane[upper_lane].delay_feedback;
        case PATCH_MORPH_PERCENT: return record.lane[upper_lane].delay_time;
        default: return record.lane[upper_lane].delay_mode;
    }
}

static const uint8_t &patch_behavior_storage(const patch_record_t &record,
                                             uint8_t parameter) {
    switch (parameter) {
        case PATCH_BREATH_OVERRIDE: return record.lane[bass_lane].chorus_rate;
        case PATCH_BEND_PERCENT: return record.lane[bass_lane].chorus_depth;
        case PATCH_RATCHET_PERCENT: return record.lane[bass_lane].delay_feedback;
        case PATCH_AMP_DECAY_MOTION: return record.lane[bass_lane].delay_time;
        case PATCH_AMP_SUSTAIN_MOTION: return record.lane[bass_lane].delay_mode;
        case PATCH_AMP_RELEASE_MOTION: return record.lane[upper_lane].chorus_mix;
        case PATCH_CUTOFF_PERCENT: return record.lane[upper_lane].chorus_depth;
        case PATCH_RESONANCE_PERCENT: return record.lane[upper_lane].delay_feedback;
        case PATCH_MORPH_PERCENT: return record.lane[upper_lane].delay_time;
        default: return record.lane[upper_lane].delay_mode;
    }
}

static uint8_t patch_proximity_octaves(const patch_record_t &record) {
    return record.lane[upper_lane].chorus_rate & 3u;
}

static uint8_t patch_expression_octave_threshold(
                                             const patch_record_t &record) {
    return record.lane[upper_lane].chorus_rate >> 2u;
}

static void set_patch_octave_behavior(patch_record_t &record,
                                      uint8_t proximity_octaves,
                                      uint8_t expression_threshold) {
    record.lane[upper_lane].chorus_rate = static_cast<uint8_t>(
        (proximity_octaves & 3u) | ((expression_threshold & 63u) << 2u));
}

static void build_default_patch_behavior(uint8_t id, patch_record_t *record,
                                         bool author_envelope_base) {
    const uint8_t bank = id / scene_count;
    const uint8_t program = id % scene_count;
    record->lane[bass_lane].chorus_mix = patch_behavior_marker;
    patch_behavior_storage(*record, PATCH_BREATH_OVERRIDE) =
        program == 14u ? 127u : 0u;
    patch_behavior_storage(*record, PATCH_BEND_PERCENT) =
        program == 10u ? 125u : 100u;
    patch_behavior_storage(*record, PATCH_RATCHET_PERCENT) =
        program == 13u ? 120u : 100u;
    patch_behavior_storage(*record, PATCH_AMP_DECAY_MOTION) =
        bank <= 1u && (program == 4u || program == 8u || program == 13u)
            ? static_cast<uint8_t>(program == 4u ? 48u :
                                   program == 8u ? 62u : 58u) : 0u;
    patch_behavior_storage(*record, PATCH_AMP_SUSTAIN_MOTION) =
        bank <= 1u && (program == 4u || program == 8u || program == 13u)
            ? static_cast<uint8_t>(program == 4u ? 72u :
                                   program == 8u ? 58u : 54u) : 0u;
    patch_behavior_storage(*record, PATCH_AMP_RELEASE_MOTION) =
        bank <= 1u && (program == 4u || program == 8u || program == 13u)
            ? static_cast<uint8_t>(program == 4u ? 58u :
                                   program == 8u ? 62u : 74u) : 0u;
    if (author_envelope_base && bank <= 1u &&
        (program == 4u || program == 8u || program == 13u)) {
        const uint8_t decay = program == 4u ? 42u : program == 8u ? 40u : 34u;
        const uint8_t sustain = program == 4u ? 38u : program == 8u ? 48u : 18u;
        const uint8_t release = program == 4u ? 58u : program == 8u ? 32u : 30u;
        record->lane[bass_lane].amp_decay = adjust_u7(decay, 8);
        record->lane[bass_lane].amp_sustain = adjust_u7(sustain, 8);
        record->lane[bass_lane].amp_release = adjust_u7(release, 10);
        record->lane[middle_lane].amp_decay = decay;
        record->lane[middle_lane].amp_sustain = sustain;
        record->lane[middle_lane].amp_release = release;
        record->lane[upper_lane].amp_decay = adjust_u7(decay, -8);
        record->lane[upper_lane].amp_sustain = adjust_u7(sustain, -10);
        record->lane[upper_lane].amp_release = adjust_u7(release, -12);
    }
    set_patch_octave_behavior(*record, bank == 4u ? 1u : 2u,
                              bank == 6u ? 41u : 0u);
    patch_behavior_storage(*record, PATCH_CUTOFF_PERCENT) = 100u;
    patch_behavior_storage(*record, PATCH_RESONANCE_PERCENT) = 100u;
    patch_behavior_storage(*record, PATCH_MORPH_PERCENT) = 100u;
    patch_behavior_storage(*record, PATCH_LFO_RATE_PERCENT) = 100u;
}

sensor_route_t generated_sensor_route(uint8_t bank) {
    static constexpr sensor_route_t routes[16] = {
        {  0,   0, 34.0f, 14.0f, 70.0f, 48.0f, 0.55f,  0, 0.85f}, // Legacy
        {127, 127, 34.0f, 14.0f, 42.0f, 24.0f, 1.00f,  0, 1.00f}, // Foundation
        {127,  76, 24.0f, 10.0f, 58.0f, 18.0f, 0.65f, -1, 0.72f}, // Organic
        { 54,  62, 42.0f, 20.0f, 28.0f, 32.0f, 0.35f,  2, 1.45f}, // Percussive
        { 86,  70, 38.0f, 24.0f, 34.0f, 22.0f, 0.85f,  1, 1.12f}, // Bass/Lead
        {108,  54, 18.0f,  8.0f, 46.0f, 12.0f, 0.45f, -2, 0.45f}, // Atmosphere
        { 92, 112, 46.0f, 22.0f, 72.0f, 40.0f, 1.10f,  0, 1.08f}, // Spectral
        {127, 127, 56.0f, 30.0f, 92.0f, 56.0f, 1.40f,  2, 1.75f}, // Extreme
        {104,  72, 28.0f, 16.0f, 38.0f, 18.0f, 0.60f, -1, 0.72f}, // Dub Techno
        { 82,  76, 34.0f, 12.0f, 34.0f, 24.0f, 0.55f,  1, 0.68f}, // Motorik
        {127,  96, 30.0f, 14.0f, 54.0f, 22.0f, 0.70f,  0, 0.82f}, // Organic poly
        {118,  68, 24.0f, 12.0f, 48.0f, 14.0f, 0.65f, -2, 0.42f}, // Cinematic
        { 76,  92, 48.0f, 26.0f, 28.0f, 34.0f, 0.80f,  1, 1.10f}, // Acid/Electro
        { 92, 118, 44.0f, 22.0f, 62.0f, 46.0f, 1.00f,  1, 1.38f}, // Broken/IDM
        {112,  82, 22.0f,  8.0f, 42.0f, 16.0f, 0.45f, -1, 0.52f}, // Minimal Phase
        { 68, 104, 38.0f, 14.0f, 32.0f, 38.0f, 0.70f,  1, 1.18f}, // Chiptune
    };
    return routes[bank];
}

scene_t make_generated_lane_scene(uint8_t id, uint8_t lane) {
    scene_t scene = make_scene(id);
    const uint8_t bank = id / scene_count;
    const uint8_t program = id % scene_count;

    if (lane == bass_lane) {
        scene.voice_mode = VOICE_MONOPHONIC;
        scene.voice_assignment = 0u;
        scene.sub_mix = adjust_u7(scene.sub_mix, 28);
        scene.osc2_coarse = adjust_u7(scene.osc2_coarse, -12);
        scene.osc_mix = adjust_u7(scene.osc_mix, -10);
        scene.cutoff = adjust_u7(scene.cutoff, -24);
        scene.resonance = adjust_u7(scene.resonance, 6);
        scene.filter_key_track = adjust_u7(scene.filter_key_track, -18);
        scene.amp_attack = adjust_u7(scene.amp_attack, -8);
        scene.amp_release = adjust_u7(scene.amp_release, 10);
        scene.lfo_rate = adjust_u7(scene.lfo_rate, -10);
        scene.lfo_filter_amt = adjust_u7(scene.lfo_filter_amt, -12);
        scene.amp_gain = adjust_u7(scene.amp_gain, 12);

        switch (bank) {
            case 2u:  // Organic: rounded mallet/bass with breathing tail.
                scene.osc1_wave = 25u;
                scene.cutoff = adjust_u7(scene.cutoff, -10);
                scene.amp_decay = adjust_u7(scene.amp_decay, 14);
                scene.amp_release = adjust_u7(scene.amp_release, 18);
                break;
            case 3u:  // Percussive: tuned kick/tom transient.
                scene.osc1_wave = 0u;
                scene.amp_attack = 0u;
                scene.amp_decay = static_cast<uint8_t>(28u + (program % 4u) * 8u);
                scene.amp_sustain = static_cast<uint8_t>((program & 1u) ? 18u : 0u);
                scene.amp_release = static_cast<uint8_t>(24u + (program % 3u) * 8u);
                scene.eg_attack = 0u;
                scene.eg_decay = adjust_u7(scene.amp_decay, -8);
                scene.eg_sustain = 0u;
                scene.eg_osc_amt = adjust_u7(scene.eg_osc_amt, 28);
                scene.eg_osc_dst = 127u;
                if ((program & 3u) == 0u) {  // Kick: sub-heavy pitch drop.
                    scene.sub_mix = 124u;
                    scene.cutoff = adjust_u7(scene.cutoff, -14);
                    scene.amp_decay = 38u;
                } else if ((program & 3u) == 1u) {  // Tom: pitched body plus noise edge.
                    scene.sub_mix = 96u;
                    scene.osc2_wave = 100u;
                    scene.osc_mix = 34u;
                    scene.amp_decay = 50u;
                    scene.amp_sustain = 10u;
                } else if ((program & 3u) == 2u) {  // Snare: noise-forward high-pass snap.
                    scene.sub_mix = 18u;
                    scene.osc2_wave = 100u;
                    scene.osc_mix = 82u;
                    scene.filter_mode = 64u;
                    scene.cutoff = 42u;
                    scene.resonance = 34u;
                    scene.amp_decay = 42u;
                    scene.amp_sustain = 0u;
                    scene.amp_release = 30u;
                    scene.eg_osc_amt = 64u;
                } else {  // Hat: near-pure noise through a high-pass filter.
                    scene.sub_mix = 0u;
                    scene.osc2_wave = 100u;
                    scene.osc_mix = 112u;
                    scene.filter_mode = 64u;
                    scene.cutoff = 86u;
                    scene.resonance = 18u;
                    scene.amp_decay = 18u;
                    scene.amp_sustain = 0u;
                    scene.amp_release = 14u;
                    scene.eg_osc_amt = 64u;
                }
                break;
            case 4u:  // Bass/Lead: deepest sustained analog bass.
                scene.sub_mix = 127u;
                scene.cutoff = adjust_u7(scene.cutoff, -12);
                scene.filter_eg = adjust_u7(scene.filter_eg, 18);
                scene.amp_sustain = adjust_u7(scene.amp_sustain, 18);
                break;
            case 5u:  // Atmosphere: sub drone.
                scene.amp_attack = adjust_u7(scene.amp_attack, 26);
                scene.amp_sustain = 118u;
                scene.amp_release = adjust_u7(scene.amp_release, 30);
                scene.lfo_rate = adjust_u7(scene.lfo_rate, -14);
                break;
            case 6u:  // Spectral: hollow resonant low percussion.
                scene.osc1_wave = (program & 1u) ? 100u : 25u;
                scene.filter_mode = (program & 2u) ? 64u : 0u;
                scene.resonance = adjust_u7(scene.resonance, 18);
                scene.eg_osc_amt = adjust_u7(scene.eg_osc_amt, 18);
                break;
            case 7u:  // Extreme: clipped/noisy impact bass.
                scene.sub_mix = 127u;
                scene.resonance = adjust_u7(scene.resonance, 20);
                scene.amp_attack = 0u;
                scene.amp_decay = adjust_u7(scene.amp_decay, -18);
                scene.lfo_wave = LFO_WAVE_S_AND_H;
                break;
            case 8u:  // Dub: round sub with occasional pitch-envelope weight.
                scene.sub_mix = 127u; scene.cutoff = 30u;
                scene.amp_sustain = 94u; scene.amp_release = 54u;
                break;
            case 9u:  // Motorik: short, even ostinato bass.
                scene.sub_mix = 104u; scene.amp_attack = 0u;
                scene.amp_decay = 58u; scene.amp_sustain = 76u;
                scene.amp_release = 34u;
                break;
            case 10u:  // Organic polyrhythm: tuned wood/tom body.
                scene.osc1_wave = 25u; scene.eg_osc_amt = 94u;
                scene.eg_osc_dst = 127u; scene.amp_decay = 64u;
                scene.amp_sustain = 28u; scene.amp_release = 52u;
                break;
            case 11u:  // Cinematic: sub swell and impact body.
                scene.amp_attack = adjust_u7(scene.amp_attack, 18);
                scene.amp_sustain = 112u;
                scene.amp_release = adjust_u7(scene.amp_release, 24);
                break;
            case 12u:  // Acid/Electro: tight resonant bass.
                scene.sub_mix = 118u; scene.cutoff = 34u;
                scene.resonance = 104u; scene.filter_eg = 120u;
                scene.amp_decay = 48u; scene.amp_release = 30u;
                break;
            case 13u:  // Broken/IDM: short hybrid impact.
                scene.amp_attack = 0u; scene.amp_decay = 38u;
                scene.amp_sustain = 12u; scene.amp_release = 28u;
                scene.lfo_wave = LFO_WAVE_S_AND_H;
                break;
            case 14u:  // Minimal: stable tonal anchor.
                scene.sub_mix = 108u; scene.resonance = 20u;
                scene.amp_sustain = 96u; scene.amp_release = 56u;
                break;
            case 15u:  // Chiptune: pulse bass with a noise-capable edge.
                scene.osc1_wave = 127u; scene.osc1_shape = 40u;
                scene.sub_mix = (program & 3u) == 3u ? 28u : 88u;
                scene.amp_decay = 44u; scene.amp_release = 24u;
                break;
            default:  // Legacy/Foundation: versatile synth bass.
                break;
        }
    } else if (lane == middle_lane) {
        scene.voice_mode = VOICE_MONOPHONIC;
        scene.voice_assignment = 0u;
        scene.sub_mix = adjust_u7(scene.sub_mix, -12);
        scene.cutoff = adjust_u7(scene.cutoff, -4);
        scene.amp_attack = adjust_u7(scene.amp_attack, 12);
        scene.amp_sustain = adjust_u7(scene.amp_sustain, 16);
        scene.amp_release = adjust_u7(scene.amp_release, 20);
        scene.chorus_mix = adjust_u7(scene.chorus_mix, 16);
        scene.chorus_depth = adjust_u7(scene.chorus_depth, 12);
        scene.delay_feedback = adjust_u7(scene.delay_feedback, 8);
        scene.amp_gain = adjust_u7(scene.amp_gain, -6);

        switch (bank) {
            case 2u:  // Organic: warm breathing pad.
                scene.osc1_wave = 25u;
                scene.osc2_wave = 75u;
                scene.amp_attack = adjust_u7(scene.amp_attack, 20);
                scene.amp_release = adjust_u7(scene.amp_release, 20);
                scene.lfo_wave = LFO_WAVE_TRIANGLE;
                scene.lfo_rate = adjust_u7(scene.lfo_rate, -16);
                break;
            case 3u:  // Percussive: compact two-note chord stabs.
                scene.amp_attack = 0u;
                scene.amp_decay = static_cast<uint8_t>(38u + (program % 4u) * 9u);
                scene.amp_sustain = static_cast<uint8_t>(24u + (program % 3u) * 12u);
                scene.amp_release = static_cast<uint8_t>(34u + (program % 4u) * 7u);
                scene.chorus_mix = adjust_u7(scene.chorus_mix, -18);
                scene.delay_feedback = adjust_u7(scene.delay_feedback, -12);
                break;
            case 4u:  // Bass/Lead: dark analog chord bed.
                scene.osc1_wave = 0u;
                scene.cutoff = adjust_u7(scene.cutoff, -16);
                scene.resonance = adjust_u7(scene.resonance, 10);
                scene.amp_attack = adjust_u7(scene.amp_attack, -8);
                break;
            case 5u:  // Atmosphere: longest evolving pad.
                scene.amp_attack = adjust_u7(scene.amp_attack, 26);
                scene.amp_sustain = 122u;
                scene.amp_release = adjust_u7(scene.amp_release, 28);
                scene.chorus_mix = 120u;
                scene.chorus_depth = 116u;
                scene.delay_feedback = adjust_u7(scene.delay_feedback, 24);
                break;
            case 6u:  // Spectral: glass/high-pass pad.
                scene.filter_mode = (program & 1u) ? 64u : scene.filter_mode;
                scene.osc2_pitch = adjust_u7(scene.osc2_pitch, 7);
                scene.resonance = adjust_u7(scene.resonance, 14);
                scene.lfo_filter_amt = adjust_u7(scene.lfo_filter_amt, 18);
                break;
            case 7u:  // Extreme: animated gated/noise cloud.
                scene.lfo_wave = LFO_WAVE_S_AND_H;
                scene.lfo_rate = adjust_u7(scene.lfo_rate, 18);
                scene.lfo_depth = adjust_u7(scene.lfo_depth, 20);
                scene.resonance = adjust_u7(scene.resonance, 16);
                break;
            case 8u:  // Dub: compact chord body feeding the shared delay.
                scene.amp_attack = 0u; scene.amp_decay = 64u;
                scene.amp_sustain = 54u; scene.amp_release = 70u;
                scene.chorus_mix = 88u; scene.delay_feedback = 92u;
                scene.delay_mode = 64u;
                break;
            case 9u:  // Motorik: sustained harmonic bed behind the pulse.
                scene.amp_attack = 10u; scene.amp_sustain = 104u;
                scene.amp_release = 60u; scene.chorus_mix = 68u;
                break;
            case 10u:  // Organic polyrhythm: warm breathing chords.
                scene.osc1_wave = 25u; scene.osc2_wave = 75u;
                scene.amp_attack = 28u; scene.amp_release = 78u;
                scene.breath_amp_mod = 72u;
                break;
            case 11u:  // Cinematic: dominant long-form pad.
                scene.amp_attack = 72u; scene.amp_sustain = 122u;
                scene.amp_release = 118u; scene.chorus_mix = 120u;
                scene.chorus_depth = 116u; scene.delay_feedback = 94u;
                scene.delay_mode = 64u;
                break;
            case 12u:  // Acid/Electro: clipped machine chord.
                scene.amp_attack = 0u; scene.amp_decay = 46u;
                scene.amp_sustain = 36u; scene.amp_release = 34u;
                scene.resonance = 72u; scene.delay_feedback = 34u;
                break;
            case 13u:  // Broken/IDM: offset digital stab.
                scene.amp_attack = 0u; scene.amp_decay = 52u;
                scene.amp_sustain = 42u; scene.amp_release = 38u;
                scene.lfo_wave = LFO_WAVE_S_AND_H;
                break;
            case 14u:  // Minimal: slowly phasing harmonic cell.
                scene.amp_attack = 24u; scene.amp_sustain = 106u;
                scene.amp_release = 82u; scene.lfo_rate = 26u;
                scene.lfo_depth = 22u;
                break;
            case 15u:  // Chiptune: compact pulse harmony.
                scene.osc1_wave = 127u; scene.osc2_wave = 0u;
                scene.amp_attack = 0u; scene.amp_decay = 54u;
                scene.amp_sustain = 76u; scene.amp_release = 30u;
                scene.chorus_mix = 38u; scene.delay_feedback = 30u;
                break;
            default:  // Legacy/Foundation: balanced playable pad.
                break;
        }
    } else {
        scene.voice_mode = VOICE_MONOPHONIC;
        scene.voice_assignment = 0u;
        scene.sub_mix = adjust_u7(scene.sub_mix, -48);
        scene.osc2_coarse = adjust_u7(scene.osc2_coarse, 12);
        scene.osc2_pitch = adjust_u7(scene.osc2_pitch, 7);
        scene.osc_mix = adjust_u7(scene.osc_mix, 14);
        scene.cutoff = adjust_u7(scene.cutoff, 22);
        scene.resonance = adjust_u7(scene.resonance, 8);
        scene.filter_key_track = adjust_u7(scene.filter_key_track, 20);
        scene.amp_attack = adjust_u7(scene.amp_attack, -10);
        scene.amp_release = adjust_u7(scene.amp_release, -10);
        scene.lfo_rate = adjust_u7(scene.lfo_rate, 12);
        scene.lfo_depth = adjust_u7(scene.lfo_depth, 8);
        scene.amp_gain = adjust_u7(scene.amp_gain, 4);

        switch (bank) {
            case 2u:  // Organic: expressive reed lead.
                scene.osc1_wave = 127u;
                scene.osc1_shape = adjust_u7(scene.osc1_shape, 18);
                scene.breath_filter_amt = 127u;
                scene.breath_amp_mod = adjust_u7(scene.breath_amp_mod, 26);
                scene.lfo_rate = adjust_u7(scene.lfo_rate, -8);
                break;
            case 3u:  // Percussive: bright pluck lead.
                scene.amp_attack = 0u;
                scene.amp_decay = static_cast<uint8_t>(30u + (program % 5u) * 8u);
                scene.amp_sustain = static_cast<uint8_t>((program % 3u) * 14u);
                scene.amp_release = static_cast<uint8_t>(26u + (program % 4u) * 7u);
                scene.eg_osc_amt = adjust_u7(scene.eg_osc_amt, 24);
                break;
            case 4u:  // Bass/Lead: portamento acid/solo voice.
                scene.portamento = static_cast<uint8_t>(44u + (program % 5u) * 14u);
                scene.resonance = adjust_u7(scene.resonance, 18);
                scene.filter_eg = adjust_u7(scene.filter_eg, 16);
                scene.pitch_bend_range = 24u;
                break;
            case 5u:  // Atmosphere: slow airy lead.
                scene.filter_mode = 64u;
                scene.amp_attack = adjust_u7(scene.amp_attack, 22);
                scene.amp_release = adjust_u7(scene.amp_release, 28);
                scene.lfo_fade = adjust_u7(scene.lfo_fade, 36);
                break;
            case 6u:  // Spectral: glass/noise lead.
                scene.osc1_wave = (program % 3u == 0u) ? 100u : 127u;
                scene.filter_mode = (program & 1u) ? 64u : 0u;
                scene.eg_osc_amt = adjust_u7(scene.eg_osc_amt, 28);
                scene.lfo_osc_amt = adjust_u7(scene.lfo_osc_amt, 20);
                break;
            case 7u:  // Extreme: unstable S&H lead.
                scene.lfo_wave = LFO_WAVE_S_AND_H;
                scene.lfo_rate = adjust_u7(scene.lfo_rate, 24);
                scene.lfo_depth = adjust_u7(scene.lfo_depth, 22);
                scene.pitch_bend_range = 36u;
                break;
            case 8u:  // Dub: rare high echo punctuation.
                scene.filter_mode = 64u; scene.amp_attack = 6u;
                scene.amp_release = 50u; scene.lfo_rate = 32u;
                break;
            case 9u:  // Motorik: bright repeating sequencer line.
                scene.osc1_wave = 75u; scene.amp_attack = 0u;
                scene.amp_decay = 52u; scene.amp_sustain = 64u;
                scene.amp_release = 28u;
                break;
            case 10u:  // Organic polyrhythm: reed/mallet response.
                scene.osc1_wave = (program & 1u) ? 127u : 25u;
                scene.breath_filter_amt = 124u;
                scene.breath_amp_mod = 88u; scene.amp_release = 54u;
                break;
            case 11u:  // Cinematic: slow high theme.
                scene.filter_mode = 64u; scene.amp_attack = 48u;
                scene.amp_sustain = 96u; scene.amp_release = 90u;
                scene.lfo_fade = 82u;
                break;
            case 12u:  // Acid/Electro: resonant glide lead.
                scene.portamento = static_cast<uint8_t>(32u + (program & 3u) * 12u);
                scene.resonance = 112u; scene.filter_eg = 116u;
                scene.amp_attack = 0u; scene.amp_release = 28u;
                break;
            case 13u:  // Broken/IDM: angular digital fragment.
                scene.osc1_wave = 100u; scene.lfo_wave = LFO_WAVE_S_AND_H;
                scene.amp_attack = 0u; scene.amp_decay = 38u;
                scene.amp_sustain = 20u; scene.amp_release = 24u;
                break;
            case 14u:  // Minimal: restricted clear melodic cell.
                scene.resonance = 20u; scene.amp_attack = 12u;
                scene.amp_sustain = 86u; scene.amp_release = 62u;
                scene.lfo_rate = 30u;
                break;
            case 15u:  // Chiptune: fast square-wave arpeggio voice.
                scene.osc1_wave = 127u; scene.osc1_shape = 92u;
                scene.osc2_wave = 0u; scene.amp_attack = 0u;
                scene.amp_decay = 38u; scene.amp_sustain = 48u;
                scene.amp_release = 20u;
                break;
            default:  // Legacy/Foundation: clear lead/pluck.
                break;
        }
    }
    return scene;
}

constexpr uint8_t pack_articulation(uint8_t algorithm, uint8_t role) {
    return static_cast<uint8_t>((algorithm & 7u) | ((role & 7u) << 3u));
}

void build_default_articulation(uint8_t bank, uint8_t program,
                                patch_record_t *record) {
    record->low_mode = LOW_MODE_INHERIT;
    record->low_balance = bank == 3u ? 112u : bank == 7u ? 92u : 64u;
    record->low_sensor = bank == 0u ? 42u : bank == 4u ? 54u : 82u;
    record->low_variation = bank == 5u ? 28u : bank == 7u ? 112u : 66u;

    const uint8_t auxiliary = bank == 2u ? PERC_RIM_WOOD :
        bank == 6u || bank == 7u ? PERC_SHAKER_METAL : PERC_CLAP;
    const articulation_slot_t defaults[6] = {
        {pack_articulation(PERC_KICK, PERC_ROLE_ANCHOR),
         116u, 108u, 19u, 24u, 8u, 42u, 110u, 20u},
        {pack_articulation(PERC_SNARE, PERC_ROLE_BACKBEAT),
         94u, 94u, 29u, 50u, 106u, 36u, 58u, 52u},
        {pack_articulation(PERC_TOM, PERC_ROLE_FREE),
         72u, 96u, 24u, 38u, 20u, 52u, 88u, 44u},
        {pack_articulation(PERC_CLOSED_HAT, PERC_ROLE_OFFBEAT),
         78u, 78u, 36u, 96u, 124u, 16u, 30u, 116u},
        {pack_articulation(PERC_OPEN_HAT, PERC_ROLE_FILL),
         46u, 76u, 36u, 102u, 127u, 40u, 24u, 100u},
        {pack_articulation(auxiliary, PERC_ROLE_FILL),
         38u, 82u, 31u, 72u, 100u, 30u, 48u, 92u},
    };
    std::memcpy(record->articulation, defaults, sizeof(defaults));

    // Four program colours retain deliberate variation without hiding any
    // parameter from the editor: these are only compiled starting values.
    const uint8_t colour = program >> 2u;
    const int tone_shift = static_cast<int>(colour) * 7 - 7;
    const int decay_shift = bank == 2u ? 12 : bank == 3u ? -4 :
                            bank == 5u ? 18 : bank == 7u ? -10 : 0;
    for (articulation_slot_t &slot : record->articulation) {
        slot.tone = adjust_u7(slot.tone, tone_shift);
        slot.decay = adjust_u7(slot.decay, decay_shift);
        if (bank == 6u) {
            slot.noise = adjust_u7(slot.noise, 14);
            slot.transient = adjust_u7(slot.transient, 12);
        } else if (bank == 5u) {
            slot.weight = static_cast<uint8_t>(slot.weight * 3u / 5u);
            slot.ratchet = static_cast<uint8_t>(slot.ratchet / 2u);
        } else if (bank == 7u) {
            slot.noise = adjust_u7(slot.noise, 20);
            slot.ratchet = adjust_u7(slot.ratchet, 24);
        }
    }
    // The low two program bits bias the kit without imposing a fixed kit.
    const uint8_t variant = program & 3u;
    if (variant == 0u) record->articulation[0].weight = 127u;
    else if (variant == 1u) record->articulation[2].weight = 122u;
    else if (variant == 2u) record->articulation[1].weight = 120u;
    else {
        record->articulation[3].weight = 122u;
        record->articulation[4].weight = 88u;
    }
}

struct percussive_profile_t {
    int8_t density[3];
    uint8_t gate_q5[3];
    uint8_t ratchet_percent[3];
    int8_t envelope_shift;
    int8_t articulation_decay_shift;
    uint8_t articulation_ratchet_percent;
};

void apply_percussive_profile(uint8_t program, patch_record_t *record) {
    // Most of the bank leaves room around full-bodied impacts. Programs 4,
    // 10 and 16 deliberately retain the dense click/micro/glitch extremes;
    // program 8 is the unchanged reference balance that inspired this pass.
    static constexpr percussive_profile_t profiles[16] = {
        {{-3, -4, -4}, { 90, 64, 48}, { 30,  45,  52},  22,  18,  68},
        {{-2, -3, -4}, { 96, 70, 46}, { 35,  50,  58},  18,  20,  72},
        {{-3, -3, -5}, { 88, 62, 42}, { 28,  45,  54},  16,  14,  70},
        {{ 1,  2,  2}, { 52, 34, 20}, { 80, 135, 150}, -10, -12, 125},
        {{-4, -5, -6}, {110, 80, 54}, { 18,  35,  42},  28,  24,  55},
        {{-2, -4, -5}, {100, 72, 48}, { 30,  48,  56},  20,  18,  68},
        {{-3, -2, -4}, { 82, 60, 38}, { 36,  62,  68},  14,  12,  78},
        {{ 0,  0,  0}, { 70, 46, 29}, { 52,  92, 100},   0,   0, 100},
        {{-5, -6, -6}, {120, 96, 70}, { 15,  28,  35},  34,  30,  48},
        {{ 2,  2,  3}, { 46, 28, 16}, { 90, 150, 170}, -14, -16, 140},
        {{-2, -3, -4}, { 90, 68, 44}, { 32,  55,  66},  18,  16,  74},
        {{-3, -4, -3}, {104, 78, 58}, { 24,  40,  50},  24,  22,  62},
        {{-5, -5, -4}, {112, 82, 52}, { 18,  38,  60},  22,  18,  66},
        {{-2, -3, -4}, { 92, 64, 42}, { 42,  72,  82},  14,  12,  82},
        {{-1, -2, -3}, { 78, 54, 34}, { 48,  80,  92},  10,   8,  90},
        {{ 2,  3,  3}, { 40, 24, 14}, {110, 175, 190}, -16, -18, 155},
    };
    const percussive_profile_t &profile = profiles[program];
    record->density_bias = profile.density[0];
    record->density_bias_pad = profile.density[1];
    record->density_bias_lead = profile.density[2];
    for (uint8_t lane = 0u; lane < SYNTH_LANE_COUNT; ++lane) {
        record->gate_q5[lane] = profile.gate_q5[lane];
        record->ratchet_percent[lane] = profile.ratchet_percent[lane];
        scene_t &scene = record->lane[lane];
        scene.amp_decay = adjust_u7(scene.amp_decay,
                                    profile.envelope_shift);
        scene.amp_sustain = adjust_u7(scene.amp_sustain,
                                      profile.envelope_shift / 3);
        scene.amp_release = adjust_u7(scene.amp_release,
                                      profile.envelope_shift);
    }
    for (articulation_slot_t &slot : record->articulation) {
        slot.decay = adjust_u7(slot.decay,
                               profile.articulation_decay_shift);
        slot.ratchet = clamp_u7(static_cast<int>(slot.ratchet) *
            profile.articulation_ratchet_percent / 100);
    }
}

struct style_composition_t {
    uint8_t bpm[16];
    uint8_t swing;
    int8_t density[3];
    uint8_t gate[3];
    uint8_t ratchet[3];
    uint8_t low_balance;
};

static constexpr style_composition_t style_compositions[8] = {
    {{ 78, 84, 92,100, 86, 96,108,116, 74, 82, 94,104, 98,110,122,128},
      58, {-1,-3,-5}, {82,58,38}, {24,44,58}, 58},
    {{104,112,118,126,108,120,128,136, 96,110,122,132,116,130,140,148},
      50, { 2,-2, 0}, {70,66,40}, {18,34,48}, 18},
    {{ 82, 90, 98,106, 88,100,112,120, 92,104,116,126,108,120,132,140},
      59, { 0,-1,-2}, {86,74,50}, {28,46,58}, 84},
    {{ 54, 62, 70, 78, 60, 68, 76, 86, 64, 72, 82, 92, 74, 84, 96,104},
      54, {-4,-5,-6}, {112,120,88}, { 8,18,26}, 46},
    {{110,118,126,134,116,124,132,142,122,130,140,148,128,140,150,158},
      54, { 2,-3, 1}, {62,40,28}, {44,62,78}, 76},
    {{ 92,102,112,124, 98,110,122,136,106,118,132,146,114,130,148,166},
      61, { 0,-2,-1}, {60,48,30}, {54,78,102}, 104},
    {{ 68, 74, 82, 90, 72, 80, 88, 98, 76, 86, 96,108, 84, 96,112,124},
      50, {-3,-4,-5}, {98,90,58}, {12,24,34}, 24},
    {{112,120,132,144,118,130,142,154,126,138,150,164,136,150,168,184},
      52, { 1,-1, 2}, {58,48,26}, {38,58,84}, 72},
};

static constexpr int8_t style_scales[8][4][7] = {
    {{0,3,5,7,10,12,15},{0,2,3,7,9,12,14},{0,3,5,6,10,12,15},{0,2,3,5,7,9,10}},
    {{0,2,4,5,7,9,10},{0,2,4,7,9,12,14},{0,2,4,6,7,9,11},{0,2,3,5,7,9,10}},
    {{0,2,3,5,7,9,10},{0,3,5,7,10,12,15},{0,2,4,7,9,12,14},{0,2,3,6,7,9,10}},
    {{0,2,3,5,7,8,11},{0,2,3,5,7,9,11},{0,1,3,5,7,8,11},{0,2,3,6,7,10,11}},
    {{0,1,3,5,7,8,10},{0,1,2,3,6,7,10},{0,2,3,5,7,8,11},{0,2,3,5,7,9,10}},
    {{0,1,3,4,6,8,10},{0,2,3,6,7,9,11},{0,1,4,5,7,8,11},{0,2,5,6,8,9,11}},
    {{0,2,4,7,9,12,14},{0,2,3,5,7,9,10},{0,2,4,6,7,9,11},{0,2,4,5,7,9,11}},
    {{0,2,4,7,9,12,16},{0,2,3,5,7,8,10},{0,2,4,5,7,9,11},{0,3,5,7,10,12,15}},
};

static constexpr uint8_t style_motifs[8][16] = {
    {0,0,2,0,3,0,1,0, 4,0,2,1,5,0,3,1},
    {0,2,4,1,3,5,2,4, 1,3,6,2,5,3,1,4},
    {0,3,1,4,2,5,0,3, 6,2,4,1,5,3,0,2},
    {0,0,3,1,0,4,2,0, 5,1,3,6,2,0,4,1},
    {0,1,4,2,0,5,3,1, 6,2,4,0,5,1,3,2},
    {0,5,1,6,2,4,0,3, 6,1,5,2,0,4,3,6},
    {0,1,0,2,1,3,2,4, 3,5,4,6,5,3,2,1},
    {0,2,4,6,1,3,5,2, 4,6,3,1,5,2,0,6},
};

static constexpr uint8_t style_lengths[8][4][3] = {
    {{16,12, 7},{16,15, 9},{16,13,11},{16,11, 7}},
    {{16,16, 8},{16,12,10},{16,15, 7},{16,13, 9}},
    {{16,11,13},{15, 9,14},{16,13, 7},{14,11,15}},
    {{16,15,11},{16,13, 9},{16,12, 7},{16,14,10}},
    {{16, 8, 6},{16,12, 7},{16,10, 5},{16, 7, 9}},
    {{15,11, 7},{16, 9,13},{14, 7,11},{16, 5, 9}},
    {{16,15,13},{15,16,11},{16,13,15},{14,15,16}},
    {{16, 8, 7},{16,12, 6},{16,10, 9},{16, 7, 5}},
};

void apply_style_composition(uint8_t bank, uint8_t program,
                             patch_record_t *record) {
    const uint8_t style = static_cast<uint8_t>(bank - 8u);
    const uint8_t quartet = program >> 2u;
    const uint8_t variant = program & 3u;
    const style_composition_t &composition = style_compositions[style];
    std::memcpy(record->scale, style_scales[style][quartet],
                sizeof(record->scale));
    for (uint8_t step = 0u; step < 16u; ++step) {
        uint8_t source = static_cast<uint8_t>((step + variant * 3u) & 15u);
        uint8_t degree = style_motifs[style][source];
        if ((step & 3u) == variant) degree = static_cast<uint8_t>(
            (degree + quartet + variant) % 7u);
        record->motif[step] = degree;
    }
    record->bpm = composition.bpm[program];
    record->swing_percent = static_cast<uint8_t>(composition.swing +
        ((style == 2u || style == 5u) ? variant : variant / 2u));
    static constexpr int8_t length_motion[4][3] = {
        {0,0,0}, {0,1,-1}, {0,-1,1}, {0,2,-2},
    };
    for (uint8_t lane = 0u; lane < 3u; ++lane) {
        int length = style_lengths[style][quartet][lane] +
                     length_motion[variant][lane];
        if (length < 1) length = 1;
        if (length > 16) length = 16;
        record->euclid_length[lane] = static_cast<uint8_t>(length);
        int density = composition.density[lane] +
            static_cast<int>(quartet) / 2 + (variant == 3u ? 1 : 0);
        if (density < -8) density = -8;
        if (density > 8) density = 8;
        if (lane == 0u) record->density_bias = static_cast<int8_t>(density);
        else if (lane == 1u) record->density_bias_pad = static_cast<int8_t>(density);
        else record->density_bias_lead = static_cast<int8_t>(density);
        record->gate_q5[lane] = composition.gate[lane];
        unsigned ratchet = composition.ratchet[lane] + quartet * 6u +
                           (variant == 3u ? 8u : 0u);
        if (ratchet > 200u) ratchet = 200u;
        record->ratchet_percent[lane] = static_cast<uint8_t>(ratchet);
    }
    record->low_balance = composition.low_balance;
    record->low_sensor = static_cast<uint8_t>(64u + style * 6u);
    record->low_variation = static_cast<uint8_t>(52u + quartet * 14u);

    patch_behavior_storage(*record, PATCH_BEND_PERCENT) =
        style == 4u || style == 5u ? 85u : 55u;
    patch_behavior_storage(*record, PATCH_RATCHET_PERCENT) =
        static_cast<uint8_t>(90u + quartet * 8u);
    patch_behavior_storage(*record, PATCH_AMP_DECAY_MOTION) =
        static_cast<uint8_t>(style == 3u ? 34u : 18u + quartet * 8u);
    patch_behavior_storage(*record, PATCH_AMP_SUSTAIN_MOTION) =
        static_cast<uint8_t>(style == 3u ? 46u : 16u + quartet * 7u);
    patch_behavior_storage(*record, PATCH_AMP_RELEASE_MOTION) =
        static_cast<uint8_t>(style == 0u || style == 3u ? 54u : 20u + quartet * 7u);
    patch_behavior_storage(*record, PATCH_CUTOFF_PERCENT) =
        static_cast<uint8_t>(82u + style * 6u);
    patch_behavior_storage(*record, PATCH_RESONANCE_PERCENT) =
        static_cast<uint8_t>(style == 4u || style == 5u ? 125u : 90u);
    patch_behavior_storage(*record, PATCH_MORPH_PERCENT) =
        static_cast<uint8_t>(style == 2u || style == 6u ? 120u : 88u);
    patch_behavior_storage(*record, PATCH_LFO_RATE_PERCENT) =
        static_cast<uint8_t>(style == 5u || style == 7u ? 125u : 82u);
    set_patch_octave_behavior(*record, style == 7u ? 2u : 1u,
                              style == 3u || style == 6u ? 48u : 0u);

    // Kit emphasis is still ordinary editable articulation data.
    if (style == 0u || style == 4u) {
        record->articulation[0].weight = 127u;
        record->articulation[1].weight = 108u;
        record->articulation[3].weight = 88u;
    } else if (style == 2u) {
        record->articulation[2].weight = 120u;
        record->articulation[5].algorithm_role =
            pack_articulation(PERC_RIM_WOOD, PERC_ROLE_FILL);
        record->articulation[5].weight = 106u;
    } else if (style == 3u) {
        record->articulation[0].weight = 92u;
        record->articulation[2].weight = 112u;
        record->articulation[3].weight = 32u;
    } else if (style == 5u) {
        record->articulation[3].weight = 118u;
        record->articulation[5].algorithm_role =
            pack_articulation(PERC_SHAKER_METAL, PERC_ROLE_FREE);
        record->articulation[5].weight = 102u;
    } else if (style == 7u) {
        record->articulation[3].weight = 116u;
        record->articulation[4].weight = 78u;
        record->articulation[5].algorithm_role =
            pack_articulation(PERC_SHAKER_METAL, PERC_ROLE_FILL);
    }
}

void build_default_patch(uint8_t id, patch_record_t *record) {
    const uint8_t bank = id / scene_count;
    const uint8_t program = id % scene_count;
    for (uint8_t lane = 0u; lane < 3u; ++lane) {
        record->lane[lane] = make_generated_lane_scene(id, lane);
    }
    std::memcpy(record->scale, scales[program], sizeof(record->scale));
    std::memcpy(record->motif, melodic_motifs[program], sizeof(record->motif));
    record->bpm = scene_bpm[program];
    std::memcpy(record->euclid_length, euclid_lengths[program],
                sizeof(record->euclid_length));
    record->swing_percent = 56u;
    record->gate_q5[0] = 70u;
    record->gate_q5[1] = 46u;
    record->gate_q5[2] = 29u;
    record->density_bias = 0;
    record->ratchet_percent[0] = 52u;
    record->ratchet_percent[1] = 92u;
    record->ratchet_percent[2] = 100u;
    build_default_articulation(static_cast<uint8_t>(id / scene_count),
                               program, record);
    record->density_bias_pad = 0;
    record->density_bias_lead = 0;
    if (bank == 3u) apply_percussive_profile(program, record);
    build_default_patch_behavior(id, record, true);
    if (bank >= 8u) apply_style_composition(bank, program, record);
}

bool load_patch_override(uint8_t id, patch_record_t *record) {
    size_t stored_size = 0u;
    const uint16_t key = preset_store_patch_key(id);
    if (!preset_store_load_prefix(key, record, sizeof(*record), &stored_size)) {
        return false;
    }
    // Records written before independent lane density used one shared value.
    // Copy it into newly appended fields so their musical balance is retained.
    if (stored_size <= offsetof(patch_record_t, density_bias_pad)) {
        record->density_bias_pad = record->density_bias;
    }
    if (stored_size <= offsetof(patch_record_t, density_bias_lead)) {
        record->density_bias_lead = record->density_bias;
    }
    // Schema 1-3 used these bytes as inactive bass/lead effects. Replace only
    // that inert data with authored schema-4 behavior; all audible values from
    // the saved patch remain intact.
    if (record->lane[bass_lane].chorus_mix != patch_behavior_marker) {
        build_default_patch_behavior(id, record, false);
    }
    return true;
}

void normalize_patch_voice_modes(patch_record_t *record) {
    for (scene_t &lane : record->lane) {
        if (lane.voice_mode <= 5u) continue;
        uint16_t index = static_cast<uint16_t>(
            (static_cast<uint16_t>(lane.voice_mode) * 10u + 127u) / 254u);
        lane.voice_mode = static_cast<uint8_t>(index > 5u ? 5u : index);
    }
    for (scene_t &lane : record->lane) {
        if (lane.voice_mode < 2u || lane.voice_mode == 3u) {
            lane.voice_mode = 2u;
        }
    }
}

void load_active_patch(uint8_t id) {
    build_default_patch(id, &active_patch);
    (void)load_patch_override(id, &active_patch);
    build_default_speech(id, &active_speech);
    (void)load_speech_override(id, &active_speech);
    for (uint8_t phrase = 0u; phrase < SAM_VOICE_PHRASE_COUNT; ++phrase) {
        active_speech.phrase[phrase][SAM_VOICE_PHRASE_LENGTH - 1u] = '\0';
    }
    normalize_patch_voice_modes(&active_patch);
    active_patch_id = id;
    active_patch_dirty = false;
}

void build_default_bank(uint8_t bank, bank_record_t *record) {
    static constexpr uint8_t colours[16][3] = {
        {100u, 62u, 8u}, {72u, 72u, 72u}, {8u, 92u, 28u},
        {100u, 34u, 2u}, {100u, 4u, 18u}, {8u, 30u, 100u},
        {0u, 88u, 92u}, {100u, 0u, 92u},
        {28u, 18u, 100u}, {100u, 54u, 4u}, {58u, 92u, 8u},
        {38u, 16u, 100u}, {70u, 100u, 0u}, {100u, 8u, 62u},
        {16u, 80u, 100u}, {100u, 84u, 4u},
    };
    sensor_route_t route = generated_sensor_route(bank);
    record->tempo_percent = bank_tempo_percent[bank];
    record->breath_max = route.breath_max;
    record->modulation_max = route.modulation_max;
    record->cutoff_range = static_cast<uint8_t>(route.cutoff_range);
    record->resonance_range = static_cast<uint8_t>(route.resonance_range);
    record->morph_range = static_cast<uint8_t>(route.morph_range);
    record->lfo_rate_range = static_cast<uint8_t>(route.lfo_rate_range);
    record->bend_percent = static_cast<uint8_t>(route.bend_scale * 100.0f);
    record->density_offset = route.density_offset;
    record->ratchet_percent = static_cast<uint8_t>(route.ratchet_scale * 100.0f);
    static constexpr uint8_t gate_percent[16] = {
        100u,100u,100u,62u,115u,185u,100u,78u,
        112u,96u,108u,160u,82u,72u,125u,76u,
    };
    static constexpr uint8_t lane_motion[16][3] = {
        {70,88,115},{70,88,115},{70,88,115},{70,88,115},
        {70,88,115},{70,88,115},{70,88,115},{70,88,115},
        {72,92,108},{68,84,110},{82,98,118},{64,92,104},
        {76,88,122},{82,96,128},{62,90,106},{70,84,126},
    };
    record->gate_percent = gate_percent[bank];
    std::memcpy(record->lane_motion_percent, lane_motion[bank],
                sizeof(record->lane_motion_percent));
    record->led_red = colours[bank][0];
    record->led_green = colours[bank][1];
    record->led_blue = colours[bank][2];
    static constexpr uint8_t low_modes[16] = {
        LOW_MODE_BASS, LOW_MODE_HYBRID, LOW_MODE_HYBRID,
        LOW_MODE_PERCUSSION, LOW_MODE_BASS, LOW_MODE_BASS,
        LOW_MODE_HYBRID, LOW_MODE_HYBRID,
        LOW_MODE_HYBRID, LOW_MODE_BASS, LOW_MODE_HYBRID,
        LOW_MODE_HYBRID, LOW_MODE_HYBRID, LOW_MODE_PERCUSSION,
        LOW_MODE_BASS, LOW_MODE_HYBRID,
    };
    static constexpr uint8_t low_balances[16] = {
        12u, 34u, 72u, 127u, 8u, 18u, 78u, 104u,
        58u, 18u, 84u, 46u, 76u, 104u, 24u, 72u,
    };
    record->low_mode = low_modes[bank];
    record->low_balance = low_balances[bank];
}

void load_active_bank(uint8_t bank) {
    build_default_bank(bank, &active_bank);
    (void)preset_store_load_prefix(preset_store_bank_key(bank),
                                   &active_bank, sizeof(active_bank), NULL);
    active_bank_id = bank;
    active_bank_dirty = false;
    status_rgb_set_bank_colour(bank, active_bank.led_red,
                               active_bank.led_green, active_bank.led_blue);
}

scene_t make_lane_scene(uint8_t id, uint8_t lane) {
    if (id == active_patch_id && lane < 3u) return active_patch.lane[lane];
    return make_generated_lane_scene(id, lane);
}

sensor_route_t sensor_route() {
    sensor_route_t result = {
        active_bank.breath_max, active_bank.modulation_max,
        static_cast<float>(active_bank.cutoff_range),
        static_cast<float>(active_bank.resonance_range),
        static_cast<float>(active_bank.morph_range),
        static_cast<float>(active_bank.lfo_rate_range),
        static_cast<float>(active_bank.bend_percent) / 100.0f,
        active_bank.density_offset,
        static_cast<float>(active_bank.ratchet_percent) / 100.0f,
    };
    const uint8_t breath_override = patch_behavior_storage(
        active_patch, PATCH_BREATH_OVERRIDE);
    if (breath_override != 0u) result.breath_max = breath_override;
    result.cutoff_range *= patch_behavior_storage(
        active_patch, PATCH_CUTOFF_PERCENT) / 100.0f;
    result.resonance_range *= patch_behavior_storage(
        active_patch, PATCH_RESONANCE_PERCENT) / 100.0f;
    result.morph_range *= patch_behavior_storage(
        active_patch, PATCH_MORPH_PERCENT) / 100.0f;
    result.lfo_rate_range *= patch_behavior_storage(
        active_patch, PATCH_LFO_RATE_PERCENT) / 100.0f;
    result.bend_scale *= patch_behavior_storage(
        active_patch, PATCH_BEND_PERCENT) / 100.0f;
    result.ratchet_scale *= patch_behavior_storage(
        active_patch, PATCH_RATCHET_PERCENT) / 100.0f;
    return result;
}

uint8_t editor_scene_bpm(uint8_t mode) {
    return mode == active_patch_id ? active_patch.bpm
                                   : scene_bpm[mode % scene_count];
}

uint8_t editor_bank_tempo(uint8_t bank) {
    return bank == active_bank_id ? active_bank.tempo_percent
                                  : bank_tempo_percent[bank];
}

uint8_t editor_euclid_length(uint8_t mode, uint8_t lane) {
    return mode == active_patch_id ? active_patch.euclid_length[lane]
        : euclid_lengths[mode % scene_count][lane];
}

uint8_t editor_motif_degree(uint8_t mode, uint8_t step) {
    return mode == active_patch_id ? active_patch.motif[step]
        : melodic_motifs[mode % scene_count][step];
}

int8_t editor_scale_offset(uint8_t mode, uint8_t degree) {
    return mode == active_patch_id ? active_patch.scale[degree]
        : scales[mode % scene_count][degree];
}

uint8_t editor_swing_percent(uint8_t mode) {
    return mode == active_patch_id ? active_patch.swing_percent : 56u;
}

int16_t scale_sample(int16_t sample, int32_t gain_q15) {
    // Halve the Q15 gain first and compensate in the post-shift. This safely
    // supports 4x gain with a fast 32-bit multiply in the 48 kHz render loop.
    int32_t scaled = (static_cast<int32_t>(sample) * (gain_q15 >> 1)) >> 14;
    if (scaled > 32767) scaled = 32767;
    if (scaled < -32768) scaled = -32768;
    return static_cast<int16_t>(scaled);
}

// One shared, non-inlined dry entry point avoids duplicating the same template
// specialization for bass and upper. It must stay in SRAM: running this large
// per-sample path from XIP flash produces audible cache-miss clicks.
__attribute__((noinline, noclone))
int16_t __not_in_flash_func(process_dry_engine)(PRA32_U_Synth<true> &target) {
    int16_t right = 0;
    return target.process(0, right);
}

void __not_in_flash_func(core1_entry)() {
    (void)flash_safe_execute_core_init();
    for (;;) {
        if (sam_voice_core1_service()) continue;
        if (upper_render_request != 0u) {
            upper_render_result = process_dry_engine(upper_engine);
            __dmb();
            upper_render_request = 0u;
            continue;
        }
        tight_loop_contents();
    }
}

template <bool bypass_fx>
void apply_scene_to(PRA32_U_Synth<bypass_fx> &target, const scene_t &scene) {
    target.control_change(OSC_1_WAVE, scene.osc1_wave);
    target.control_change(OSC_1_SHAPE, scene.osc1_shape);
    target.control_change(OSC_1_MORPH, scene.osc1_morph);
    target.control_change(MIXER_SUB_OSC, scene.sub_mix);
    target.control_change(OSC_2_WAVE, scene.osc2_wave);
    target.control_change(OSC_2_COARSE, scene.osc2_coarse);
    target.control_change(OSC_2_PITCH, scene.osc2_pitch);
    target.control_change(MIXER_OSC_MIX, scene.osc_mix);
    target.control_change(FILTER_CUTOFF, scene.cutoff);
    target.control_change(FILTER_RESO, scene.resonance);
    target.control_change(FILTER_EG_AMT, scene.filter_eg);
    target.control_change(FILTER_KEY_TRK, scene.filter_key_track);
    target.control_change(EG_ATTACK, scene.eg_attack);
    target.control_change(EG_DECAY, scene.eg_decay);
    target.control_change(EG_SUSTAIN, scene.eg_sustain);
    target.control_change(EG_RELEASE, scene.eg_release);
    target.control_change(EG_OSC_AMT, scene.eg_osc_amt);
    target.control_change(EG_OSC_DST, scene.eg_osc_dst);
    target.control_change(VOICE_MODE, scene.voice_mode);
    target.control_change(PORTAMENTO, scene.portamento);
    target.control_change(LFO_WAVE, scene.lfo_wave);
    target.control_change(LFO_RATE, scene.lfo_rate);
    target.control_change(LFO_DEPTH, scene.lfo_depth);
    target.control_change(LFO_FADE_TIME, scene.lfo_fade);
    target.control_change(LFO_OSC_AMT, scene.lfo_osc_amt);
    target.control_change(LFO_OSC_DST, scene.lfo_osc_dst);
    target.control_change(LFO_FILTER_AMT, scene.lfo_filter_amt);
    target.control_change(AMP_GAIN, scene.amp_gain);
    target.control_change(AMP_ATTACK, scene.amp_attack);
    target.control_change(AMP_DECAY, scene.amp_decay);
    target.control_change(AMP_SUSTAIN, scene.amp_sustain);
    target.control_change(AMP_RELEASE, scene.amp_release);
    target.control_change(FILTER_MODE, scene.filter_mode);
    target.control_change(EG_AMP_MOD, scene.eg_amp_mod);
    target.control_change(REL_EQ_DECAY, scene.release_equals_decay);
    target.control_change(P_BEND_RANGE, scene.pitch_bend_range);
    target.control_change(BTH_FILTER_AMT, scene.breath_filter_amt);
    target.control_change(BTH_AMP_MOD, scene.breath_amp_mod);
    target.control_change(EG_VEL_SENS, scene.eg_velocity);
    target.control_change(AMP_VEL_SENS, scene.amp_velocity);
    target.control_change(VOICE_ASGN_MODE, scene.voice_assignment);
    if constexpr (!bypass_fx) {
        target.control_change(CHORUS_MIX, scene.chorus_mix);
        target.control_change(CHORUS_RATE, scene.chorus_rate);
        target.control_change(CHORUS_DEPTH, scene.chorus_depth);
        target.control_change(DELAY_FEEDBACK, scene.delay_feedback);
        target.control_change(DELAY_TIME, scene.delay_time);
        target.control_change(DELAY_MODE, scene.delay_mode);
    }
    target.control_change(MODULATION, 0);
    target.control_change(BTH_CONTROLLER, 0);
    target.pitch_bend(0, 64);
}

void apply_scenes(uint8_t index) {
    apply_scene_to(bass_engine, make_lane_scene(index, bass_lane));
    apply_scene_to(middle_engine, make_lane_scene(index, middle_lane));
    apply_scene_to(upper_engine, make_lane_scene(index, upper_lane));
}

void apply_active_lane(uint8_t lane) {
    if (lane == bass_lane) apply_scene_to(bass_engine, active_patch.lane[lane]);
    else if (lane == middle_lane) {
        apply_scene_to(middle_engine, active_patch.lane[lane]);
    } else if (lane == upper_lane) {
        apply_scene_to(upper_engine, active_patch.lane[lane]);
    }
}

void lane_control_change(uint8_t lane, uint8_t control, uint8_t value) {
    if (lane == bass_lane) bass_engine.control_change(control, value);
    else if (lane == middle_lane) middle_engine.control_change(control, value);
    else upper_engine.control_change(control, value);
}

void set_live_amp_envelope(float expression, float proximity,
                           float release_source) {
    const uint8_t decay_motion = patch_behavior_storage(
        active_patch, PATCH_AMP_DECAY_MOTION);
    const uint8_t sustain_motion = patch_behavior_storage(
        active_patch, PATCH_AMP_SUSTAIN_MOTION);
    const uint8_t release_motion = patch_behavior_storage(
        active_patch, PATCH_AMP_RELEASE_MOTION);
    for (uint8_t lane = 0u; lane < SYNTH_LANE_COUNT; ++lane) {
        const scene_t &base = active_patch.lane[lane];
        lane_control_change(lane, AMP_DECAY, clamp_u7(
            base.amp_decay + static_cast<int>(expression * decay_motion)));
        lane_control_change(lane, AMP_SUSTAIN, clamp_u7(
            base.amp_sustain + static_cast<int>(proximity * sustain_motion)));
        lane_control_change(lane, AMP_RELEASE, clamp_u7(
            base.amp_release + static_cast<int>(release_source * release_motion)));
    }
}

void all_engines_control_change(uint8_t control, uint8_t value) {
    bass_engine.control_change(control, value);
    middle_engine.control_change(control, value);
    upper_engine.control_change(control, value);
}

void all_engines_pitch_bend(uint8_t lsb, uint8_t msb) {
    bass_engine.pitch_bend(lsb, msb);
    middle_engine.pitch_bend(lsb, msb);
    upper_engine.pitch_bend(lsb, msb);
}

void lane_note_on(uint8_t lane, uint8_t note, uint8_t velocity) {
    if (lane == bass_lane) bass_engine.note_on(note, velocity);
    else if (lane == middle_lane) middle_engine.note_on(note, velocity);
    else upper_engine.note_on(note, velocity);
}

void lane_note_off(uint8_t lane, uint8_t note) {
    if (lane == bass_lane) bass_engine.note_off(note);
    else if (lane == middle_lane) middle_engine.note_off(note);
    else upper_engine.note_off(note);
}

struct low_articulation_t {
    int8_t note_offset;
    float gate_scale;
    float ratchet_scale;
};

uint8_t articulation_role_strength(uint8_t role, uint8_t step) {
    bool match = false;
    switch (role) {
        case PERC_ROLE_ANCHOR: match = (step & 7u) == 0u; break;
        case PERC_ROLE_BACKBEAT: match = (step & 3u) == 2u; break;
        case PERC_ROLE_OFFBEAT: match = (step & 1u) != 0u; break;
        case PERC_ROLE_FILL: match = (step & 15u) >= 12u; break;
        default: return 2u;
    }
    return match ? 4u : 1u;
}

void restore_low_base(const scene_t &base, float expression,
                      float proximity, float sensor_depth) {
    const float depth = sensor_depth / 127.0f;
    const sensor_route_t route = sensor_route();
    lane_control_change(bass_lane, MIXER_SUB_OSC, base.sub_mix);
    lane_control_change(bass_lane, OSC_2_WAVE, base.osc2_wave);
    lane_control_change(bass_lane, MIXER_OSC_MIX, base.osc_mix);
    lane_control_change(bass_lane, FILTER_MODE, base.filter_mode);
    lane_control_change(bass_lane, FILTER_CUTOFF, clamp_u7(
        base.cutoff + static_cast<int>(proximity * route.cutoff_range * depth)));
    lane_control_change(bass_lane, FILTER_RESO, clamp_u7(
        base.resonance + static_cast<int>(expression *
            route.resonance_range * depth)));
    lane_control_change(bass_lane, FILTER_EG_AMT, base.filter_eg);
    lane_control_change(bass_lane, EG_OSC_AMT, base.eg_osc_amt);
    lane_control_change(bass_lane, EG_OSC_DST, base.eg_osc_dst);
    lane_control_change(bass_lane, AMP_GAIN, base.amp_gain);
    lane_control_change(bass_lane, AMP_DECAY, base.amp_decay);
    lane_control_change(bass_lane, AMP_SUSTAIN, base.amp_sustain);
    lane_control_change(bass_lane, AMP_RELEASE, base.amp_release);
}

low_articulation_t prepare_low_articulation(uint8_t mode, uint8_t step,
                                             uint32_t random,
                                             float expression,
                                             float proximity, float spread) {
    const scene_t base = make_lane_scene(mode, bass_lane);
    uint8_t low_mode = active_patch.low_mode == LOW_MODE_INHERIT
        ? active_bank.low_mode : active_patch.low_mode;
    uint8_t balance = active_patch.low_mode == LOW_MODE_INHERIT
        ? active_bank.low_balance : active_patch.low_balance;
    const float sensor_depth = active_patch.low_sensor / 127.0f;
    const float variation = active_patch.low_variation / 127.0f;
    low_articulation_t result = {0, 1.0f, 1.0f};

    bool percussion = low_mode == LOW_MODE_PERCUSSION;
    if (low_mode == LOW_MODE_HYBRID) {
        float gesture = (expression * 0.55f + spread * 0.30f +
                         proximity * 0.15f) - 0.5f;
        int chance = static_cast<int>(balance) + static_cast<int>(
            gesture * variation * sensor_depth * 96.0f);
        if ((step & 7u) == 0u) chance -= 36;
        chance = chance < 0 ? 0 : chance > 127 ? 127 : chance;
        percussion = static_cast<int>(random & 127u) < chance;
    }
    if (low_mode == LOW_MODE_BASS || !percussion) {
        restore_low_base(base, expression, proximity,
                         static_cast<float>(active_patch.low_sensor));
        return result;
    }

    uint16_t weighted[6];
    uint16_t total = 0u;
    for (unsigned index = 0u; index < 6u; ++index) {
        const articulation_slot_t &slot = active_patch.articulation[index];
        uint8_t role = static_cast<uint8_t>((slot.algorithm_role >> 3u) & 7u);
        uint16_t amount = static_cast<uint16_t>(slot.weight) *
                          articulation_role_strength(role, step);
        weighted[index] = amount;
        total = static_cast<uint16_t>(total + amount);
    }
    if (total == 0u) {
        restore_low_base(base, expression, proximity,
                         static_cast<float>(active_patch.low_sensor));
        return result;
    }
    uint16_t selection = static_cast<uint16_t>((random >> 7u) % total);
    unsigned selected = 0u;
    while (selected < 5u && selection >= weighted[selected]) {
        selection = static_cast<uint16_t>(selection - weighted[selected]);
        ++selected;
    }
    const articulation_slot_t &slot = active_patch.articulation[selected];
    const uint8_t algorithm = slot.algorithm_role & 7u;
    int random_motion = static_cast<int>((random >> 19u) & 31u) - 15;
    random_motion = static_cast<int>(
        static_cast<float>(random_motion) * variation);
    const int sensor_tone = static_cast<int>((proximity * 18.0f +
        expression * 14.0f) * sensor_depth);
    uint8_t cutoff = clamp_u7(slot.tone + sensor_tone + random_motion);
    uint8_t resonance = clamp_u7(18 + static_cast<int>(
        slot.transient * 0.35f + expression * 18.0f * sensor_depth));
    uint8_t noise = clamp_u7(slot.noise + static_cast<int>(
        expression * 18.0f * sensor_depth) + random_motion / 2);
    uint8_t sub_mix = static_cast<uint8_t>(127u - noise);
    uint8_t osc_mix = clamp_u7(18 + static_cast<int>(noise * 0.78f));
    uint8_t filter_mode = 0u;
    uint8_t decay = clamp_u7(slot.decay + static_cast<int>(
        spread * 20.0f * sensor_depth) + random_motion / 2);
    uint8_t release = clamp_u7(10 + static_cast<int>(decay * 0.48f));
    uint8_t pitch_env = clamp_u7(slot.transient + static_cast<int>(
        expression * 20.0f * sensor_depth));
    int tune = static_cast<int>(slot.tune) - 24;

    switch (algorithm) {
        case PERC_KICK:
            sub_mix = adjust_u7(sub_mix, 34); osc_mix = adjust_u7(osc_mix, -26);
            cutoff = adjust_u7(cutoff, -18); resonance = adjust_u7(resonance, -8);
            result.gate_scale = 0.72f + decay / 320.0f;
            break;
        case PERC_TOM:
            sub_mix = adjust_u7(sub_mix, 18); cutoff = adjust_u7(cutoff, -8);
            result.gate_scale = 0.64f + decay / 260.0f;
            break;
        case PERC_SNARE:
            filter_mode = 64u; osc_mix = adjust_u7(osc_mix, 18);
            cutoff = adjust_u7(cutoff, 8); result.gate_scale = 0.62f;
            break;
        case PERC_CLOSED_HAT:
            filter_mode = 64u; sub_mix = 0u; osc_mix = 116u;
            cutoff = adjust_u7(cutoff, 18); decay = adjust_u7(decay, -24);
            release = adjust_u7(release, -14); pitch_env /= 2u;
            result.gate_scale = 0.30f;
            break;
        case PERC_OPEN_HAT:
            filter_mode = 64u; sub_mix = 0u; osc_mix = 120u;
            cutoff = adjust_u7(cutoff, 20); release = adjust_u7(release, 12);
            pitch_env /= 2u; result.gate_scale = 0.72f;
            break;
        case PERC_CLAP:
            filter_mode = 64u; sub_mix = 0u; osc_mix = 112u;
            cutoff = adjust_u7(cutoff, 10); result.gate_scale = 0.48f;
            result.ratchet_scale = 1.35f;
            break;
        case PERC_RIM_WOOD:
            noise = adjust_u7(noise, -34); sub_mix = 70u;
            resonance = adjust_u7(resonance, 42); decay = adjust_u7(decay, -20);
            release = adjust_u7(release, -16); result.gate_scale = 0.34f;
            break;
        default:
            filter_mode = 64u; sub_mix = 0u; osc_mix = 124u;
            cutoff = adjust_u7(cutoff, 22); resonance = adjust_u7(resonance, 18);
            result.gate_scale = 0.38f;
            break;
    }

    result.note_offset = static_cast<int8_t>(tune);
    result.ratchet_scale *= 0.20f + slot.ratchet / 64.0f;
    lane_control_change(bass_lane, MIXER_SUB_OSC, sub_mix);
    lane_control_change(bass_lane, OSC_2_WAVE, 100u);
    lane_control_change(bass_lane, MIXER_OSC_MIX, osc_mix);
    lane_control_change(bass_lane, FILTER_MODE, filter_mode);
    lane_control_change(bass_lane, FILTER_CUTOFF, cutoff);
    lane_control_change(bass_lane, FILTER_RESO, resonance);
    lane_control_change(bass_lane, FILTER_EG_AMT, pitch_env / 2u);
    lane_control_change(bass_lane, EG_OSC_AMT, pitch_env);
    lane_control_change(bass_lane, EG_OSC_DST, 127u);
    lane_control_change(bass_lane, AMP_GAIN, clamp_u7(
        base.amp_gain + (static_cast<int>(slot.level) - 64) / 2));
    lane_control_change(bass_lane, AMP_DECAY, decay);
    lane_control_change(bass_lane, AMP_SUSTAIN, 0u);
    lane_control_change(bass_lane, AMP_RELEASE, release);
    return result;
}

uint8_t midi_channel_for_lane(const synth_t *synth, unsigned lane) {
    return synth->midi_multichannel ? static_cast<uint8_t>(lane % 3u) : 0u;
}

template <typename Function>
void for_each_midi_channel(const synth_t *synth, Function function) {
    unsigned count = synth->midi_multichannel ? 3u : 1u;
    for (unsigned channel = 0; channel < count; ++channel) {
        function(static_cast<uint8_t>(channel));
    }
}

bool start_note(synth_t *synth, uint8_t lane, uint8_t note, uint8_t velocity,
                uint8_t midi_channel,
                uint32_t duration_frames) {
    if (lane >= SYNTH_LANE_COUNT) return false;
    unsigned first_slot = lane;
    unsigned end_slot = lane + 1u;
#if defined(PICO_RP2350)
    // Bass stays intentionally monophonic; melodic lanes get independent
    // note slots and therefore do not retrigger/steal each other's envelopes.
    if (lane == 1u) { first_slot = 1u; end_slot = 5u; }
    else if (lane == 2u) { first_slot = 5u; end_slot = 8u; }
#endif

    // Repeated pitches tie within their originating lane, preserving the
    // envelope even when single-channel MIDI gives both melodic lanes channel 1.
    for (unsigned slot = first_slot; slot < end_slot; ++slot) {
        synth_note_t &voice = synth->notes[slot];
        if (!voice.active || voice.lane != lane || voice.note != note ||
            voice.midi_channel != midi_channel) continue;
        if (voice.frames_left < duration_frames) {
            voice.frames_left = duration_frames;
        }
        return true;
    }

    synth_note_t *voice = nullptr;
    for (unsigned slot = first_slot; slot < end_slot; ++slot) {
        if (!synth->notes[slot].active) { voice = &synth->notes[slot]; break; }
    }
    // If every slot is occupied, leave existing notes intact.  Dropping the
    // newest trigger is preferable to an audible voice steal/click.
    if (voice == nullptr) return false;
    if (voice->midi_note_off_pending) {
        midi_note_off(voice->midi_channel, voice->note);
        voice->midi_note_off_pending = 0u;
    }
    voice->note = note;
    voice->midi_channel = midi_channel;
    voice->lane = lane;
    voice->frames_left = duration_frames;
    voice->active = 1u;
    lane_note_on(lane, note, velocity);
    midi_note_on(midi_channel, note, velocity);
    ++synth->note_on_counter;
    return true;
}

void update_next_ratchet_frame(synth_t *synth) {
    synth->next_ratchet_frame = 0u;
    for (unsigned i = 0; i < SYNTH_RATCHET_EVENT_COUNT; ++i) {
        const synth_ratchet_event_t &event = synth->ratchets[i];
        if (!event.active) continue;
        if (synth->next_ratchet_frame == 0u ||
            static_cast<int32_t>(event.due_frame - synth->next_ratchet_frame) < 0) {
            synth->next_ratchet_frame = event.due_frame;
        }
    }
}

void schedule_ratchet(synth_t *synth, uint8_t lane,
                      uint8_t note, uint8_t velocity,
                      uint8_t midi_channel,
                      uint32_t due_frame, uint32_t duration_frames) {
    for (unsigned i = 0; i < SYNTH_RATCHET_EVENT_COUNT; ++i) {
        synth_ratchet_event_t &event = synth->ratchets[i];
        if (event.active) continue;
        event.due_frame = due_frame;
        event.duration_frames = duration_frames;
        event.note = note;
        event.velocity = velocity;
        event.midi_channel = midi_channel;
        event.lane = lane;
        event.active = 1u;
        if (synth->next_ratchet_frame == 0u ||
            static_cast<int32_t>(due_frame - synth->next_ratchet_frame) < 0) {
            synth->next_ratchet_frame = due_frame;
        }
        return;
    }
}

void fire_ratchet(synth_t *synth, const synth_ratchet_event_t &event) {
    if (event.lane >= SYNTH_LANE_COUNT) return;
    for (unsigned slot = 0u; slot < SYNTH_VOICE_COUNT; ++slot) {
        synth_note_t &voice = synth->notes[slot];
        if (!voice.active || voice.lane != event.lane ||
            voice.note != event.note ||
            voice.midi_channel != event.midi_channel) continue;
        lane_note_off(event.lane, event.note);
        lane_note_on(event.lane, event.note, event.velocity);
        midi_note_off(event.midi_channel, event.note);
        midi_note_on(event.midi_channel, event.note, event.velocity);
        voice.frames_left = event.duration_frames;
        ++synth->ratchet_fire_counter;
        return;
    }

    if (start_note(synth, event.lane, event.note, event.velocity,
                   event.midi_channel, event.duration_frames)) {
        ++synth->ratchet_fire_counter;
    }
}

void service_ratchets(synth_t *synth, uint32_t current_frame) {
    if (synth->next_ratchet_frame == 0u ||
        static_cast<int32_t>(current_frame - synth->next_ratchet_frame) < 0) {
        return;
    }
    for (unsigned i = 0; i < SYNTH_RATCHET_EVENT_COUNT; ++i) {
        synth_ratchet_event_t &event = synth->ratchets[i];
        if (!event.active ||
            static_cast<int32_t>(current_frame - event.due_frame) < 0) continue;
        fire_ratchet(synth, event);
        event.active = 0u;
    }
    update_next_ratchet_frame(synth);
}

void service_note_durations(synth_t *synth) {
    for (unsigned i = 0; i < SYNTH_VOICE_COUNT; ++i) {
        synth_note_t &note = synth->notes[i];
        if (!note.active) continue;
        if (note.frames_left > 0u) --note.frames_left;
        if (note.frames_left == 0u) {
            lane_note_off(note.lane, note.note);
            note.active = 0;
            note.midi_note_off_pending = 1;
        }
    }
}

}  // namespace

extern "C" {

static void select_program(synth_t *synth, uint8_t bank, uint8_t program);

void synth_init(synth_t *synth) {
    (void)preset_store_init();
    sam_voice_init();
    std::memset(synth, 0, sizeof(*synth));
    synth->random_state = 0x51c10a7du;
    synth->rhythm_seed = 0x6d2b79f5u;
    synth->root_note = 45;
    synth->program_index = 0;
    synth->bank_index = 0;
    synth->pitch_bend_enabled = 0;
    synth->sensitivity_index = 4;
    synth->volume_index = 7;
    synth->duration_index = 3;
    synth->speech_last_bar = 0xffu;
    synth->speech_last_phrase = 0xffu;
    global_record_t globals = default_global_record();
    (void)preset_store_load_prefix(PRESET_STORE_GLOBAL_KEY, &globals,
                                   sizeof(globals), NULL);
    apply_global_record(synth, globals);
    status_rgb_set_brightness(editor_led_brightness);
    synth->master_gain_q15 = volume_gain_q15[synth->volume_index];
    bass_engine.initialize();
    middle_engine.initialize();
    upper_engine.initialize();
    load_active_bank(0u);
    load_active_patch(0u);
    apply_scenes(0u);
    multicore_launch_core1(core1_entry);
}

void synth_startup_chord(synth_t *synth) {
    constexpr uint8_t chord[] = {45, 52, 57};
    for (unsigned lane = 0; lane < sizeof(chord); ++lane) {
        (void)start_note(synth, static_cast<uint8_t>(lane), chord[lane], 72,
                         midi_channel_for_lane(synth, lane),
                         SYNTH_SAMPLE_RATE * 2u);
    }
}

void synth_midi_chord_clear(synth_t *synth) {
    std::memset(synth->chord_held_notes, 0, sizeof(synth->chord_held_notes));
    synth->chord_pitch_classes = 0u;
    synth->chord_pending_pitch_classes = 0u;
    synth->chord_capture_pending = 0u;
}

void synth_midi_chord_release(synth_t *synth) {
    std::memset(synth->chord_held_notes, 0, sizeof(synth->chord_held_notes));
}

void synth_midi_chord_note(synth_t *synth, uint8_t note, bool pressed) {
    if (note > 127u) return;
    const uint32_t bit = 1u << (note & 31u);
    uint32_t &word = synth->chord_held_notes[note >> 5u];
    if (!pressed) {
        word &= ~bit;
        return;
    }

    const bool chord_was_released =
        (synth->chord_held_notes[0] | synth->chord_held_notes[1] |
         synth->chord_held_notes[2] | synth->chord_held_notes[3]) == 0u;
    const uint16_t pitch_class = static_cast<uint16_t>(1u << (note % 12u));
    if (chord_was_released) {
        synth->chord_pending_pitch_classes = pitch_class;
        synth->chord_capture_deadline = synth->transport_frame +
            SYNTH_SAMPLE_RATE / 20u;
        synth->chord_capture_pending = 1u;
        if (synth->chord_pitch_classes == 0u) {
            synth->chord_pitch_classes = pitch_class;
        }
    } else {
        uint16_t &target = synth->chord_capture_pending != 0u
            ? synth->chord_pending_pitch_classes
            : synth->chord_pitch_classes;
        if ((target & pitch_class) != 0u || pitch_class_count(target) < 7u) {
            target = static_cast<uint16_t>(target | pitch_class);
        }
    }
    word |= bit;
}

void synth_set_sensitivity_step(synth_t *synth, int direction) {
    int index = static_cast<int>(synth->sensitivity_index) + direction;
    constexpr int maximum = 7;
    if (index < 0) index = 0;
    if (index > maximum) index = maximum;
    synth->sensitivity_index = static_cast<uint8_t>(index);
    if (synth->raw_mode) return;
    for_each_midi_channel(synth, [index](uint8_t channel) {
        midi_control_change(channel, 20,
                            static_cast<uint8_t>(index * 127 / 7));
    });
}

void synth_set_volume_step(synth_t *synth, int direction) {
    int index = static_cast<int>(synth->volume_index) + direction;
    constexpr int maximum = 11;
    if (index < 0) index = 0;
    if (index > maximum) index = maximum;
    synth->volume_index = static_cast<uint8_t>(index);
    synth->master_gain_q15 = volume_gain_q15[index];
    if (synth->raw_mode) return;
    for_each_midi_channel(synth, [index](uint8_t channel) {
        midi_control_change(channel, 7,
                            static_cast<uint8_t>(index * 127 / 11));
    });
}

void synth_set_duration_step(synth_t *synth, int direction) {
    int index = static_cast<int>(synth->duration_index) + direction;
    constexpr int maximum = 7;
    if (index < 0) index = 0;
    if (index > maximum) index = maximum;
    synth->duration_index = static_cast<uint8_t>(index);
    if (synth->raw_mode) return;
    for_each_midi_channel(synth, [index](uint8_t channel) {
        midi_control_change(channel, 21,
                            static_cast<uint8_t>(index * 127 / 7));
    });
}

void synth_set_root_step(synth_t *synth, int direction) {
    int note = static_cast<int>(synth->root_note) + direction;
    if (note < 24) note = 24;
    if (note > 72) note = 72;
    synth->root_note = static_cast<uint8_t>(note);
    if (synth->raw_mode) return;
    for_each_midi_channel(synth, [synth](uint8_t channel) {
        midi_control_change(channel, 22, synth->root_note);
    });
}

void synth_set_program_step(synth_t *synth, int direction) {
    int program = static_cast<int>(synth->program_index) + direction;
    if (program < 0) program = scene_count - 1;
    if (program >= scene_count) program = 0;
    select_program(synth, synth->bank_index, static_cast<uint8_t>(program));
}

void synth_set_bank_step(synth_t *synth, int direction) {
    int bank = static_cast<int>(synth->bank_index) + direction;
    if (bank < 0) bank = bank_count - 1;
    if (bank >= bank_count) bank = 0;
    select_program(synth, static_cast<uint8_t>(bank), synth->program_index);
}

uint8_t synth_program_id(const synth_t *synth) {
    return static_cast<uint8_t>(synth->bank_index * scene_count +
                                synth->program_index);
}

void synth_sync_midi(const synth_t *synth) {
    if (synth->raw_mode) return;
    uint8_t id = synth_program_id(synth);
    for_each_midi_channel(synth, [synth, id](uint8_t channel) {
        midi_control_change(channel, 0, synth->bank_index);
        midi_control_change(channel, 32, 0);
        midi_program_change(channel, synth->program_index);
        if (id < 128u) midi_control_change(channel, 23, id);
        midi_control_change(channel, 24,
                            synth->midi_multichannel ? 127u : 0u);
        midi_control_change(channel, 20,
            static_cast<uint8_t>(synth->sensitivity_index * 127u / 7u));
        midi_control_change(channel, 7,
            static_cast<uint8_t>(synth->volume_index * 127u / 11u));
        midi_control_change(channel, 21,
            static_cast<uint8_t>(synth->duration_index * 127u / 7u));
        midi_control_change(channel, 22, synth->root_note);
        midi_pitch_bend(channel, 8192u);
    });
}

static void select_program(synth_t *synth, uint8_t bank, uint8_t program) {
    synth->bank_index = static_cast<uint8_t>(bank % bank_count);
    synth->program_index = static_cast<uint8_t>(program % scene_count);
    uint8_t id = synth_program_id(synth);
    load_active_bank(synth->bank_index);
    load_active_patch(id);
    synth->sequence_step = 0;
    synth->sequence_bar = 0;
    synth->rhythm_seed ^= 0x9e3779b9u + id;
    synth->next_step_frame = synth->transport_frame;
    for (unsigned i = 0; i < SYNTH_RATCHET_EVENT_COUNT; ++i) {
        synth->ratchets[i].active = 0u;
    }
    synth->next_ratchet_frame = 0u;
    all_engines_control_change(ALL_NOTES_OFF, 0);
    for (unsigned i = 0; i < SYNTH_VOICE_COUNT; ++i) {
        if (synth->notes[i].active || synth->notes[i].midi_note_off_pending) {
            if (!synth->raw_mode) {
                midi_note_off(synth->notes[i].midi_channel,
                              synth->notes[i].note);
            }
        }
        synth->notes[i].active = 0u;
        synth->notes[i].midi_note_off_pending = 0u;
    }
    apply_scenes(id);
    if (!synth->raw_mode) for_each_midi_channel(synth, [synth, id](uint8_t channel) {
        midi_control_change(channel, 123, 0);
        midi_control_change(channel, 0, synth->bank_index);
        midi_control_change(channel, 32, 0);
        midi_program_change(channel, synth->program_index);
        if (id < 128u) midi_control_change(channel, 23, id);
    });
}

void synth_next_mode(synth_t *synth) {
    select_program(synth, synth->bank_index,
                   static_cast<uint8_t>(synth->program_index + 1u));
}

void synth_next_bank(synth_t *synth) {
    select_program(synth, static_cast<uint8_t>(synth->bank_index + 1u),
                   synth->program_index);
}

void synth_toggle_pitch_bend(synth_t *synth) {
    synth->pitch_bend_enabled ^= 1u;
    if (!synth->pitch_bend_enabled) {
        all_engines_pitch_bend(0, 64);
        if (!synth->raw_mode) for_each_midi_channel(synth, [](uint8_t channel) {
            midi_pitch_bend(channel, 8192u);
        });
    }
}

void synth_toggle_midi_mode(synth_t *synth) {
    all_engines_control_change(ALL_NOTES_OFF, 0);
    if (!synth->raw_mode) for_each_midi_channel(synth, [](uint8_t channel) {
        midi_control_change(channel, 123, 0);
    });
    for (unsigned i = 0; i < SYNTH_VOICE_COUNT; ++i) {
        synth_note_t &note = synth->notes[i];
        if (note.active || note.midi_note_off_pending) {
            if (!synth->raw_mode) midi_note_off(note.midi_channel, note.note);
        }
        note.active = 0u;
        note.midi_note_off_pending = 0u;
    }
    for (unsigned i = 0; i < SYNTH_RATCHET_EVENT_COUNT; ++i) {
        synth->ratchets[i].active = 0u;
    }
    synth->next_ratchet_frame = 0u;
    synth->midi_multichannel ^= 1u;
    synth_sync_midi(synth);
}

void synth_toggle_raw_mode(synth_t *synth) {
    bool enabling = synth->raw_mode == 0u;
    if (enabling) midi_discard_pending();
    all_engines_control_change(ALL_NOTES_OFF, 0);
    for (unsigned i = 0; i < SYNTH_VOICE_COUNT; ++i) {
        synth_note_t &note = synth->notes[i];
        if (enabling && (note.active || note.midi_note_off_pending)) {
            midi_note_off(note.midi_channel, note.note);
        }
        note.active = 0u;
        note.midi_note_off_pending = 0u;
    }
    for (unsigned i = 0; i < SYNTH_RATCHET_EVENT_COUNT; ++i) {
        synth->ratchets[i].active = 0u;
    }
    synth->next_ratchet_frame = 0u;
    if (enabling) {
        for_each_midi_channel(synth, [](uint8_t channel) {
            midi_control_change(channel, 123, 0);
        });
    }
    synth->raw_mode = enabling ? 1u : 0u;
    synth->raw_previous_status = 0u;
    synth->raw_unchanged_frames = 0u;
    synth->next_step_frame = synth->transport_frame;
    if (!enabling) synth_sync_midi(synth);
}

float synth_sensitivity(const synth_t *synth) {
    return sensitivity_values[synth->sensitivity_index];
}

static void process_sensor_state(synth_t *synth, const sensor_stats_t *stats,
                                 bool fresh_window) {
    if (fresh_window && stats->range_fault) return;

    if (fresh_window) {
    ++synth->sensor_window_counter;
    synth->last_sensor_stats = *stats;
    synth->sensor_stats_valid = 1u;
    const float mean_us = std::fmax(stats->mean_us, 1.0f);
    const float variation = stats->standard_deviation / mean_us;

    if (synth->adaptive_mean_low_us == 0.0f) {
        synth->adaptive_mean_low_us = mean_us;
        synth->adaptive_mean_high_us = mean_us;
        synth->adaptive_variation_low = variation;
        synth->adaptive_variation_high = variation + 0.003f;
    } else if (synth->sensor_calibration_learning != 0u) {
        // Fast expansion learns a new gesture; configurable contraction
        // recovers from temporary spikes without chasing every window.
        const float recovery_rate = static_cast<float>(
            synth->sensor_calibration_recovery_tenths_percent) * 0.001f;
        float low_rate = mean_us < synth->adaptive_mean_low_us ?
            0.18f : recovery_rate;
        float high_rate = mean_us > synth->adaptive_mean_high_us ?
            0.18f : recovery_rate;
        synth->adaptive_mean_low_us +=
            (mean_us - synth->adaptive_mean_low_us) * low_rate;
        synth->adaptive_mean_high_us +=
            (mean_us - synth->adaptive_mean_high_us) * high_rate;

        const float variation_recovery_rate = recovery_rate * 1.2f;
        float variation_low_rate = variation < synth->adaptive_variation_low ?
            0.14f : variation_recovery_rate;
        float variation_high_rate = variation > synth->adaptive_variation_high ?
            0.14f : variation_recovery_rate;
        synth->adaptive_variation_low +=
            (variation - synth->adaptive_variation_low) * variation_low_rate;
        synth->adaptive_variation_high +=
            (variation - synth->adaptive_variation_high) * variation_high_rate;
    }

    float mean_mid = (synth->adaptive_mean_low_us +
                      synth->adaptive_mean_high_us) * 0.5f;
    float mean_span = std::fmax(
        synth->adaptive_mean_high_us - synth->adaptive_mean_low_us,
        std::fmax(mean_mid * 0.04f, 3000.0f));
    // Shorter pulse intervals represent stronger contact. Centering a stable
    // source at 0.5 leaves room in both directions instead of reading as off.
    float adaptive_pressure = clampf(
        0.5f + (mean_mid - mean_us) / mean_span, 0.0f, 1.0f);
    float fixed_pressure = 1.0f - clampf(mean_us / 100000.0f, 0.0f, 1.0f);
    const float adaptive_mix =
        static_cast<float>(synth->sensor_adaptive_percent) * 0.01f;
    float raw_proximity = adaptive_pressure * adaptive_mix +
        fixed_pressure * (1.0f - adaptive_mix);

    float variation_span = std::fmax(
        synth->adaptive_variation_high - synth->adaptive_variation_low,
        0.003f);
    float adaptive_expression = clampf(
        (variation - synth->adaptive_variation_low) / variation_span,
        0.0f, 1.0f);
    float fixed_expression = clampf(
        variation * static_cast<float>(synth->sensor_variation_gain_tenths) *
            0.1f,
        0.0f, 1.0f);
    float raw_expression = adaptive_expression * adaptive_mix +
        fixed_expression * (1.0f - adaptive_mix);

    float pressure_step = std::fabs(raw_proximity - synth->sensor_proximity);
    float mean_step = synth->previous_mean_us > 0.0f ?
        std::fabs(synth->previous_mean_us - mean_us) / mean_us : 0.0f;
    float raw_transient = clampf(
        (pressure_step * 2.8f + mean_step * 8.0f) *
            static_cast<float>(synth->sensor_transient_gain_percent) * 0.01f,
        0.0f, 1.0f);
    synth->sensor_proximity +=
        (raw_proximity - synth->sensor_proximity) *
            static_cast<float>(synth->sensor_pressure_smoothing) * 0.01f;
    synth->sensor_expression +=
        (raw_expression - synth->sensor_expression) *
            static_cast<float>(synth->sensor_expression_smoothing) * 0.01f;
    // Fast attack and decay preserve accents without creating chatter.
    float transient_rate = raw_transient > synth->sensor_transient ? 0.48f :
        static_cast<float>(synth->sensor_transient_decay_percent) * 0.01f;
    synth->sensor_transient +=
        (raw_transient - synth->sensor_transient) * transient_rate;
    }
    if (synth->raw_mode) return;

    const uint8_t mode = synth_program_id(synth);
    const bool pitch_bend_enabled = synth->pitch_bend_enabled != 0u;
    const scene_t &scene = active_patch.lane[middle_lane];
    const sensor_route_t route = sensor_route();

    if (fresh_window) {
        float bend_target = 0.0f;
        if (synth->previous_mean_us > 0.0f) {
            bend_target = (synth->previous_mean_us - stats->mean_us) /
                std::fmax(synth->previous_mean_us, 1.0f);
            bend_target = clampf(bend_target * 4.0f, -1.0f, 1.0f);
        }
        synth->previous_mean_us = stats->mean_us;
        synth->sensor_bend += (bend_target - synth->sensor_bend) * 0.18f;
    }

    float breath_source = synth->sensor_proximity * 0.68f +
                          synth->sensor_expression * 0.24f +
                          synth->sensor_transient * 0.08f;
    float modulation_source = synth->sensor_expression * 0.76f +
                              synth->sensor_transient * 0.24f;
    uint8_t breath = clamp_u7(static_cast<int>(
        breath_source * static_cast<float>(route.breath_max)));
    uint8_t modulation = clamp_u7(static_cast<int>(
        modulation_source * static_cast<float>(route.modulation_max)));
    all_engines_control_change(BTH_CONTROLLER, breath);
    all_engines_control_change(MODULATION, modulation);

    const uint8_t decay_motion = patch_behavior_storage(
        active_patch, PATCH_AMP_DECAY_MOTION);
    const uint8_t sustain_motion = patch_behavior_storage(
        active_patch, PATCH_AMP_SUSTAIN_MOTION);
    const uint8_t release_motion = patch_behavior_storage(
        active_patch, PATCH_AMP_RELEASE_MOTION);
    if (decay_motion != 0u || sustain_motion != 0u || release_motion != 0u) {
        const float release_source = synth->sensor_proximity * 0.65f +
            clampf(static_cast<float>(stats->delta_us) / 60000.0f,
                   0.0f, 1.0f) * 0.35f;
        set_live_amp_envelope(synth->sensor_expression,
                              synth->sensor_proximity, release_source);
    }
    float base_bend_span = scene.pitch_bend_range >= 24u ? 4095.0f : 1536.0f;
    int bend_span = static_cast<int>(base_bend_span * route.bend_scale);
    int bend_value = 8192 + static_cast<int>(
        synth->sensor_bend * static_cast<float>(bend_span));
    if (bend_value < 0) bend_value = 0;
    if (bend_value > 16383) bend_value = 16383;
    if (pitch_bend_enabled) {
        all_engines_pitch_bend(static_cast<uint8_t>(bend_value & 127),
                               static_cast<uint8_t>(bend_value >> 7));
        for_each_midi_channel(synth, [bend_value](uint8_t channel) {
            midi_pitch_bend(channel, static_cast<uint16_t>(bend_value));
        });
    } else {
        all_engines_pitch_bend(0, 64);
    }

    const uint32_t now = synth->transport_frame;
    if (synth->next_step_frame == 0u) synth->next_step_frame = now;
    if (static_cast<int32_t>(now - synth->next_step_frame) < 0) return;

    // If sensor data arrives late, preserve the global pulse by skipping over
    // missed steps rather than moving the clock to the arrival time.
    uint32_t step_frames = rhythmic_step_frames(mode, synth->sequence_step);
    while (static_cast<int32_t>(now - synth->next_step_frame) >=
           static_cast<int32_t>(step_frames)) {
        synth->next_step_frame += step_frames;
        advance_sequence(synth);
        step_frames = rhythmic_step_frames(mode, synth->sequence_step);
    }

    const uint8_t step = synth->sequence_step;
    synth->next_step_frame += step_frames;
    const uint8_t bar = synth->sequence_bar;

    // Quantize sensor statistics into new Euclidean parameters at each bar.
    // This lets interaction reshape the groove without scrambling it mid-bar.
    if (step == 0u || synth->euclid_steps[0] == 0u) {
        for (unsigned lane = 0; lane < 3u; ++lane) {
            synth->euclid_steps[lane] = editor_euclid_length(
                mode, static_cast<uint8_t>(lane));
        }
        int density = static_cast<int>(synth->sensitivity_index) / 2 +
                      route.density_offset;
        const int lane_density[3] = {
            density + active_patch.density_bias,
            density + active_patch.density_bias_pad,
            density + active_patch.density_bias_lead,
        };
        int bass_pulses = 3 + lane_density[0] + static_cast<int>(
            synth->sensor_proximity * 3.0f + synth->sensor_transient * 2.0f);
        int melody_pulses = 2 + lane_density[1] + static_cast<int>(
            synth->sensor_expression * 3.0f + synth->sensor_proximity * 2.0f);
        int upper_pulses = 1 + lane_density[2] + static_cast<int>(
            synth->sensor_expression * 2.0f + synth->sensor_proximity * 1.5f +
            synth->sensor_transient * 2.0f);
        if (stats->trigger || synth->sensor_transient > 0.55f) ++melody_pulses;
        int requested[3] = {bass_pulses, melody_pulses, upper_pulses};
        for (unsigned lane = 0; lane < 3u; ++lane) {
            int maximum = static_cast<int>(synth->euclid_steps[lane]) - 1;
            if (requested[lane] < 1) requested[lane] = 1;
            if (requested[lane] > maximum) requested[lane] = maximum;
            synth->euclid_pulses[lane] = static_cast<uint8_t>(requested[lane]);
        }
        synth->euclid_rotation[0] = static_cast<uint8_t>(bar & 3u);
        synth->euclid_rotation[1] = static_cast<uint8_t>(
            ((stats->delta_us / 2500u) + (synth->rhythm_seed & 7u)) %
            synth->euclid_steps[1]);
        synth->euclid_rotation[2] = static_cast<uint8_t>(
            (static_cast<uint32_t>(synth->sensor_expression * 23.0f) +
             ((synth->rhythm_seed >> 8) & 15u)) % synth->euclid_steps[2]);
    }

    const uint8_t phrase_tick = static_cast<uint8_t>(bar * 16u + step);
    advance_sequence(synth);

    bool lane_hit[3];
    bool any_hit = false;
    for (unsigned lane = 0; lane < 3u; ++lane) {
        lane_hit[lane] = euclidean_hit(phrase_tick, synth->euclid_pulses[lane],
                                      synth->euclid_steps[lane],
                                      synth->euclid_rotation[lane]);
        any_hit = any_hit || lane_hit[lane];
    }
    if (step == 0u && synth->speech_last_bar != bar &&
        active_speech.enabled != 0u && !sam_voice_active()) {
        float sensor = synth->sensor_proximity * 0.45f +
            synth->sensor_expression * 0.35f + synth->sensor_transient * 0.20f;
        int chance = active_speech.density + static_cast<int>(
            sensor * active_speech.sensor_influence);
        if (chance > 127) chance = 127;
        uint32_t voice_random = random_u32(synth);
        if ((voice_random & 127u) < static_cast<uint32_t>(chance)) {
            uint8_t phrase = static_cast<uint8_t>(
                (voice_random >> 8u) % SAM_VOICE_PHRASE_COUNT);
            for (uint8_t attempt = 0u; attempt < SAM_VOICE_PHRASE_COUNT;
                 ++attempt) {
                if (phrase != synth->speech_last_phrase &&
                    active_speech.phrase[phrase][0] != '\0') break;
                phrase = static_cast<uint8_t>(
                    (phrase + 1u) % SAM_VOICE_PHRASE_COUNT);
            }
            sam_voice_character_t performance = {
                active_speech.enabled, active_speech.level,
                active_speech.speed, active_speech.pitch,
                active_speech.mouth, active_speech.throat,
            };
            uint32_t motion_random = random_u32(synth);
            if ((motion_random & 127u) < active_speech.motion_chance &&
                active_speech.motion_amount != 0u) {
                const int amount = active_speech.motion_amount;
                auto moved = [amount](uint8_t value, uint8_t random_byte,
                                      int scale, int minimum) {
                    int offset = (static_cast<int>(random_byte) - 128) *
                        amount * scale / (128 * 2);
                    int result = static_cast<int>(value) + offset;
                    if (result < minimum) result = minimum;
                    if (result > 255) result = 255;
                    return static_cast<uint8_t>(result);
                };
                performance.speed = moved(active_speech.speed,
                    static_cast<uint8_t>(motion_random >> 8u), 1, 1);
                performance.pitch = moved(active_speech.pitch,
                    static_cast<uint8_t>(motion_random >> 16u), 2, 0);
                performance.mouth = moved(active_speech.mouth,
                    static_cast<uint8_t>(motion_random >> 24u), 2, 0);
                performance.throat = moved(active_speech.throat,
                    static_cast<uint8_t>(random_u32(synth) >> 24u), 2, 0);
            }
            if (sam_voice_request(active_speech.phrase[phrase], &performance)) {
                synth->speech_last_bar = bar;
                synth->speech_last_phrase = phrase;
                // USB-only CC 119 lets the editor display the exact phrase
                // without adding speech metadata to the musician-facing DIN
                // MIDI stream.
                midi_usb_control_change(0u, 119u, phrase);
            }
        }
    }
    if (!any_hit) return;

    const uint8_t octave_span = patch_proximity_octaves(active_patch);
    int octave = static_cast<int>(synth->sensor_proximity *
                                  (static_cast<float>(octave_span) + 0.999f));
    if (octave > octave_span) octave = octave_span;
    const uint8_t expression_threshold =
        patch_expression_octave_threshold(active_patch);
    if (expression_threshold != 0u && octave < 2 &&
        synth->sensor_expression * 63.0f >= expression_threshold) {
        ++octave;
    }
    unsigned motif_step = (step + bar * 3u) & 15u;
    unsigned melody_degree = editor_motif_degree(
        mode, static_cast<uint8_t>(motif_step));

    float sustained_motion = synth->sensor_proximity * 0.55f +
                             synth->sensor_expression * 0.35f +
                             synth->sensor_transient * 0.10f;
    float animated_motion = synth->sensor_expression * 0.72f +
                            synth->sensor_transient * 0.28f;
    uint8_t cutoff = clamp_u7(scene.cutoff + static_cast<int>(
        sustained_motion * route.cutoff_range));
    uint8_t resonance = clamp_u7(scene.resonance + static_cast<int>(
        animated_motion * route.resonance_range));
    for (uint8_t lane = 0u; lane < SYNTH_LANE_COUNT; ++lane) {
        const scene_t lane_scene = make_lane_scene(mode, lane);
        float motion_scale = static_cast<float>(
            active_bank.lane_motion_percent[lane]) / 100.0f;
        lane_control_change(lane, FILTER_CUTOFF, clamp_u7(
            lane_scene.cutoff + static_cast<int>(
                sustained_motion * route.cutoff_range * motion_scale)));
        lane_control_change(lane, FILTER_RESO, clamp_u7(
            lane_scene.resonance + static_cast<int>(
                animated_motion * route.resonance_range * motion_scale)));
        lane_control_change(lane, OSC_1_MORPH, clamp_u7(
            lane_scene.osc1_morph + static_cast<int>(
                (animated_motion - 0.5f) * route.morph_range *
                motion_scale)));
        lane_control_change(lane, LFO_RATE, clamp_u7(
            lane_scene.lfo_rate + static_cast<int>(
                animated_motion * route.lfo_rate_range * motion_scale)));
    }

    const unsigned degrees[3] = {
        static_cast<unsigned>((bar + step / 4u) % 4u),
        melody_degree,
        static_cast<unsigned>((melody_degree + 2u) % 7u),
    };
    const int octave_offsets[3] = {octave > 0 ? octave - 1 : 0, octave, octave + 1};
    float lane_gates[3] = {
        static_cast<float>(active_patch.gate_q5[0]) / 32.0f,
        static_cast<float>(active_patch.gate_q5[1]) / 32.0f,
        static_cast<float>(active_patch.gate_q5[2]) / 32.0f,
    };
    float gate_scale = static_cast<float>(active_bank.gate_percent) / 100.0f;
    for (float &gate : lane_gates) gate *= gate_scale;
    const int lane_velocity[3] = {0, 8, -4};
    float spread = clampf(static_cast<float>(stats->delta_us) / 60000.0f,
                          0.0f, 1.0f);
    float ratchet_drive = synth->sensor_expression * 0.34f +
        synth->sensor_proximity * 0.18f + synth->sensor_transient * 0.26f +
        spread * 0.12f +
        static_cast<float>(synth->sensitivity_index) * 0.025f;
    ratchet_drive = clampf(ratchet_drive * route.ratchet_scale, 0.0f, 1.0f);
    uint32_t random = random_u32(synth);
    for (unsigned lane = 0; lane < 3u; ++lane) {
        if (!lane_hit[lane]) continue;
        int note_value = static_cast<int>(synth->root_note) +
            editor_scale_offset(mode, static_cast<uint8_t>(degrees[lane])) +
            octave_offsets[lane] * 12;
        low_articulation_t articulation = {0, 1.0f, 1.0f};
        if (lane == bass_lane) {
            articulation = prepare_low_articulation(
                mode, phrase_tick, random_u32(synth), synth->sensor_expression,
                synth->sensor_proximity, spread);
            note_value += articulation.note_offset;
        }
        note_value = constrain_note_to_chord(
            note_value, synth->chord_pitch_classes);
        if (note_value < 24) note_value = 24;
        if (note_value > 108) note_value = 108;
        float gate_steps = lane_gates[lane] *
            duration_multipliers[synth->duration_index] * articulation.gate_scale;
        gate_steps = clampf(gate_steps, 0.25f, 4.0f);
        uint32_t duration_frames = static_cast<uint32_t>(
            gate_steps * static_cast<float>(step_frames));
        int velocity_value = 38 + lane_velocity[lane] + static_cast<int>(
            synth->sensor_proximity * 28.0f +
            synth->sensor_expression * 24.0f +
            synth->sensor_transient * 20.0f);
        if ((step & 3u) == 0u) velocity_value += 8;
        if (stats->trigger || synth->sensor_transient > 0.55f) velocity_value += 8;
        velocity_value += static_cast<int>((random >> (lane * 3u)) & 7u) - 3;
        uint8_t note = static_cast<uint8_t>(note_value);
        uint8_t velocity = clamp_u7(velocity_value);
        uint8_t midi_channel = midi_channel_for_lane(synth, lane);
        bool started = start_note(synth, static_cast<uint8_t>(lane), note,
                                  velocity, midi_channel,
                                  duration_frames);

        // Bass ratchets are rarer; melody and upper harmony respond more
        // strongly. Counts range from two to four subdivisions of this step.
        const float lane_response[3] = {
            static_cast<float>(active_patch.ratchet_percent[0]) / 100.0f,
            static_cast<float>(active_patch.ratchet_percent[1]) / 100.0f,
            static_cast<float>(active_patch.ratchet_percent[2]) / 100.0f,
        };
        uint32_t lane_random = random_u32(synth);
        unsigned chance = static_cast<unsigned>(
            clampf(ratchet_drive * lane_response[lane] *
                   articulation.ratchet_scale * 112.0f, 0.0f, 112.0f));
        if (!started || (lane_random & 127u) >= chance) continue;
        unsigned maximum_count = 2u +
            static_cast<unsigned>(ratchet_drive * 2.99f);
        if (maximum_count > 4u) maximum_count = 4u;
        unsigned ratchet_count = 2u +
            static_cast<unsigned>((lane_random >> 8) % (maximum_count - 1u));
        uint32_t spacing = step_frames / ratchet_count;
        uint32_t ratchet_duration = (spacing * 3u) / 4u;
        for (unsigned repeat = 1u; repeat < ratchet_count; ++repeat) {
            int accent = velocity_value - static_cast<int>(repeat * 3u);
            schedule_ratchet(synth, static_cast<uint8_t>(lane), note,
                             clamp_u7(accent), midi_channel,
                             now + spacing * repeat, ratchet_duration);
        }
    }

    for_each_midi_channel(synth, [cutoff, resonance, modulation, breath]
                          (uint8_t channel) {
        midi_control_change(channel, 74, cutoff);
        midi_control_change(channel, 71, resonance);
        midi_control_change(channel, 1, modulation);
        midi_control_change(channel, 2, breath);
    });
}

void synth_sensor_window(synth_t *synth, const sensor_stats_t *stats) {
    process_sensor_state(synth, stats, true);
    synth->sensor_next_hold_frame = synth->transport_frame +
        (SYNTH_SAMPLE_RATE / 50u);
}

void synth_sensor_tick(synth_t *synth, bool input_active) {
    if (synth->sensor_stats_valid == 0u || synth->raw_mode) return;
    uint32_t now = synth->transport_frame;
    if (static_cast<int32_t>(now - synth->sensor_next_hold_frame) < 0) return;
    synth->sensor_next_hold_frame = now + (SYNTH_SAMPLE_RATE / 50u);

    if (!input_active) {
        // Individual edges distinguish a slow plant from an open circuit. A
        // disconnected input stops after the edge-age timeout even if its last
        // completed statistics window was recent.
        synth->sensor_proximity = 0.0f;
        synth->sensor_expression = 0.0f;
        synth->sensor_transient = 0.0f;
        synth->sensor_bend = 0.0f;
        synth->next_step_frame = now;
        return;
    }

    sensor_stats_t held = synth->last_sensor_stats;
    held.trigger = false;
    process_sensor_state(synth, &held, false);
}

void __not_in_flash_func(synth_render)(synth_t *synth,
                                      uint32_t *stereo_frames,
                                      uint32_t frame_count) {
    if (synth->raw_mode) {
        bool captured = raw_capture_copy_latest(stereo_frames, frame_count);
        for (uint32_t frame = 0; frame < frame_count; ++frame) {
            uint32_t status = stereo_frames[frame];
            if (captured && status != synth->raw_previous_status) {
                synth->raw_unchanged_frames = 0u;
            } else if (captured) {
                ++synth->raw_unchanged_frames;
            }
            synth->raw_previous_status = status;

            // GPIO0_STATUS bit 17 is INFROMPAD. This is the exact field read
            // by the original 20 kHz DMA capture and renderer at 0x10002810.
            int16_t raw = captured && synth->raw_unchanged_frames <= 1200u
                ? ((status & 0x00020000u) ? -8000 : 8000)
                : 0;
            int16_t sample = scale_sample(raw, synth->master_gain_q15);
            stereo_frames[frame] =
                (static_cast<uint32_t>(static_cast<uint16_t>(sample)) << 16) |
                static_cast<uint16_t>(sample);
        }
        synth->transport_frame += frame_count;
        return;
    }
    for (uint32_t frame = 0; frame < frame_count; ++frame) {
        service_ratchets(synth, synth->transport_frame + frame);
        service_note_durations(synth);
        const bool speech_rendering = sam_voice_render_busy();
        if (upper_render_disabled == 0u && !speech_rendering) {
            upper_render_request = 1u;
            __dmb();
        }
        int16_t bass = process_dry_engine(bass_engine);
#if defined(PICO_RP2350)
        // SAM occupies core 1 for the duration of a phrase.  Keep the lead
        // audible on RP2350 by rendering it locally during speech; otherwise
        // use the secondary core as before to minimize core-0 load.
        int16_t upper = speech_rendering
            ? process_dry_engine(upper_engine)
            : upper_render_result;
#else
        int16_t upper = upper_render_result;
#endif
        // Paraphonic/polyphonic lead rendering processes four oscillators on
        // core 1 and needs a wider bounded completion window than mono mode.
        unsigned wait_budget = 2048u;
        while (upper_render_disabled == 0u && !speech_rendering &&
               upper_render_request != 0u && wait_budget-- != 0u) {
            tight_loop_contents();
        }
        if (!speech_rendering && upper_render_request != 0u) {
            // Never let a failed secondary render freeze controls and LEDs.
            // Mute the lead for this boot and continue with bass + pad.
            upper_render_disabled = 1u;
            upper_render_request = 0u;
            upper_render_result = 0;
        }
        __dmb();
#if !defined(PICO_RP2350)
        upper = upper_render_result;
#endif
        int16_t dry_input;
#if defined(PICO_RP2350)
        dry_input = upper_render_disabled != 0u
            ? bass
            : static_cast<int16_t>((static_cast<int32_t>(bass) + upper) / 2);
#else
        dry_input = upper_render_disabled != 0u || speech_rendering ? bass :
            static_cast<int16_t>((static_cast<int32_t>(bass) + upper) / 2);
#endif
        int32_t mixed = static_cast<int32_t>(dry_input) +
                        static_cast<int32_t>(sam_voice_next_sample());
        if (mixed > 32767) mixed = 32767;
        if (mixed < -32768) mixed = -32768;
        dry_input = static_cast<int16_t>(mixed);
        int16_t right = 0;
        int16_t left = middle_engine.process(dry_input, right);
        left = scale_sample(left, synth->master_gain_q15);
        right = scale_sample(right, synth->master_gain_q15);
        stereo_frames[frame] = (static_cast<uint32_t>(static_cast<uint16_t>(left)) << 16) |
            static_cast<uint16_t>(right);
    }
    int16_t visual_envelope = bass_engine.get_amp_envelope_output();
    int16_t middle_envelope = middle_engine.get_amp_envelope_output();
    if (middle_envelope > visual_envelope) visual_envelope = middle_envelope;
    int16_t upper_envelope = upper_engine.get_amp_envelope_output();
    if (upper_envelope > visual_envelope) visual_envelope = upper_envelope;
    synth->visual_amp_envelope = visual_envelope;
    synth->visual_lfo = middle_engine.get_lfo_output();
    synth->transport_frame += frame_count;
}

bool synth_editor_select(synth_t *synth, uint16_t patch_id) {
    if (patch_id >= scene_count * bank_count) return false;
    select_program(synth, static_cast<uint8_t>(patch_id / scene_count),
                   static_cast<uint8_t>(patch_id % scene_count));
    return true;
}

static bool editor_get_patch(uint16_t target, uint8_t lane, uint8_t parameter,
                             bool defaults_only, uint16_t *value) {
    if (target >= scene_count * bank_count) return false;
    if (lane == SYNTH_EDITOR_SPEECH_LANE) {
        if (parameter >= SYNTH_EDITOR_SPEECH_PARAMETER_COUNT) return false;
        sam_voice_patch_t temporary_speech;
        const sam_voice_patch_t *speech = &active_speech;
        if (defaults_only || target != active_patch_id) {
            build_default_speech(static_cast<uint8_t>(target), &temporary_speech);
            if (!defaults_only) {
                (void)load_speech_override(static_cast<uint8_t>(target),
                                           &temporary_speech);
            }
            speech = &temporary_speech;
        }
        const uint8_t values[SYNTH_EDITOR_SPEECH_PARAMETER_COUNT] = {
            speech->enabled, speech->level, speech->speed, speech->pitch,
            speech->mouth, speech->throat, speech->density,
            speech->sensor_influence, speech->motion_chance,
            speech->motion_amount,
        };
        *value = values[parameter];
        return true;
    }
    patch_record_t temporary;
    const patch_record_t *record = &active_patch;
    if (defaults_only || target != active_patch_id) {
        build_default_patch(static_cast<uint8_t>(target), &temporary);
        if (!defaults_only) {
            (void)load_patch_override(static_cast<uint8_t>(target), &temporary);
        }
        normalize_patch_voice_modes(&temporary);
        record = &temporary;
    }
    if (lane < 3u) {
        if (parameter >= sizeof(scene_t)) return false;
        // Schema 1-3 editors read all 47 lane bytes. Bass/Lead FX storage is
        // patch behavior in schema 4, so preserve compatibility without
        // exposing or leaking the packed representation.
        if (lane != middle_lane && parameter >= 41u) {
            *value = 0u;
            return true;
        }
        *value = reinterpret_cast<const uint8_t *>(&record->lane[lane])[parameter];
        return true;
    }
    if (lane != 3u) return false;
    if (parameter < 7u) {
        *value = static_cast<uint16_t>(record->scale[parameter] + 24);
    } else if (parameter < 23u) {
        *value = record->motif[parameter - 7u];
    } else if (parameter == 23u) {
        *value = record->bpm;
    } else if (parameter < 27u) {
        *value = record->euclid_length[parameter - 24u];
    } else if (parameter == 27u) {
        *value = record->swing_percent;
    } else if (parameter < 31u) {
        *value = record->gate_q5[parameter - 28u];
    } else if (parameter == 31u) {
        *value = static_cast<uint16_t>(record->density_bias + 16);
    } else if (parameter < 35u) {
        *value = record->ratchet_percent[parameter - 32u];
    } else if (parameter == 35u) {
        *value = record->low_mode;
    } else if (parameter == 36u) {
        *value = record->low_balance;
    } else if (parameter == 37u) {
        *value = record->low_sensor;
    } else if (parameter == 38u) {
        *value = record->low_variation;
    } else if (parameter < SYNTH_EDITOR_PATCH_SHARED_COUNT) {
        if (parameter == 99u) {
            *value = static_cast<uint16_t>(record->density_bias_pad + 16);
            return true;
        }
        if (parameter == 100u) {
            *value = static_cast<uint16_t>(record->density_bias_lead + 16);
            return true;
        }
        if (parameter == PATCH_PROXIMITY_OCTAVES) {
            *value = patch_proximity_octaves(*record);
            return true;
        }
        if (parameter == PATCH_EXPRESSION_OCTAVE_THRESHOLD) {
            *value = patch_expression_octave_threshold(*record);
            return true;
        }
        if (parameter >= PATCH_BREATH_OVERRIDE) {
            *value = patch_behavior_storage(*record, parameter);
            return true;
        }
        const uint8_t relative = static_cast<uint8_t>(parameter - 39u);
        const articulation_slot_t &slot = record->articulation[relative / 10u];
        switch (relative % 10u) {
            case 0: *value = slot.algorithm_role & 7u; break;
            case 1: *value = (slot.algorithm_role >> 3u) & 7u; break;
            case 2: *value = slot.weight; break;
            case 3: *value = slot.level; break;
            case 4: *value = slot.tune; break;
            case 5: *value = slot.tone; break;
            case 6: *value = slot.noise; break;
            case 7: *value = slot.decay; break;
            case 8: *value = slot.transient; break;
            default: *value = slot.ratchet; break;
        }
    } else {
        return false;
    }
    return true;
}

static bool editor_get_bank(uint16_t target, uint8_t parameter,
                            bool defaults_only, uint16_t *value) {
    if (target >= bank_count || parameter >= SYNTH_EDITOR_BANK_PARAMETER_COUNT) {
        return false;
    }
    bank_record_t temporary;
    const bank_record_t *record = &active_bank;
    if (defaults_only || target != active_bank_id) {
        build_default_bank(static_cast<uint8_t>(target), &temporary);
        if (!defaults_only) {
            (void)preset_store_load_prefix(
                preset_store_bank_key(static_cast<uint8_t>(target)),
                &temporary, sizeof(temporary), NULL);
        }
        record = &temporary;
    }
    switch (parameter) {
        case 0: *value = record->tempo_percent; break;
        case 1: *value = record->breath_max; break;
        case 2: *value = record->modulation_max; break;
        case 3: *value = record->cutoff_range; break;
        case 4: *value = record->resonance_range; break;
        case 5: *value = record->morph_range; break;
        case 6: *value = record->lfo_rate_range; break;
        case 7: *value = record->bend_percent; break;
        case 8: *value = static_cast<uint16_t>(record->density_offset + 16); break;
        case 9: *value = record->ratchet_percent; break;
        case 10: *value = record->gate_percent; break;
        case 11: case 12: case 13:
            *value = record->lane_motion_percent[parameter - 11u]; break;
        case 14: *value = record->led_red; break;
        case 15: *value = record->led_green; break;
        case 16: *value = record->led_blue; break;
        case 17: *value = record->low_mode; break;
        default: *value = record->low_balance; break;
    }
    return true;
}

bool synth_editor_get(const synth_t *synth, uint8_t scope, uint16_t target,
                      uint8_t lane, uint8_t parameter, uint16_t *value) {
    if (value == NULL) return false;
    if (scope == SYNTH_EDITOR_SCOPE_PATCH ||
        scope == SYNTH_EDITOR_SCOPE_FACTORY_PATCH) {
        return editor_get_patch(target, lane, parameter,
            scope == SYNTH_EDITOR_SCOPE_FACTORY_PATCH, value);
    }
    if (scope == SYNTH_EDITOR_SCOPE_BANK ||
        scope == SYNTH_EDITOR_SCOPE_FACTORY_BANK) {
        return editor_get_bank(target, parameter,
            scope == SYNTH_EDITOR_SCOPE_FACTORY_BANK, value);
    }
    if (scope == SYNTH_EDITOR_SCOPE_GLOBAL) {
        if (parameter >= SYNTH_EDITOR_GLOBAL_PARAMETER_COUNT) return false;
        switch (parameter) {
            case 0: *value = synth->root_note; break;
            case 1: *value = synth->sensitivity_index; break;
            case 2: *value = synth->volume_index; break;
            case 3: *value = synth->duration_index; break;
            case 4: *value = synth->pitch_bend_enabled; break;
            case 5: *value = synth->midi_multichannel; break;
            case 6: *value = editor_led_brightness; break;
            case 7: *value = synth->sensor_window_size; break;
            case 8: *value = synth->sensor_minimum_interval_us; break;
            case 9: *value = synth->sensor_adaptive_percent; break;
            case 10: *value = synth->sensor_pressure_smoothing; break;
            case 11: *value = synth->sensor_expression_smoothing; break;
            case 12: *value = synth->sensor_variation_gain_tenths; break;
            case 13: *value = synth->sensor_transient_gain_percent; break;
            case 14: *value = synth->sensor_transient_decay_percent; break;
            case 15: *value = synth->sensor_activity_timeout_ms; break;
            case 16: *value = synth->sensor_calibration_learning; break;
            default:
                *value = synth->sensor_calibration_recovery_tenths_percent;
                break;
        }
        return true;
    }
    if (scope == SYNTH_EDITOR_SCOPE_SENSOR) {
        float reading;
        switch (parameter) {
            case 0: reading = synth->sensor_proximity; break;
            case 1: reading = synth->sensor_expression; break;
            case 2: reading = synth->sensor_transient; break;
            case 3: reading = synth->sensor_bend * 0.5f + 0.5f; break;
            default: return false;
        }
        *value = static_cast<uint16_t>(clampf(reading, 0.0f, 1.0f) * 1000.0f);
        return true;
    }
    return false;
}

bool synth_editor_set(synth_t *synth, uint8_t scope, uint16_t target,
                      uint8_t lane, uint8_t parameter, uint16_t value) {
    if (scope == SYNTH_EDITOR_SCOPE_PATCH) {
        if (target >= scene_count * bank_count) return false;
        if (target != active_patch_id && !synth_editor_select(synth, target)) {
            return false;
        }
        if (lane < 3u) {
            if (parameter >= sizeof(scene_t) || value > 127u) return false;
            // Old JSON/editor clients write the former dry-lane FX fields.
            // Acknowledge and ignore those writes so they cannot corrupt the
            // schema-4 patch behavior stored in the same bytes.
            if (lane != middle_lane && parameter >= 41u) return true;
            if (parameter == 18u && value != 2u && value != 4u && value != 5u) {
                return false;
            }
            const uint8_t previous = reinterpret_cast<const uint8_t *>(
                &active_patch.lane[lane])[parameter];
            if (parameter == 18u && previous != value) {
                for (unsigned index = 0u; index < SYNTH_VOICE_COUNT; ++index) {
                    synth_note_t &note = synth->notes[index];
                    if (note.lane != lane) continue;
                    if (!synth->raw_mode &&
                        (note.active || note.midi_note_off_pending)) {
                        midi_note_off(note.midi_channel, note.note);
                    }
                    note.active = 0u;
                    note.midi_note_off_pending = 0u;
                }
                for (unsigned index = 0u;
                     index < SYNTH_RATCHET_EVENT_COUNT; ++index) {
                    if (synth->ratchets[index].lane == lane) {
                        synth->ratchets[index].active = 0u;
                    }
                }
                update_next_ratchet_frame(synth);
            }
            reinterpret_cast<uint8_t *>(&active_patch.lane[lane])[parameter] =
                static_cast<uint8_t>(value);
            apply_active_lane(lane);
        } else if (lane == 3u) {
            if (parameter < 7u && value <= 48u) {
                active_patch.scale[parameter] = static_cast<int8_t>(value) - 24;
            } else if (parameter < 23u && value <= 6u) {
                active_patch.motif[parameter - 7u] = static_cast<uint8_t>(value);
            } else if (parameter == 23u && value >= 40u && value <= 240u) {
                active_patch.bpm = static_cast<uint8_t>(value);
            } else if (parameter < 27u && value >= 1u && value <= 16u) {
                active_patch.euclid_length[parameter - 24u] =
                    static_cast<uint8_t>(value);
            } else if (parameter == 27u && value >= 50u && value <= 75u) {
                active_patch.swing_percent = static_cast<uint8_t>(value);
            } else if (parameter < 31u && value >= 8u && value <= 128u) {
                active_patch.gate_q5[parameter - 28u] =
                    static_cast<uint8_t>(value);
            } else if (parameter == 31u && value >= 8u && value <= 24u) {
                active_patch.density_bias = static_cast<int8_t>(value) - 16;
            } else if (parameter < 35u &&
                       value <= 200u) {
                active_patch.ratchet_percent[parameter - 32u] =
                    static_cast<uint8_t>(value);
            } else if (parameter == 35u && value <= LOW_MODE_HYBRID) {
                active_patch.low_mode = static_cast<uint8_t>(value);
            } else if (parameter >= 36u && parameter <= 38u && value <= 127u) {
                uint8_t *fields[] = {&active_patch.low_balance,
                                     &active_patch.low_sensor,
                                     &active_patch.low_variation};
                *fields[parameter - 36u] = static_cast<uint8_t>(value);
            } else if (parameter == 99u && value >= 8u && value <= 24u) {
                active_patch.density_bias_pad = static_cast<int8_t>(value) - 16;
            } else if (parameter == 100u && value >= 8u && value <= 24u) {
                active_patch.density_bias_lead = static_cast<int8_t>(value) - 16;
            } else if (parameter == PATCH_PROXIMITY_OCTAVES && value <= 2u) {
                set_patch_octave_behavior(active_patch,
                    static_cast<uint8_t>(value),
                    patch_expression_octave_threshold(active_patch));
            } else if (parameter == PATCH_EXPRESSION_OCTAVE_THRESHOLD &&
                       value <= 63u) {
                set_patch_octave_behavior(active_patch,
                    patch_proximity_octaves(active_patch),
                    static_cast<uint8_t>(value));
            } else if (parameter >= PATCH_BREATH_OVERRIDE &&
                       parameter < SYNTH_EDITOR_PATCH_SHARED_COUNT) {
                const bool percentage = parameter == PATCH_BEND_PERCENT ||
                    parameter == PATCH_RATCHET_PERCENT ||
                    parameter >= PATCH_CUTOFF_PERCENT;
                if (value > (percentage ? 200u : 127u)) return false;
                patch_behavior_storage(active_patch, parameter) =
                    static_cast<uint8_t>(value);
                if (parameter >= PATCH_AMP_DECAY_MOTION &&
                    parameter <= PATCH_AMP_RELEASE_MOTION && value == 0u) {
                    for (uint8_t voice = 0u; voice < SYNTH_LANE_COUNT; ++voice) {
                        apply_active_lane(voice);
                    }
                }
            } else if (parameter >= 39u && parameter < 99u) {
                const uint8_t relative = static_cast<uint8_t>(parameter - 39u);
                articulation_slot_t &slot =
                    active_patch.articulation[relative / 10u];
                switch (relative % 10u) {
                    case 0:
                        if (value > PERC_SHAKER_METAL) return false;
                        slot.algorithm_role = static_cast<uint8_t>(
                            (slot.algorithm_role & 0x38u) | value); break;
                    case 1:
                        if (value > PERC_ROLE_FREE) return false;
                        slot.algorithm_role = static_cast<uint8_t>(
                            (slot.algorithm_role & 7u) | (value << 3u)); break;
                    case 2: if (value > 127u) return false;
                            slot.weight = static_cast<uint8_t>(value); break;
                    case 3: if (value > 127u) return false;
                            slot.level = static_cast<uint8_t>(value); break;
                    case 4: if (value > 48u) return false;
                            slot.tune = static_cast<uint8_t>(value); break;
                    case 5: if (value > 127u) return false;
                            slot.tone = static_cast<uint8_t>(value); break;
                    case 6: if (value > 127u) return false;
                            slot.noise = static_cast<uint8_t>(value); break;
                    case 7: if (value > 127u) return false;
                            slot.decay = static_cast<uint8_t>(value); break;
                    case 8: if (value > 127u) return false;
                            slot.transient = static_cast<uint8_t>(value); break;
                    default: if (value > 127u) return false;
                             slot.ratchet = static_cast<uint8_t>(value); break;
                }
            } else {
                return false;
            }
        } else if (lane == SYNTH_EDITOR_SPEECH_LANE) {
            if (parameter >= SYNTH_EDITOR_SPEECH_PARAMETER_COUNT ||
                value > 255u) return false;
            uint8_t *fields[SYNTH_EDITOR_SPEECH_PARAMETER_COUNT] = {
                &active_speech.enabled, &active_speech.level,
                &active_speech.speed, &active_speech.pitch,
                &active_speech.mouth, &active_speech.throat,
                &active_speech.density, &active_speech.sensor_influence,
                &active_speech.motion_chance, &active_speech.motion_amount,
            };
            if (parameter == 0u && value > 1u) return false;
            if ((parameter == 1u || parameter >= 6u) && value > 127u) {
                return false;
            }
            if (parameter == 2u && value == 0u) return false;
            *fields[parameter] = static_cast<uint8_t>(value);
        } else {
            return false;
        }
        active_patch_dirty = true;
        return true;
    }
    if (scope == SYNTH_EDITOR_SCOPE_BANK) {
        if (target >= bank_count ||
            parameter >= SYNTH_EDITOR_BANK_PARAMETER_COUNT) return false;
        if (target != active_bank_id) {
            if (!synth_editor_select(synth,
                    static_cast<uint8_t>(target * scene_count +
                                         synth->program_index))) return false;
        }
        uint8_t u8 = static_cast<uint8_t>(value > 255u ? 255u : value);
        switch (parameter) {
            case 0: if (value < 40u || value > 200u) return false;
                    active_bank.tempo_percent = u8; break;
            case 1: if (value > 127u) return false; active_bank.breath_max = u8; break;
            case 2: if (value > 127u) return false; active_bank.modulation_max = u8; break;
            case 3: if (value > 127u) return false; active_bank.cutoff_range = u8; break;
            case 4: if (value > 127u) return false; active_bank.resonance_range = u8; break;
            case 5: if (value > 127u) return false; active_bank.morph_range = u8; break;
            case 6: if (value > 127u) return false; active_bank.lfo_rate_range = u8; break;
            case 7: if (value > 200u) return false; active_bank.bend_percent = u8; break;
            case 8: if (value < 8u || value > 24u) return false;
                    active_bank.density_offset = static_cast<int8_t>(value) - 16; break;
            case 9: if (value > 200u) return false; active_bank.ratchet_percent = u8; break;
            case 10: if (value < 25u || value > 200u) return false;
                     active_bank.gate_percent = u8; break;
            case 11: case 12: case 13:
                     if (value > 200u) return false;
                     active_bank.lane_motion_percent[parameter - 11u] = u8; break;
            case 14: case 15: case 16:
                     if (value > 127u) return false;
                     if (parameter == 14u) active_bank.led_red = u8;
                     else if (parameter == 15u) active_bank.led_green = u8;
                     else active_bank.led_blue = u8;
                     status_rgb_set_bank_colour(active_bank_id,
                         active_bank.led_red, active_bank.led_green,
                         active_bank.led_blue); break;
            case 17: if (value < LOW_MODE_BASS || value > LOW_MODE_HYBRID) {
                         return false;
                     }
                     active_bank.low_mode = u8; break;
            case 18: if (value > 127u) return false;
                     active_bank.low_balance = u8; break;
            default: return false;
        }
        active_bank_dirty = true;
        return true;
    }
    if (scope == SYNTH_EDITOR_SCOPE_GLOBAL) {
        switch (parameter) {
            case 0: if (value < 24u || value > 72u) return false;
                    synth->root_note = static_cast<uint8_t>(value); break;
            case 1: if (value > 7u) return false;
                    synth->sensitivity_index = static_cast<uint8_t>(value); break;
            case 2: if (value > 11u) return false;
                    synth->volume_index = static_cast<uint8_t>(value);
                    synth->master_gain_q15 = volume_gain_q15[value]; break;
            case 3: if (value > 7u) return false;
                    synth->duration_index = static_cast<uint8_t>(value); break;
            case 4: if (value > 1u) return false;
                    if (synth->pitch_bend_enabled != value) synth_toggle_pitch_bend(synth);
                    break;
            case 5: if (value > 1u) return false;
                    if (synth->midi_multichannel != value) synth_toggle_midi_mode(synth);
                    break;
            case 6: if (value > 127u) return false;
                    editor_led_brightness = static_cast<uint8_t>(value);
                    status_rgb_set_brightness(editor_led_brightness); break;
            case 7: if (value < 4u || value > 24u) return false;
                    synth->sensor_window_size = static_cast<uint8_t>(value);
                    sensor_configure(synth->sensor_window_size,
                        synth->sensor_minimum_interval_us,
                        synth->sensor_activity_timeout_ms); break;
            case 8: if (value < 500u || value > 10000u) return false;
                    synth->sensor_minimum_interval_us = value;
                    sensor_configure(synth->sensor_window_size,
                        synth->sensor_minimum_interval_us,
                        synth->sensor_activity_timeout_ms); break;
            case 9: if (value > 100u) return false;
                    synth->sensor_adaptive_percent =
                        static_cast<uint8_t>(value); break;
            case 10: if (value < 1u || value > 100u) return false;
                     synth->sensor_pressure_smoothing =
                         static_cast<uint8_t>(value); break;
            case 11: if (value < 1u || value > 100u) return false;
                     synth->sensor_expression_smoothing =
                         static_cast<uint8_t>(value); break;
            case 12: if (value < 1u || value > 200u) return false;
                     synth->sensor_variation_gain_tenths =
                         static_cast<uint8_t>(value); break;
            case 13: if (value > 200u) return false;
                     synth->sensor_transient_gain_percent =
                         static_cast<uint8_t>(value); break;
            case 14: if (value < 1u || value > 100u) return false;
                     synth->sensor_transient_decay_percent =
                         static_cast<uint8_t>(value); break;
            case 15: if (value < 100u || value > 10000u) return false;
                     synth->sensor_activity_timeout_ms = value;
                     sensor_configure(synth->sensor_window_size,
                         synth->sensor_minimum_interval_us,
                         synth->sensor_activity_timeout_ms); break;
            case 16:
                     if (value > 2u) return false;
                     if (value == 2u) {
                         reset_sensor_calibration(synth);
                         synth->sensor_calibration_learning = 1u;
                     } else {
                         synth->sensor_calibration_learning =
                             static_cast<uint8_t>(value);
                     }
                     break;
            case 17: if (value < 1u || value > 50u) return false;
                     synth->sensor_calibration_recovery_tenths_percent =
                         static_cast<uint8_t>(value); break;
            default: return false;
        }
        globals_dirty = true;
        return true;
    }
    return false;
}

bool synth_editor_get_phrase(synth_t *synth, uint16_t target,
                             uint8_t phrase, char *text, size_t capacity) {
    (void)synth;
    constexpr uint16_t active_target = 0x3fffu;
    if ((target >= PRESET_STORE_PATCH_COUNT && target != active_target) ||
        phrase >= SAM_VOICE_PHRASE_COUNT || text == nullptr || capacity == 0u) {
        return false;
    }
    sam_voice_patch_t temporary_speech;
    const sam_voice_patch_t *speech = &active_speech;
    if (target != active_target && target != active_patch_id) {
        build_default_speech(static_cast<uint8_t>(target), &temporary_speech);
        (void)load_speech_override(static_cast<uint8_t>(target),
                                   &temporary_speech);
        speech = &temporary_speech;
    }
    std::strncpy(text, speech->phrase[phrase], capacity - 1u);
    text[capacity - 1u] = '\0';
    return true;
}

bool synth_editor_set_phrase_chunk(synth_t *synth, uint16_t target,
                                   uint8_t phrase, uint8_t offset,
                                   const uint8_t *text, uint8_t length,
                                   bool final_chunk) {
    if (target >= PRESET_STORE_PATCH_COUNT ||
        phrase >= SAM_VOICE_PHRASE_COUNT ||
        offset >= SAM_VOICE_PHRASE_LENGTH ||
        length > SAM_VOICE_PHRASE_LENGTH - 1u - offset ||
        (length != 0u && text == nullptr)) return false;
    if (target != active_patch_id && !synth_editor_select(synth, target)) {
        return false;
    }
    if (offset == 0u) std::memset(active_speech.phrase[phrase], 0,
                                  SAM_VOICE_PHRASE_LENGTH);
    for (uint8_t index = 0u; index < length; ++index) {
        uint8_t character = text[index];
        active_speech.phrase[phrase][offset + index] =
            static_cast<char>(character >= 32u && character <= 126u ?
                              character : ' ');
    }
    if (final_chunk) {
        active_speech.phrase[phrase][offset + length] = '\0';
    }
    active_patch_dirty = true;
    return true;
}

bool synth_editor_commit(const synth_t *synth, uint8_t scope, uint16_t target) {
    if (scope == SYNTH_EDITOR_SCOPE_PATCH && target == active_patch_id) {
        bool ok = preset_store_save(preset_store_patch_key(target),
                                    &active_patch, sizeof(active_patch));
        if (ok) {
            ok = save_speech_override(static_cast<uint8_t>(target),
                                      active_speech);
        }
        if (ok) active_patch_dirty = false;
        return ok;
    }
    if (scope == SYNTH_EDITOR_SCOPE_BANK && target == active_bank_id) {
        bool ok = preset_store_save(
                                    preset_store_bank_key(static_cast<uint8_t>(target)),
                                    &active_bank, sizeof(active_bank));
        if (ok) active_bank_dirty = false;
        return ok;
    }
    if (scope == SYNTH_EDITOR_SCOPE_GLOBAL) {
        global_record_t globals = capture_global_record(synth);
        bool ok = preset_store_save(PRESET_STORE_GLOBAL_KEY, &globals,
                                    sizeof(globals));
        if (ok) globals_dirty = false;
        return ok;
    }
    return false;
}

bool synth_editor_revert(synth_t *synth, uint8_t scope, uint16_t target) {
    if (scope == SYNTH_EDITOR_SCOPE_PATCH && target == active_patch_id) {
        load_active_patch(static_cast<uint8_t>(target));
        apply_scenes(static_cast<uint8_t>(target));
        return true;
    }
    if (scope == SYNTH_EDITOR_SCOPE_BANK && target == active_bank_id) {
        load_active_bank(static_cast<uint8_t>(target));
        return true;
    }
    if (scope == SYNTH_EDITOR_SCOPE_GLOBAL) {
        global_record_t globals = default_global_record();
        (void)preset_store_load_prefix(PRESET_STORE_GLOBAL_KEY, &globals,
                                       sizeof(globals), NULL);
        apply_global_record(synth, globals);
        status_rgb_set_brightness(editor_led_brightness);
        synth->master_gain_q15 = volume_gain_q15[synth->volume_index];
        globals_dirty = false;
        return true;
    }
    return false;
}

bool synth_editor_restore(synth_t *synth, uint8_t scope, uint16_t target) {
    uint16_t key;
    if (scope == SYNTH_EDITOR_SCOPE_PATCH && target < PRESET_STORE_PATCH_COUNT) {
        bool ok = preset_store_erase(preset_store_patch_key(target));
        ok = preset_store_erase(preset_store_speech_patch_key(target)) && ok;
        ok = preset_store_erase(
            preset_store_speech_extension_key(target, 0u)) && ok;
        ok = preset_store_erase(
            preset_store_speech_extension_key(target, 1u)) && ok;
        return ok && synth_editor_revert(synth, scope, target);
    } else if (scope == SYNTH_EDITOR_SCOPE_BANK &&
               target < PRESET_STORE_BANK_COUNT) {
        key = preset_store_bank_key(static_cast<uint8_t>(target));
    } else if (scope == SYNTH_EDITOR_SCOPE_GLOBAL) {
        key = PRESET_STORE_GLOBAL_KEY;
    } else {
        return false;
    }
    return preset_store_erase(key) && synth_editor_revert(synth, scope, target);
}

void synth_service(synth_t *synth) {
    if (synth->chord_capture_pending != 0u &&
        static_cast<int32_t>(synth->transport_frame -
                             synth->chord_capture_deadline) >= 0) {
        synth->chord_pitch_classes = synth->chord_pending_pitch_classes;
        synth->chord_capture_pending = 0u;
    }
    if (synth->raw_mode) return;
    for (unsigned i = 0; i < SYNTH_VOICE_COUNT; ++i) {
        synth_note_t &note = synth->notes[i];
        if (!note.midi_note_off_pending) continue;
        midi_note_off(note.midi_channel, note.note);
        note.midi_note_off_pending = 0;
    }
}

}  // extern "C"
