#include <stdint.h>

#include "audio_i2s.h"
#include "controls.h"
#include "hardware/clocks.h"
#include "midi_uart.h"
#include "pico/stdlib.h"
#include "raw_capture.h"
#include "sensor.h"
#include "status_rgb.h"
#include "synth.h"

static synth_t synth;
static bool usb_midi_was_mounted;

static void show_program_state(void) {
    if (synth.raw_mode) {
        status_rgb_show_raw_mode();
    } else {
        status_rgb_show_program(synth_program_id(&synth),
                                synth.pitch_bend_enabled != 0u);
    }
}

static void apply_control(control_event_t event) {
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
        default:
            break;
    }
}

int main(void) {
    set_sys_clock_khz(153600u, true);
    synth_init(&synth);
    controls_init();
    midi_uart_init();
    synth_sync_midi(&synth);
    sensor_init();
    audio_i2s_init();
    raw_capture_init();
    status_rgb_init();
    show_program_state();
    synth_startup_chord(&synth);

    for (;;) {
        midi_service();
        bool usb_midi_is_mounted = midi_usb_mounted();
        if (usb_midi_is_mounted && !usb_midi_was_mounted) {
            synth_sync_midi(&synth);
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

        sensor_stats_t stats;
        if (sensor_take_window(&stats, synth_sensitivity(&synth))) {
            synth_sensor_window(&synth, &stats);
        }

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
