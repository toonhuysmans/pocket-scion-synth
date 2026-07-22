#include <stdint.h>
#include <stdio.h>

#include "audio_i2s.h"
#include "controls.h"
#include "display.h"
#include "editor_protocol.h"
#include "hardware/clocks.h"
#include "midi_uart.h"
#include "pico2_menu.h"
#include "pico/stdlib.h"
#include "raw_capture.h"
#include "sensor.h"
#include "status_rgb.h"
#include "synth.h"

static synth_t synth;
static bool usb_midi_was_mounted;
#if PICO_RP2350
static uint16_t display_parameter;
static bool voice_edit_mode;
static uint8_t tree_level;
static uint8_t tree_lane;
static uint8_t tree_section;
static const char *const tree_sections[] = {"OSCILLATOR", "FILTER", "ENVELOPES", "LFO", "VOICE", "EXPRESSION", "EFFECTS", "SEQUENCE", "ARTICULATION", "MOTION", "SPEECH"};
#define TREE_SECTION_COUNT 11u
static uint8_t section_first(uint8_t s) { static const uint8_t v[] = {0,8,12,20,18,36,41,47,55,75,85}; return v[s]; }
static uint8_t section_last(uint8_t s) { static const uint8_t v[] = {7,11,17,26,35,40,46,54,74,84,94}; return v[s]; }
#define DISPLAY_GLOBAL_COUNT 9u
#define DISPLAY_VOICE_PARAMETER_COUNT SYNTH_EDITOR_PATCH_SHARED_COUNT
#define DISPLAY_PARAMETER_COUNT (DISPLAY_GLOBAL_COUNT + 3u * DISPLAY_VOICE_PARAMETER_COUNT)
static const char *const voice_parameter_names[47] = {
    "OSC1 WAVE", "OSC1 SHAPE", "OSC1 MORPH", "SUB/NOISE", "OSC2 WAVE", "OSC2 COARSE", "OSC2 FINE", "OSC MIX",
    "CUTOFF", "RESONANCE", "FILTER EG", "KEY TRACK", "MOD ATTACK", "MOD DECAY", "MOD SUSTAIN", "MOD RELEASE",
    "MOD OSC AMT", "MOD OSC DEST", "VOICE MODE", "PORTAMENTO", "LFO WAVE", "LFO RATE", "LFO DEPTH", "LFO FADE",
    "LFO OSC AMT", "LFO OSC DEST", "LFO FILTER", "AMP GAIN", "AMP ATTACK", "AMP DECAY", "AMP SUSTAIN", "AMP RELEASE",
    "FILTER MODE", "AMP EG MOD", "REL=DECAY", "BEND RANGE", "BREATH FILTER", "BREATH AMP", "ENV VELOCITY", "AMP VELOCITY",
    "ASSIGN MODE", "CHORUS MIX", "CHORUS RATE", "CHORUS DEPTH", "DELAY FEEDBACK", "DELAY TIME", "DELAY MODE"
};
static const char *const shared_parameter_names[] = {
 "SCALE DEGREE 1","SCALE DEGREE 2","SCALE DEGREE 3","SCALE DEGREE 4","SCALE DEGREE 5","SCALE DEGREE 6","SCALE DEGREE 7",
 "MOTIF 1","MOTIF 2","MOTIF 3","MOTIF 4","MOTIF 5","MOTIF 6","MOTIF 7","MOTIF 8","MOTIF 9","MOTIF 10","MOTIF 11","MOTIF 12","MOTIF 13","MOTIF 14","MOTIF 15","MOTIF 16",
 "TEMPO","BASS STEPS","PAD STEPS","LEAD STEPS","SWING","BASS GATE","PAD GATE","LEAD GATE","BASS DENSITY","BASS RATCHETS","PAD RATCHETS","LEAD RATCHETS","LOW-LANE MODE","PERC BALANCE","SENSOR INFLUENCE","VARIATION"
};
static const char *const late_parameter_names[] = {"PERC 1 SOUND","PERC 1 ROLE","PERC 1 WEIGHT","PERC 1 LEVEL","PERC 1 TUNE","PERC 1 TONE","PERC 1 NOISE","PERC 1 DECAY","PERC 1 TRANSIENT","PERC 1 RATCHET","PAD DENSITY","LEAD DENSITY","BREATH MAX","PITCH-BEND RESPONSE","RATCHET RESPONSE","AMP DECAY MOTION","AMP SUSTAIN MOTION","AMP RELEASE MOTION","PRESSURE OCTAVES","EXPRESSION THRESHOLD","CUTOFF MOTION","RESONANCE MOTION","MORPH MOTION","LFO RATE MOTION"};
#endif

static void show_display_state(void) {
#if PICO_RP2350
    pico2_menu_show(&synth);
    return;
    if (tree_level == 1u) {
        static const char *const lanes[] = {"BASS", "PAD", "LEAD"};
        display_show_parameter(lanes[tree_lane], tree_lane + 1, 1, 3,
                               synth.program_index, synth.bank_index, true);
        return;
    }
    if (tree_level == 2u) {
        display_show_parameter(tree_sections[tree_section], tree_section + 1, 1, TREE_SECTION_COUNT,
                               synth.program_index, synth.bank_index, true);
        return;
    }
    static const char *names[] = {"SENS", "VOLUME", "DURATION", "ROOT", "BANK", "INST",
                                  "P-BEND", "MIDI", "RAW"};
    int value = 0;
    int minimum = 0;
    int maximum = 0;
    switch (display_parameter) {
        case 0: value = synth.sensitivity_index; maximum = 7; break;
        case 1: value = synth.volume_index; maximum = 11; break;
        case 2: value = synth.duration_index; maximum = 7; break;
        case 3: value = synth.root_note; minimum = 24; maximum = 72; break;
        case 4: value = synth.bank_index + 1; minimum = 1; maximum = 16; break;
        case 5: value = synth.program_index + 1; minimum = 1; maximum = 16; break;
        case 6: value = synth.pitch_bend_enabled; maximum = 1; break;
        case 7: value = synth.midi_multichannel; maximum = 1; break;
        case 8: value = synth.raw_mode; maximum = 1; break;
        default: {
            uint16_t offset = (uint16_t)(display_parameter - DISPLAY_GLOBAL_COUNT);
            uint8_t lane = (uint8_t)(offset / DISPLAY_VOICE_PARAMETER_COUNT);
            uint8_t parameter = (uint8_t)(offset % DISPLAY_VOICE_PARAMETER_COUNT);
            value = 0; maximum = 127;
            uint16_t editor_value = 0;
            (void)synth_editor_get(&synth, SYNTH_EDITOR_SCOPE_PATCH,
                                   synth_program_id(&synth), lane, parameter,
                                   &editor_value);
            value = editor_value;
            static const char *lanes[] = {"BASS", "PAD", "LEAD"};
            char label[24];
            if (parameter < 47u) snprintf(label, sizeof(label), "%s %s", lanes[lane], voice_parameter_names[parameter]);
            else if (parameter >= 47u && parameter < 86u) snprintf(label, sizeof(label), "%s", shared_parameter_names[parameter - 47u]);
            else if (parameter >= 86u && parameter < 110u) snprintf(label, sizeof(label), "%s", late_parameter_names[parameter - 86u]);
            else snprintf(label, sizeof(label), "%s P%u", lanes[lane], parameter);
            display_show_parameter(label, value, 0, 127,
                                   synth.program_index, synth.bank_index, true);
            return;
        }
    }
    display_show_parameter(names[display_parameter], value, minimum, maximum,
                           synth.program_index, synth.bank_index, true);
#endif
}

static void apply_midi_chord_note(uint8_t note, bool pressed) {
    synth_midi_chord_note(&synth, note, pressed);
}

static void reset_midi_chord(bool clear_latch) {
    if (clear_latch) synth_midi_chord_clear(&synth);
    else synth_midi_chord_release(&synth);
}

static void show_program_state(void) {
    if (synth.raw_mode) {
        status_rgb_show_raw_mode();
    } else {
        status_rgb_show_program(synth_program_id(&synth),
                                synth.pitch_bend_enabled != 0u);
    }
    show_display_state();
}

static void apply_control(control_event_t event) {
#if PICO_RP2350
    if (pico2_menu_handle(&synth, event)) return;
#endif
    switch (event) {
        case CONTROL_SENSITIVITY_DOWN:
            synth_set_sensitivity_step(&synth, -1);
            status_rgb_show_level(STATUS_RGB_GREEN, synth.sensitivity_index, 7);
            break;
        case CONTROL_SENSITIVITY_UP:
            synth_set_sensitivity_step(&synth, 1);
            status_rgb_show_level(STATUS_RGB_GREEN, synth.sensitivity_index, 7);
            break;
        case CONTROL_MODE:
            synth_next_mode(&synth);
            show_program_state();
            break;
        case CONTROL_BANK_NEXT:
            synth_next_bank(&synth);
            show_program_state();
            break;
        case CONTROL_PITCH_BEND_TOGGLE:
            synth_toggle_pitch_bend(&synth);
            show_program_state();
            break;
        case CONTROL_MIDI_MODE_TOGGLE:
            synth_toggle_midi_mode(&synth);
            status_rgb_show_midi_mode(synth.midi_multichannel != 0u);
            break;
        case CONTROL_RAW_MODE_TOGGLE:
            synth_toggle_raw_mode(&synth);
            show_program_state();
            break;
        case CONTROL_VOLUME_DOWN:
            synth_set_volume_step(&synth, -1);
            status_rgb_show_level(STATUS_RGB_YELLOW, synth.volume_index, 11);
            break;
        case CONTROL_VOLUME_UP:
            synth_set_volume_step(&synth, 1);
            status_rgb_show_level(STATUS_RGB_YELLOW, synth.volume_index, 11);
            break;
        case CONTROL_DURATION_DOWN:
            synth_set_duration_step(&synth, -1);
            status_rgb_show_level(STATUS_RGB_CYAN, synth.duration_index, 7);
            break;
        case CONTROL_DURATION_UP:
            synth_set_duration_step(&synth, 1);
            status_rgb_show_level(STATUS_RGB_CYAN, synth.duration_index, 7);
            break;
        case CONTROL_ROOT_DOWN:
            synth_set_root_step(&synth, -1);
            status_rgb_show_level(STATUS_RGB_PURPLE,
                                  (uint8_t)(synth.root_note - 24u), 48);
            break;
        case CONTROL_ROOT_UP:
            synth_set_root_step(&synth, 1);
            status_rgb_show_level(STATUS_RGB_PURPLE,
                                  (uint8_t)(synth.root_note - 24u), 48);
            break;
#if PICO_RP2350
        case CONTROL_PARAMETER_ENTER:
            voice_edit_mode = true;
            if (tree_level == 0u) { tree_level = 1u; tree_lane = 0u; display_parameter = DISPLAY_GLOBAL_COUNT; }
            else if (tree_level == 1u) { tree_level = 2u; tree_section = 0u; }
            else if (tree_level == 2u) { tree_level = 3u; display_parameter = DISPLAY_GLOBAL_COUNT + tree_lane * DISPLAY_VOICE_PARAMETER_COUNT + section_first(tree_section); }
            show_display_state();
            break;
        case CONTROL_PARAMETER_BACK:
            if (tree_level == 3u) tree_level = 2u;
            else if (tree_level == 2u) tree_level = 1u;
            else { voice_edit_mode = false; tree_level = 0u; display_parameter = 0u; }
            show_display_state();
            break;
        case CONTROL_PARAMETER_PREVIOUS:
            if (tree_level == 1u) { tree_lane = (uint8_t)((tree_lane + 2u) % 3u); show_display_state(); break; }
            if (tree_level == 2u) { tree_section = (uint8_t)((tree_section + TREE_SECTION_COUNT - 1u) % TREE_SECTION_COUNT); show_display_state(); break; }
            if (tree_level == 3u) { uint16_t base = DISPLAY_GLOBAL_COUNT + tree_lane * DISPLAY_VOICE_PARAMETER_COUNT; uint8_t p = (uint8_t)(display_parameter - base); display_parameter = base + (p <= section_first(tree_section) ? section_last(tree_section) : p - 1u); show_display_state(); break; }
            if (voice_edit_mode) {
                if (display_parameter <= DISPLAY_GLOBAL_COUNT) display_parameter = DISPLAY_PARAMETER_COUNT - 1u;
                else display_parameter--;
            } else {
                display_parameter = (uint8_t)((display_parameter + DISPLAY_GLOBAL_COUNT - 1u) % DISPLAY_GLOBAL_COUNT);
            }
            show_display_state();
            break;
        case CONTROL_PARAMETER_NEXT:
            if (tree_level == 1u) { tree_lane = (uint8_t)((tree_lane + 1u) % 3u); show_display_state(); break; }
            if (tree_level == 2u) { tree_section = (uint8_t)((tree_section + 1u) % TREE_SECTION_COUNT); show_display_state(); break; }
            if (tree_level == 3u) { uint16_t base = DISPLAY_GLOBAL_COUNT + tree_lane * DISPLAY_VOICE_PARAMETER_COUNT; uint8_t p = (uint8_t)(display_parameter - base); display_parameter = base + (p >= section_last(tree_section) ? section_first(tree_section) : p + 1u); show_display_state(); break; }
            if (voice_edit_mode) {
                display_parameter++;
                if (display_parameter >= DISPLAY_PARAMETER_COUNT) display_parameter = DISPLAY_GLOBAL_COUNT;
            } else {
                display_parameter = (uint8_t)((display_parameter + 1u) % DISPLAY_GLOBAL_COUNT);
            }
            show_display_state();
            break;
        case CONTROL_PARAMETER_DECREASE:
            if (display_parameter == 0u) synth_set_sensitivity_step(&synth, -1);
            else if (display_parameter == 1u) synth_set_volume_step(&synth, -1);
            else if (display_parameter == 2u) synth_set_duration_step(&synth, -1);
            else if (display_parameter == 3u) synth_set_root_step(&synth, -1);
            else if (display_parameter == 4u) synth_set_bank_step(&synth, -1);
            else if (display_parameter == 5u) synth_set_program_step(&synth, -1);
            else if (display_parameter == 6u) synth_toggle_pitch_bend(&synth);
            else if (display_parameter == 7u) synth_toggle_midi_mode(&synth);
            else if (display_parameter == 8u) synth_toggle_raw_mode(&synth);
            else {
                uint16_t offset = (uint16_t)(display_parameter - DISPLAY_GLOBAL_COUNT);
                uint8_t lane = (uint8_t)(offset / DISPLAY_VOICE_PARAMETER_COUNT);
                uint8_t parameter = (uint8_t)(offset % DISPLAY_VOICE_PARAMETER_COUNT);
                uint16_t current = 0;
                if (synth_editor_get(&synth, SYNTH_EDITOR_SCOPE_PATCH, synth_program_id(&synth), lane, parameter, &current) && current > 0u)
                    (void)synth_editor_set(&synth, SYNTH_EDITOR_SCOPE_PATCH, synth_program_id(&synth), lane, parameter, (uint16_t)(current - 1u));
            }
            show_display_state();
            break;
        case CONTROL_PARAMETER_INCREASE:
            if (display_parameter == 0u) synth_set_sensitivity_step(&synth, 1);
            else if (display_parameter == 1u) synth_set_volume_step(&synth, 1);
            else if (display_parameter == 2u) synth_set_duration_step(&synth, 1);
            else if (display_parameter == 3u) synth_set_root_step(&synth, 1);
            else if (display_parameter == 4u) synth_set_bank_step(&synth, 1);
            else if (display_parameter == 5u) synth_set_program_step(&synth, 1);
            else if (display_parameter == 6u) synth_toggle_pitch_bend(&synth);
            else if (display_parameter == 7u) synth_toggle_midi_mode(&synth);
            else if (display_parameter == 8u) synth_toggle_raw_mode(&synth);
            else {
                uint16_t offset = (uint16_t)(display_parameter - DISPLAY_GLOBAL_COUNT);
                uint8_t lane = (uint8_t)(offset / DISPLAY_VOICE_PARAMETER_COUNT);
                uint8_t parameter = (uint8_t)(offset % DISPLAY_VOICE_PARAMETER_COUNT);
                uint16_t current = 0;
                if (synth_editor_get(&synth, SYNTH_EDITOR_SCOPE_PATCH, synth_program_id(&synth), lane, parameter, &current) && current < 127u)
                    (void)synth_editor_set(&synth, SYNTH_EDITOR_SCOPE_PATCH, synth_program_id(&synth), lane, parameter, (uint16_t)(current + 1u));
            }
            show_display_state();
            break;
#endif
        default:
            break;
    }
}

int main(void) {
#if PICO_RP2350
    // 150 MHz is the rated Pico 2 clock. It also divides exactly to the
    // 3.072 MHz I2S bit clock through PIO's 16.8 fixed-point divider.
    set_sys_clock_khz(150000u, true);
#else
    set_sys_clock_khz(153600u, true);
#endif
    synth_init(&synth);
#if PICO_RP2350
    pico2_menu_init();
#endif
    controls_init();
    midi_uart_init();
    midi_set_note_input_handler(apply_midi_chord_note);
    midi_set_chord_reset_input_handler(reset_midi_chord);
    editor_protocol_init(&synth);
    synth_sync_midi(&synth);
    sensor_init();
    audio_i2s_init();
    raw_capture_init();
    status_rgb_init();
    display_init();
    show_program_state();
    show_display_state();
    synth_startup_chord(&synth);

    for (;;) {
        midi_service();
        bool usb_midi_is_mounted = midi_usb_mounted();
        if (usb_midi_is_mounted && !usb_midi_was_mounted) {
            synth_sync_midi(&synth);
        } else if (!usb_midi_is_mounted && usb_midi_was_mounted) {
            synth_midi_chord_clear(&synth);
        }
        usb_midi_was_mounted = usb_midi_is_mounted;
        uint32_t *audio_frames;
        if (audio_i2s_take_buffer(&audio_frames)) {
            synth_render(&synth, audio_frames, AUDIO_FRAMES_PER_BUFFER);
            audio_i2s_submit_buffer(audio_frames);
            continue;
        }

        sensor_service();
        synth_service(&synth);
        apply_control(controls_poll());
#if PICO_RP2350
        pico2_menu_service(&synth);
#endif

        sensor_stats_t stats;
        if (sensor_take_window(&stats, synth_sensitivity(&synth))) {
            synth_sensor_window(&synth, &stats);
        }
        synth_sensor_tick(&synth, sensor_has_recent_activity());

        uint8_t active_voices = 0;
        for (unsigned i = 0; i < SYNTH_VOICE_COUNT; ++i) {
            if (synth.notes[i].active) ++active_voices;
        }
        uint8_t rhythm_density = 0;
        for (unsigned i = 0; i < 3u; ++i) {
            rhythm_density = (uint8_t)(rhythm_density + synth.euclid_pulses[i]);
        }
        uint8_t pending_ratchets = 0;
        for (unsigned i = 0; i < SYNTH_RATCHET_EVENT_COUNT; ++i) {
            if (synth.ratchets[i].active) ++pending_ratchets;
        }
        status_rgb_service(synth_program_id(&synth),
                           synth.pitch_bend_enabled != 0u,
                           synth.raw_mode != 0u,
                           synth.sensor_expression, synth.sensor_proximity,
                           active_voices, rhythm_density, pending_ratchets,
                           synth.note_on_counter,
                           synth.ratchet_fire_counter,
                           synth.visual_amp_envelope, synth.visual_lfo);

        tight_loop_contents();
    }
}
