#include "status_rgb.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "board_pins.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "rgb_tx.pio.h"

#ifndef SCION_RGB_OUTPUT_ENABLED
#define SCION_RGB_OUTPUT_ENABLED 1
#endif

#if SCION_RGB_OUTPUT_ENABLED

#define RGB_LED_COUNT 9u
#define RGB_CENTRE_INDEX 4u
#define RGB_RING_COUNT 5u
#define RGB_ACTIVE_PART_COUNT 3u
#define RGB_FRAME_PERIOD_US 20000u
#define RGB_OVERLAY_TIME_US 900000u

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} rgb_t;

static PIO rgb_pio = pio1;
static const uint rgb_sm = 0;
static rgb_t pixels[RGB_LED_COUNT];
static rgb_t last_sent_pixels[RGB_LED_COUNT];
static bool frame_sent;
static uint32_t overlay_until_us;
static uint32_t next_frame_us;
static uint32_t last_note_on_counter;
static uint32_t last_ratchet_fire_counter;
static uint8_t ratchet_flash_frames;
static uint8_t latched_highest_ring;
static bool animation_counters_initialized;
static uint8_t master_brightness = 127u;

static const rgb_t parameter_colours[] = {
    [STATUS_RGB_GREEN]  = {  8, 100, 12 },
    [STATUS_RGB_YELLOW] = {100,  82,  0 },
    [STATUS_RGB_BLUE]   = {  8,  28, 100 },
    [STATUS_RGB_CYAN]   = {  0,  82, 82 },
    [STATUS_RGB_PURPLE] = { 72,  16, 96 },
};

static const rgb_t scene_colours[] = {
    {100,  8,  4},
    {  5, 28, 100},
    {  4, 90, 12},
    { 72, 10, 96},
    {  0, 76, 82},
    {100, 52,  0},
    { 45,  8, 92},
    {  4, 46, 96},
    {100,  0, 22},
    { 92,  0, 76},
    { 18, 64, 96},
    { 72, 42,  4},
    { 58, 92, 96},
    { 96, 30,  2},
    { 30, 92, 54},
    {100,  0,100},
};

static rgb_t bank_colours[] = {
    {100, 62,  8},  // Legacy
    { 72, 72, 72},  // Foundation
    {  8, 92, 28},  // Organic
    {100, 34,  2},  // Percussive
    {100,  4, 18},  // Bass & Lead
    {  8, 30,100},  // Atmosphere
    {  0, 88, 92},  // Spectral
    {100,  0, 92},  // Extreme
};

static uint8_t scale_channel(uint8_t channel, uint8_t scale) {
    return (uint8_t)(((uint16_t)channel * scale) / 100u);
}

static rgb_t scale_colour(rgb_t colour, uint8_t scale) {
    rgb_t result = {
        scale_channel(colour.red, scale),
        scale_channel(colour.green, scale),
        scale_channel(colour.blue, scale),
    };
    return result;
}

static uint8_t blend_channel(uint8_t a, uint8_t b, uint8_t b_amount) {
    return (uint8_t)(((uint16_t)a * (100u - b_amount) +
                      (uint16_t)b * b_amount) / 100u);
}

static rgb_t blend_colour(rgb_t a, rgb_t b, uint8_t b_amount) {
    rgb_t result = {
        blend_channel(a.red, b.red, b_amount),
        blend_channel(a.green, b.green, b_amount),
        blend_channel(a.blue, b.blue, b_amount),
    };
    return result;
}

static rgb_t intensify_colour(rgb_t colour) {
    uint8_t minimum = colour.red;
    if (colour.green < minimum) minimum = colour.green;
    if (colour.blue < minimum) minimum = colour.blue;
    uint8_t maximum = colour.red;
    if (colour.green > maximum) maximum = colour.green;
    if (colour.blue > maximum) maximum = colour.blue;

    // Pull a portion of the shared grey component out, then normalize the
    // strongest channel. Near-neutral patches stay neutral but become richer.
    uint8_t spread = (uint8_t)(maximum - minimum);
    uint8_t floor = spread < 8u ? 0u : (uint8_t)(minimum * 2u / 3u);
    colour.red = (uint8_t)(colour.red - floor);
    colour.green = (uint8_t)(colour.green - floor);
    colour.blue = (uint8_t)(colour.blue - floor);
    maximum = (uint8_t)(maximum - floor);
    if (maximum == 0u) return colour;
    colour.red = (uint8_t)((uint16_t)colour.red * 100u / maximum);
    colour.green = (uint8_t)((uint16_t)colour.green * 100u / maximum);
    colour.blue = (uint8_t)((uint16_t)colour.blue * 100u / maximum);
    return colour;
}

static rgb_t program_colour(uint8_t program, bool pitch_bend_enabled) {
    const unsigned count = sizeof(scene_colours) / sizeof(scene_colours[0]);
    rgb_t colour = scene_colours[program % count];
    unsigned bank = program / count;
    colour = blend_colour(colour,
        bank_colours[bank % (sizeof(bank_colours) / sizeof(bank_colours[0]))],
        28u);
    if (pitch_bend_enabled) {
        colour = blend_colour(colour, (rgb_t){88, 44, 100}, 28u);
    }
    return colour;
}

static unsigned pixel_ring(unsigned pixel) {
    return pixel > RGB_CENTRE_INDEX
        ? pixel - RGB_CENTRE_INDEX
        : RGB_CENTRE_INDEX - pixel;
}

static void render_rings(rgb_t colour, unsigned highest_ring,
                         uint8_t brightness) {
    if (highest_ring >= RGB_RING_COUNT) highest_ring = RGB_RING_COUNT - 1u;
    for (unsigned pixel = 0; pixel < RGB_LED_COUNT; ++pixel) {
        unsigned ring = pixel_ring(pixel);
        if (ring > highest_ring) {
            pixels[pixel] = (rgb_t){0, 0, 0};
            continue;
        }

        // The original reduces the outer-pixel drive to compensate for its
        // larger illuminated artwork area. Retain a gentler version here.
        uint8_t ring_scale = (uint8_t)(100u - ring * 9u);
        pixels[pixel] = scale_colour(
            scale_colour(colour, brightness), ring_scale);
    }
}

static uint32_t pack_grb(rgb_t colour) {
    return ((uint32_t)colour.green << 24) |
           ((uint32_t)colour.red << 16) |
           ((uint32_t)colour.blue << 8);
}

static void flush(void) {
    if (frame_sent && memcmp(pixels, last_sent_pixels, sizeof(pixels)) == 0) {
        return;
    }
    for (unsigned pixel = 0; pixel < RGB_LED_COUNT; ++pixel) {
        uint8_t scale = (uint8_t)((uint16_t)master_brightness * 100u / 127u);
        pio_sm_put_blocking(rgb_pio, rgb_sm,
                            pack_grb(scale_colour(pixels[pixel], scale)));
    }
    memcpy(last_sent_pixels, pixels, sizeof(pixels));
    frame_sent = true;
}

void status_rgb_set_brightness(uint8_t brightness) {
    master_brightness = brightness > 127u ? 127u : brightness;
    frame_sent = false;
}

void status_rgb_set_bank_colour(uint8_t bank, uint8_t red, uint8_t green,
                                uint8_t blue) {
    if (bank >= sizeof(bank_colours) / sizeof(bank_colours[0])) return;
    bank_colours[bank] = (rgb_t){red, green, blue};
    frame_sent = false;
}

void status_rgb_init(void) {
    uint offset = (uint)pio_add_program(rgb_pio, &rgb_tx_program);
    rgb_tx_program_init(rgb_pio, rgb_sm, offset, PIN_RGB_DATA, 800000.0f);
    gpio_set_drive_strength(PIN_RGB_DATA, GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_slew_rate(PIN_RGB_DATA, GPIO_SLEW_RATE_SLOW);
    render_rings((rgb_t){0, 0, 0}, 0, 0);
    flush();
    sleep_us(400u);
    next_frame_us = time_us_32();
}

void status_rgb_show_level(status_rgb_colour_t colour,
                           uint8_t value, uint8_t maximum) {
    if ((unsigned)colour >= sizeof(parameter_colours) / sizeof(parameter_colours[0])) {
        return;
    }
    if (value > maximum) value = maximum;
    unsigned highest_ring = maximum == 0u
        ? 0u
        : ((unsigned)value * (RGB_RING_COUNT - 1u) + maximum / 2u) / maximum;
    render_rings(parameter_colours[colour], highest_ring, 55u);
    flush();
    uint32_t now = time_us_32();
    overlay_until_us = now + RGB_OVERLAY_TIME_US;
    next_frame_us = now + RGB_FRAME_PERIOD_US;
}

void status_rgb_show_program(uint8_t program, bool pitch_bend_enabled) {
    rgb_t colour = program_colour(program, pitch_bend_enabled);
    render_rings(colour, RGB_RING_COUNT - 1u, 50u);
    if (pitch_bend_enabled) pixels[RGB_CENTRE_INDEX] = (rgb_t){44, 24, 54};
    flush();
    uint32_t now = time_us_32();
    overlay_until_us = now + RGB_OVERLAY_TIME_US;
    next_frame_us = now + RGB_FRAME_PERIOD_US;
}

void status_rgb_show_midi_mode(bool multichannel) {
    rgb_t colour = multichannel ? (rgb_t){100, 4, 2} : (rgb_t){4, 100, 12};
    render_rings(colour, RGB_RING_COUNT - 1u, 54u);
    flush();
    uint32_t now = time_us_32();
    overlay_until_us = now + RGB_OVERLAY_TIME_US;
    next_frame_us = now + RGB_FRAME_PERIOD_US;
}

void status_rgb_show_raw_mode(void) {
    render_rings((rgb_t){100, 100, 100}, RGB_RING_COUNT - 1u, 52u);
    flush();
    uint32_t now = time_us_32();
    overlay_until_us = now + RGB_OVERLAY_TIME_US;
    next_frame_us = now + RGB_FRAME_PERIOD_US;
}

void status_rgb_service(uint8_t program, bool pitch_bend_enabled, bool raw_mode,
                        float sensor_expression, float sensor_proximity,
                        uint8_t active_voices, uint8_t rhythm_density,
                        uint8_t pending_ratchets, uint32_t note_on_counter,
                        uint32_t ratchet_fire_counter,
                        int16_t amp_envelope, int16_t lfo_level) {
    uint32_t now = time_us_32();
    if ((int32_t)(now - overlay_until_us) < 0 ||
        (int32_t)(now - next_frame_us) < 0) {
        return;
    }
    next_frame_us = now + RGB_FRAME_PERIOD_US;

    if (sensor_expression < 0.0f) sensor_expression = 0.0f;
    if (sensor_expression > 1.0f) sensor_expression = 1.0f;
    if (sensor_proximity < 0.0f) sensor_proximity = 0.0f;
    if (sensor_proximity > 1.0f) sensor_proximity = 1.0f;

    if (!animation_counters_initialized) {
        last_note_on_counter = note_on_counter;
        last_ratchet_fire_counter = ratchet_fire_counter;
        animation_counters_initialized = true;
    }

    if (raw_mode) {
        uint8_t base = (uint8_t)(12.0f + sensor_proximity * 48.0f);
        uint8_t motion = (uint8_t)(sensor_expression * 26.0f);
        unsigned spark = (now / 70000u) % RGB_LED_COUNT;
        for (unsigned pixel = 0; pixel < RGB_LED_COUNT; ++pixel) {
            unsigned distance = pixel > spark ? pixel - spark : spark - pixel;
            uint8_t brightness = (uint8_t)(base +
                (distance == 0u ? motion + 24u :
                 distance == 1u ? motion / 2u : 0u));
            if (brightness > 92u) brightness = 92u;
            pixels[pixel] = scale_colour((rgb_t){100,100,100}, brightness);
        }
        flush();
        return;
    }

    unsigned bank = program / 16u;
    (void)rhythm_density;
    (void)pending_ratchets;

    // Keep the artwork visually coherent: all nine LEDs share one live hue.
    // Sensor expression moves the complete field toward the next program hue
    // rather than assigning unrelated colours to individual parameters.
    rgb_t live_colour = program_colour(program, pitch_bend_enabled);
    rgb_t transition_colour = program_colour(
        (uint8_t)(bank * 16u + ((program + 1u) & 15u)),
        pitch_bend_enabled);
    uint8_t transition = (uint8_t)(sensor_expression * 22.0f);
    live_colour = blend_colour(live_colour, transition_colour, transition);
    live_colour = intensify_colour(live_colour);

    // A genuine note onset selects the radial mask from active note count.
    // Brightness below comes directly from PRA32-U's live amp envelope, with
    // a restrained contribution from its live, patch-configured LFO.
    unsigned voice_count = active_voices > RGB_ACTIVE_PART_COUNT
        ? RGB_ACTIVE_PART_COUNT : active_voices;
    unsigned highest_ring = voice_count == 0u ? 0u
        : ((voice_count - 1u) * (RGB_RING_COUNT - 1u)) /
          (RGB_ACTIVE_PART_COUNT - 1u);
    if (note_on_counter != last_note_on_counter) {
        last_note_on_counter = note_on_counter;
        latched_highest_ring = (uint8_t)highest_ring;
    }

    if (amp_envelope < 0) amp_envelope = 0;
    if (amp_envelope > 16384) amp_envelope = 16384;
    int32_t modulation = 100 + lfo_level / 2048;
    if (modulation < 88) modulation = 88;
    if (modulation > 112) modulation = 112;
    uint32_t envelope_level =
        (uint32_t)amp_envelope * (uint32_t)modulation / 100u;
    if (envelope_level > 16384u) envelope_level = 16384u;
    if (envelope_level < 32u && voice_count == 0u) latched_highest_ring = 0u;

    for (unsigned pixel = 0; pixel < RGB_LED_COUNT; ++pixel) {
        unsigned ring = pixel_ring(pixel);
        uint8_t ring_scale = (uint8_t)(100u - ring * 7u);
        uint8_t level = ring <= latched_highest_ring
            ? (uint8_t)((envelope_level * 88u * ring_scale) /
                        (16384u * 100u))
            : 0u;
        if (level > 94u) level = 94u;
        pixels[pixel] = scale_colour(live_colour, level);
    }

    // White is reserved for actual ratchet firings, not queued ratchets.
    if (ratchet_fire_counter != last_ratchet_fire_counter) {
        last_ratchet_fire_counter = ratchet_fire_counter;
        ratchet_flash_frames = 2u;
    }
    if (ratchet_flash_frames > 0u) {
        unsigned flash_ring = voice_count == 0u ? 0u : highest_ring;
        unsigned flash_pixel = ((now / RGB_FRAME_PERIOD_US) & 1u) != 0u
            ? RGB_CENTRE_INDEX + flash_ring
            : RGB_CENTRE_INDEX - flash_ring;
        pixels[flash_pixel] = blend_colour(
            pixels[flash_pixel], (rgb_t){100,100,100}, 72u);
        --ratchet_flash_frames;
    }
    flush();
}

#else

void status_rgb_init(void) {}
void status_rgb_set_brightness(uint8_t brightness) { (void)brightness; }
void status_rgb_set_bank_colour(uint8_t bank, uint8_t red, uint8_t green,
                                uint8_t blue) {
    (void)bank; (void)red; (void)green; (void)blue;
}

void status_rgb_show_level(status_rgb_colour_t colour,
                           uint8_t value, uint8_t maximum) {
    (void)colour;
    (void)value;
    (void)maximum;
}

void status_rgb_show_program(uint8_t program, bool pitch_bend_enabled) {
    (void)program;
    (void)pitch_bend_enabled;
}

void status_rgb_show_midi_mode(bool multichannel) {
    (void)multichannel;
}

void status_rgb_show_raw_mode(void) {}

void status_rgb_service(uint8_t program, bool pitch_bend_enabled, bool raw_mode,
                        float sensor_expression, float sensor_proximity,
                        uint8_t active_voices, uint8_t rhythm_density,
                        uint8_t pending_ratchets, uint32_t note_on_counter,
                        uint32_t ratchet_fire_counter,
                        int16_t amp_envelope, int16_t lfo_level) {
    (void)program;
    (void)pitch_bend_enabled;
    (void)raw_mode;
    (void)sensor_expression;
    (void)sensor_proximity;
    (void)active_voices;
    (void)rhythm_density;
    (void)pending_ratchets;
    (void)note_on_counter;
    (void)ratchet_fire_counter;
    (void)amp_envelope;
    (void)lfo_level;
}

#endif
