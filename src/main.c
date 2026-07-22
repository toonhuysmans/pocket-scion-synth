#include <stdint.h>

#include "audio_i2s.h"
#include "controls.h"
#include "display.h"
#include "editor_protocol.h"
#include "hardware/clocks.h"
#include "midi_uart.h"
#include "pico/stdlib.h"
#include "raw_capture.h"
#include "sensor.h"
#include "status_rgb.h"
#include "synth.h"

static synth_t synth;
static bool usb_midi_was_mounted;
#if PICO_RP2350
static uint8_t display_parameter;
#endif

static void show_display_state(void) {
#if PICO_RP2350
    static const char *names[] = {"SENS", "VOLUME", "DURATION", "ROOT", "BANK", "INST"};
    int value = 0;
    int minimum = 0;
    int maximum = 0;
    switch (display_parameter) {
        case 0: value = synth.sensitivity_index; maximum = 7; break;
        case 1: value = synth.volume_index; maximum = 11; break;
        case 2: value = synth.duration_index; maximum = 7; break;
        case 3: value = synth.root_note; minimum = 24; maximum = 72; break;
        case 4: value = synth.bank_index + 1; minimum = 1; maximum = 16; break;
        default: value = synth.program_index + 1; minimum = 1; maximum = 16; break;
    }
    display_show_parameter(names[display_parameter], value, minimum, maximum,
                           synth_program_id(&synth), synth.bank_index, true);
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
        case CONTROL_PARAMETER_PREVIOUS:
            display_parameter = (uint8_t)((display_parameter + 5u) % 6u);
            show_display_state();
            break;
        case CONTROL_PARAMETER_NEXT:
            display_parameter = (uint8_t)((display_parameter + 1u) % 6u);
            show_display_state();
            break;
        case CONTROL_PARAMETER_DECREASE:
            if (display_parameter == 0u) synth_set_sensitivity_step(&synth, -1);
            else if (display_parameter == 1u) synth_set_volume_step(&synth, -1);
            else if (display_parameter == 2u) synth_set_duration_step(&synth, -1);
            else if (display_parameter == 3u) synth_set_root_step(&synth, -1);
            else if (display_parameter == 4u) synth_set_bank_step(&synth, -1);
            else synth_set_program_step(&synth, -1);
            show_display_state();
            break;
        case CONTROL_PARAMETER_INCREASE:
            if (display_parameter == 0u) synth_set_sensitivity_step(&synth, 1);
            else if (display_parameter == 1u) synth_set_volume_step(&synth, 1);
            else if (display_parameter == 2u) synth_set_duration_step(&synth, 1);
            else if (display_parameter == 3u) synth_set_root_step(&synth, 1);
            else if (display_parameter == 4u) synth_set_bank_step(&synth, 1);
            else synth_set_program_step(&synth, 1);
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
