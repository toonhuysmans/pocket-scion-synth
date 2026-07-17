#include "synth.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "midi_uart.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/platform.h"
#include "raw_capture.h"

// PRA32-U uses Arduino's boolean alias in its otherwise portable DSP headers.
using boolean = bool;
uint8_t g_midi_ch = 0;
#define PRA32_U_USE_2_CORES_FOR_SIGNAL_PROCESSING
#include "pra32-u-synth.h"

namespace {

// One independent monophonic PRA32-U part per sequencer lane. Only the middle
// part owns chorus and delay; the dry bass and upper parts are mixed into that
// shared effects stage. This keeps the RP2040 RAM cost bounded (the delay line
// alone is about 64 KiB) while giving every lane its own oscillators, filter,
// envelopes and LFO.
PRA32_U_Synth<true> bass_engine;
PRA32_U_Synth<false> middle_engine;
PRA32_U_Synth<true> upper_engine;

constexpr uint8_t bass_lane = 0u;
constexpr uint8_t middle_lane = 1u;
constexpr uint8_t upper_lane = 2u;

volatile uint32_t upper_render_request = 0u;
volatile int16_t upper_render_result = 0;

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
constexpr uint8_t bank_tempo_percent[] = {100, 100, 90, 125, 108, 70, 112, 135};
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
    const uint8_t scene = mode % 16u;
    const uint8_t bank = mode / 16u;
    uint32_t bpm = (static_cast<uint32_t>(scene_bpm[scene]) *
                    bank_tempo_percent[bank]) / 100u;
    const uint32_t straight = (SYNTH_SAMPLE_RATE * 15u) / bpm;
    // 56/44 swing. A pair remains exactly two straight sixteenth notes long.
    return (step & 1u) ? (straight * 88u) / 100u
                       : (straight * 112u) / 100u;
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

constexpr uint8_t scene_count = 16u;
constexpr uint8_t bank_count = 8u;
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
        default:  // Extreme: bounded edge cases across the complete engine.
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

sensor_route_t sensor_route(uint8_t bank, uint8_t program) {
    static constexpr sensor_route_t routes[8] = {
        {  0,   0, 34.0f, 14.0f, 70.0f, 48.0f, 0.55f,  0, 0.85f}, // Legacy
        {127, 127, 34.0f, 14.0f, 42.0f, 24.0f, 1.00f,  0, 1.00f}, // Foundation
        {127,  76, 24.0f, 10.0f, 58.0f, 18.0f, 0.65f, -1, 0.72f}, // Organic
        { 54,  62, 42.0f, 20.0f, 28.0f, 32.0f, 0.35f,  2, 1.45f}, // Percussive
        { 86,  70, 38.0f, 24.0f, 34.0f, 22.0f, 0.85f,  1, 1.12f}, // Bass/Lead
        {108,  54, 18.0f,  8.0f, 46.0f, 12.0f, 0.45f, -2, 0.45f}, // Atmosphere
        { 92, 112, 46.0f, 22.0f, 72.0f, 40.0f, 1.10f,  0, 1.08f}, // Spectral
        {127, 127, 56.0f, 30.0f, 92.0f, 56.0f, 1.40f,  2, 1.75f}, // Extreme
    };
    sensor_route_t result = routes[bank];
    if (program == 14u) result.breath_max = 127u;
    if (program == 10u) result.bend_scale *= 1.25f;
    if (program == 13u) result.ratchet_scale *= 1.20f;
    return result;
}

scene_t make_lane_scene(uint8_t id, uint8_t lane) {
    scene_t scene = make_scene(id);
    // All three parts are deliberately monophonic. Polyphony comes from the
    // three independently articulated lanes rather than four identical voices.
    scene.voice_mode = VOICE_MONOPHONIC;
    scene.voice_assignment = 0u;

    if (lane == bass_lane) {
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
    } else if (lane == upper_lane) {
        scene.sub_mix = adjust_u7(scene.sub_mix, -42);
        scene.osc2_coarse = adjust_u7(scene.osc2_coarse, 12);
        scene.osc2_pitch = adjust_u7(scene.osc2_pitch, 7);
        scene.osc_mix = adjust_u7(scene.osc_mix, 12);
        scene.cutoff = adjust_u7(scene.cutoff, 24);
        scene.resonance = adjust_u7(scene.resonance, -8);
        scene.filter_key_track = adjust_u7(scene.filter_key_track, 18);
        scene.amp_attack = adjust_u7(scene.amp_attack, -5);
        scene.amp_release = adjust_u7(scene.amp_release, -12);
        scene.lfo_rate = adjust_u7(scene.lfo_rate, 12);
        scene.lfo_depth = adjust_u7(scene.lfo_depth, 8);
        scene.amp_gain = adjust_u7(scene.amp_gain, -4);
    }
    return scene;
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
    for (;;) {
        if (upper_render_request == 0u) {
            tight_loop_contents();
            continue;
        }
        upper_render_result = process_dry_engine(upper_engine);
        __dmb();
        upper_render_request = 0u;
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
    target.control_change(CHORUS_MIX, scene.chorus_mix);
    target.control_change(CHORUS_RATE, scene.chorus_rate);
    target.control_change(CHORUS_DEPTH, scene.chorus_depth);
    target.control_change(DELAY_FEEDBACK, scene.delay_feedback);
    target.control_change(DELAY_TIME, scene.delay_time);
    target.control_change(DELAY_MODE, scene.delay_mode);
    target.control_change(MODULATION, 0);
    target.control_change(BTH_CONTROLLER, 0);
    target.pitch_bend(0, 64);
}

void apply_scenes(uint8_t index) {
    apply_scene_to(bass_engine, make_lane_scene(index, bass_lane));
    apply_scene_to(middle_engine, make_lane_scene(index, middle_lane));
    apply_scene_to(upper_engine, make_lane_scene(index, upper_lane));
}

void lane_control_change(uint8_t lane, uint8_t control, uint8_t value) {
    if (lane == bass_lane) bass_engine.control_change(control, value);
    else if (lane == middle_lane) middle_engine.control_change(control, value);
    else upper_engine.control_change(control, value);
}

void set_live_amp_envelope(uint8_t decay, uint8_t sustain, uint8_t release) {
    lane_control_change(bass_lane, AMP_DECAY, adjust_u7(decay, 8));
    lane_control_change(bass_lane, AMP_SUSTAIN, adjust_u7(sustain, 8));
    lane_control_change(bass_lane, AMP_RELEASE, adjust_u7(release, 10));
    lane_control_change(middle_lane, AMP_DECAY, decay);
    lane_control_change(middle_lane, AMP_SUSTAIN, sustain);
    lane_control_change(middle_lane, AMP_RELEASE, release);
    lane_control_change(upper_lane, AMP_DECAY, adjust_u7(decay, -6));
    lane_control_change(upper_lane, AMP_SUSTAIN, adjust_u7(sustain, -8));
    lane_control_change(upper_lane, AMP_RELEASE, adjust_u7(release, -12));
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
    synth_note_t &voice = synth->notes[lane];

    // Repeated lane pitches become ties, so their envelopes are not needlessly
    // restarted. A different pitch replaces only this monophonic lane.
    if (voice.active && voice.note == note &&
        voice.midi_channel == midi_channel) {
        if (voice.frames_left < duration_frames) {
            voice.frames_left = duration_frames;
        }
        return true;
    }

    if (voice.active) {
        lane_note_off(lane, voice.note);
        midi_note_off(voice.midi_channel, voice.note);
        voice.active = 0u;
        voice.midi_note_off_pending = 0u;
    }
    if (voice.midi_note_off_pending) {
        midi_note_off(voice.midi_channel, voice.note);
        voice.midi_note_off_pending = 0u;
    }
    voice.note = note;
    voice.midi_channel = midi_channel;
    voice.lane = lane;
    voice.frames_left = duration_frames;
    voice.active = 1u;
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
    synth_note_t &voice = synth->notes[event.lane];
    if (voice.active && voice.note == event.note &&
        voice.midi_channel == event.midi_channel) {
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

void synth_init(synth_t *synth) {
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
    synth->master_gain_q15 = volume_gain_q15[synth->volume_index];
    bass_engine.initialize();
    middle_engine.initialize();
    upper_engine.initialize();
    apply_scenes(0);
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
        midi_control_change(channel, 23, id);
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
        midi_control_change(channel, 23, id);
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

void synth_sensor_window(synth_t *synth, const sensor_stats_t *stats) {
    if (stats->range_fault) return;

    ++synth->sensor_window_counter;
    float raw_expression = stats->standard_deviation / std::fmax(stats->mean_us, 1.0f);
    raw_expression = clampf(raw_expression * 8.0f, 0.0f, 1.0f);
    synth->sensor_expression += (raw_expression - synth->sensor_expression) * 0.16f;
    float raw_proximity = 1.0f - clampf(stats->mean_us / 100000.0f, 0.0f, 1.0f);
    synth->sensor_proximity += (raw_proximity - synth->sensor_proximity) * 0.12f;
    if (synth->raw_mode) return;

    const uint8_t mode = synth_program_id(synth);
    const uint8_t patch = mode % scene_count;
    const uint8_t bank = mode / scene_count;
    const bool pitch_bend_enabled = synth->pitch_bend_enabled != 0u;
    const scene_t scene = make_scene(mode);
    const sensor_route_t route = sensor_route(bank, patch);

    float bend_target = 0.0f;
    if (synth->previous_mean_us > 0.0f) {
        bend_target = (synth->previous_mean_us - stats->mean_us) /
            std::fmax(synth->previous_mean_us, 1.0f);
        bend_target = clampf(bend_target * 4.0f, -1.0f, 1.0f);
    }
    synth->previous_mean_us = stats->mean_us;
    synth->sensor_bend += (bend_target - synth->sensor_bend) * 0.18f;

    float breath_source = synth->sensor_proximity * 0.65f +
                          synth->sensor_expression * 0.35f;
    uint8_t breath = clamp_u7(static_cast<int>(
        breath_source * static_cast<float>(route.breath_max)));
    uint8_t modulation = clamp_u7(static_cast<int>(
        synth->sensor_expression * static_cast<float>(route.modulation_max)));
    all_engines_control_change(BTH_CONTROLLER, breath);
    all_engines_control_change(MODULATION, modulation);

    // The three transient-oriented programs span a wider envelope range under
    // the plant. Values are deliberately distinct rather than sharing one
    // generic movement-to-length mapping.
    if (bank <= 1u && patch == 4u) {  // Glass: proximity blooms sustain and release.
        set_live_amp_envelope(
            clamp_u7(42 + static_cast<int>(synth->sensor_expression * 48.0f)),
            clamp_u7(38 + static_cast<int>(synth->sensor_proximity * 72.0f)),
            clamp_u7(58 + static_cast<int>(synth->sensor_proximity * 58.0f)));
    } else if (bank <= 1u && patch == 8u) {  // Acid: stab to tied phrase.
        set_live_amp_envelope(
            clamp_u7(40 + static_cast<int>(synth->sensor_expression * 62.0f)),
            clamp_u7(48 + static_cast<int>(synth->sensor_expression * 58.0f)),
            clamp_u7(32 + static_cast<int>(synth->sensor_proximity * 62.0f)));
    } else if (bank <= 1u && patch == 13u) {  // Percussion: spread grows tail.
        float spread_envelope = clampf(
            static_cast<float>(stats->delta_us) / 60000.0f, 0.0f, 1.0f);
        set_live_amp_envelope(
            clamp_u7(34 + static_cast<int>(synth->sensor_proximity * 58.0f)),
            clamp_u7(18 + static_cast<int>(synth->sensor_expression * 54.0f)),
            clamp_u7(30 + static_cast<int>(spread_envelope * 74.0f)));
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
            synth->euclid_steps[lane] = euclid_lengths[patch][lane];
        }
        int density = static_cast<int>(synth->sensitivity_index) / 2 +
                      route.density_offset;
        int bass_pulses = 3 + density +
            static_cast<int>(synth->sensor_proximity * 3.0f);
        int melody_pulses = 2 + density +
            static_cast<int>(synth->sensor_expression * 4.0f);
        int upper_pulses = 1 + density + static_cast<int>(
            (synth->sensor_expression + synth->sensor_proximity) * 2.0f);
        if (stats->trigger) ++melody_pulses;
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
    if (!any_hit) return;

    int octave = synth->sensor_proximity > 0.72f ? 2 :
                 synth->sensor_proximity > 0.38f ? 1 : 0;
    if (bank == 4u && octave > 0) --octave;
    if (bank == 6u && synth->sensor_expression > 0.65f && octave < 2) ++octave;
    unsigned motif_step = (step + bar * 3u) & 15u;
    unsigned melody_degree = melodic_motifs[patch][motif_step];

    uint8_t cutoff = clamp_u7(scene.cutoff + static_cast<int>(
        synth->sensor_expression * route.cutoff_range));
    uint8_t resonance = clamp_u7(scene.resonance + static_cast<int>(
        synth->sensor_expression * route.resonance_range));
    for (uint8_t lane = 0u; lane < SYNTH_LANE_COUNT; ++lane) {
        const scene_t lane_scene = make_lane_scene(mode, lane);
        float motion_scale = lane == bass_lane ? 0.70f :
                             lane == upper_lane ? 1.12f : 1.0f;
        lane_control_change(lane, FILTER_CUTOFF, clamp_u7(
            lane_scene.cutoff + static_cast<int>(
                synth->sensor_expression * route.cutoff_range * motion_scale)));
        lane_control_change(lane, FILTER_RESO, clamp_u7(
            lane_scene.resonance + static_cast<int>(
                synth->sensor_expression * route.resonance_range * motion_scale)));
        lane_control_change(lane, OSC_1_MORPH, clamp_u7(
            lane_scene.osc1_morph + static_cast<int>(
                (synth->sensor_expression - 0.5f) * route.morph_range *
                motion_scale)));
        lane_control_change(lane, LFO_RATE, clamp_u7(
            lane_scene.lfo_rate + static_cast<int>(
                synth->sensor_expression * route.lfo_rate_range * motion_scale)));
    }

    const unsigned degrees[3] = {
        static_cast<unsigned>((bar + step / 4u) % 4u),
        melody_degree,
        static_cast<unsigned>((melody_degree + 2u) % 7u),
    };
    const int octave_offsets[3] = {octave > 0 ? octave - 1 : 0, octave, octave + 1};
    float lane_gates[3] = {2.20f, 1.45f, 0.90f};
    float gate_scale = bank == 3u ? 0.62f :
                       bank == 4u ? 1.15f :
                       bank == 5u ? 1.85f :
                       bank == 7u ? 0.78f : 1.0f;
    for (float &gate : lane_gates) gate *= gate_scale;
    const int lane_velocity[3] = {0, 8, -4};
    float spread = clampf(static_cast<float>(stats->delta_us) / 60000.0f,
                          0.0f, 1.0f);
    float ratchet_drive = synth->sensor_expression * 0.48f +
        synth->sensor_proximity * 0.20f + spread * 0.22f +
        static_cast<float>(synth->sensitivity_index) * 0.025f;
    ratchet_drive = clampf(ratchet_drive * route.ratchet_scale, 0.0f, 1.0f);
    uint32_t random = random_u32(synth);
    for (unsigned lane = 0; lane < 3u; ++lane) {
        if (!lane_hit[lane]) continue;
        int note_value = static_cast<int>(synth->root_note) +
            scales[patch][degrees[lane]] + octave_offsets[lane] * 12;
        if (note_value > 108) note_value = 108;
        float gate_steps = lane_gates[lane] *
            duration_multipliers[synth->duration_index];
        gate_steps = clampf(gate_steps, 0.25f, 4.0f);
        uint32_t duration_frames = static_cast<uint32_t>(
            gate_steps * static_cast<float>(step_frames));
        int velocity_value = 46 + lane_velocity[lane] +
            static_cast<int>(synth->sensor_expression * 42.0f);
        if ((step & 3u) == 0u) velocity_value += 8;
        if (stats->trigger) velocity_value += 8;
        velocity_value += static_cast<int>((random >> (lane * 3u)) & 7u) - 3;
        uint8_t note = static_cast<uint8_t>(note_value);
        uint8_t velocity = clamp_u7(velocity_value);
        uint8_t midi_channel = midi_channel_for_lane(synth, lane);
        bool started = start_note(synth, static_cast<uint8_t>(lane), note,
                                  velocity, midi_channel,
                                  duration_frames);

        // Bass ratchets are rarer; melody and upper harmony respond more
        // strongly. Counts range from two to four subdivisions of this step.
        const float lane_response[3] = {0.52f, 0.92f, 1.0f};
        uint32_t lane_random = random_u32(synth);
        unsigned chance = static_cast<unsigned>(
            clampf(ratchet_drive * lane_response[lane] * 112.0f, 0.0f, 112.0f));
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
        upper_render_request = 1u;
        __dmb();
        int16_t bass = process_dry_engine(bass_engine);
        while (upper_render_request != 0u) {
            tight_loop_contents();
        }
        __dmb();
        int16_t upper = upper_render_result;
        int32_t dry_sum = static_cast<int32_t>(bass) + upper;
        int16_t dry_input = static_cast<int16_t>(dry_sum / 2);
        int16_t right = 0;
        int16_t left = middle_engine.process(dry_input, right);
        left = scale_sample(left, synth->master_gain_q15);
        right = scale_sample(right, synth->master_gain_q15);
        stereo_frames[frame] = (static_cast<uint32_t>(static_cast<uint16_t>(left)) << 16) |
            static_cast<uint16_t>(right);
    }
    int16_t visual_envelope = bass_engine.get_amp_envelope_output();
    int16_t middle_envelope = middle_engine.get_amp_envelope_output();
    int16_t upper_envelope = upper_engine.get_amp_envelope_output();
    if (middle_envelope > visual_envelope) visual_envelope = middle_envelope;
    if (upper_envelope > visual_envelope) visual_envelope = upper_envelope;
    synth->visual_amp_envelope = visual_envelope;
    synth->visual_lfo = middle_engine.get_lfo_output();
    synth->transport_frame += frame_count;
}

void synth_service(synth_t *synth) {
    if (synth->raw_mode) return;
    for (unsigned i = 0; i < SYNTH_VOICE_COUNT; ++i) {
        synth_note_t &note = synth->notes[i];
        if (!note.midi_note_off_pending) continue;
        midi_note_off(note.midi_channel, note.note);
        note.midi_note_off_pending = 0;
    }
}

}  // extern "C"
