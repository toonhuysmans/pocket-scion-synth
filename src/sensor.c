#include "sensor.h"

#include <string.h>

#include "board_pins.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/time.h"

#define EDGE_QUEUE_SIZE 32u
#define MAX_WINDOW_SIZE 24u

static volatile uint32_t edge_queue[EDGE_QUEUE_SIZE];
static volatile uint8_t edge_head;
static volatile uint8_t edge_tail;
static volatile uint32_t dropped_edges;
static uint32_t rejected_edges;

static uint32_t intervals[MAX_WINDOW_SIZE];
static uint8_t interval_count;
static uint8_t configured_window_size = 10u;
static uint16_t configured_minimum_interval_us = 2500u;
static uint32_t configured_activity_timeout_us = 1500000u;
static uint32_t last_timestamp;
static bool have_last_timestamp;
static bool window_ready;
#if PICO_RP2350
static uint32_t simulated_next_window_us;
static uint32_t simulated_phase;
#endif

static void sensor_gpio_irq(uint gpio, uint32_t events) {
    (void)gpio;
    (void)events;
    uint8_t head = edge_head;
    uint8_t next = (uint8_t)((head + 1u) & (EDGE_QUEUE_SIZE - 1u));
    if (next == edge_tail) {
        dropped_edges++;
        return;
    }
    edge_queue[head] = time_us_32();
    __compiler_memory_barrier();
    edge_head = next;
}

void sensor_init(void) {
    memset(intervals, 0, sizeof(intervals));
#if PICO_RP2350
    // A Pico 2 development unit has no Pocket SCION GPIO0 sensor attached.
    // Keep the input logically active and synthesize a gently moving window
    // below so the generative engine, voice, MIDI, and editor can be used
    // without a disconnected-input timeout.
    simulated_next_window_us = time_us_32();
    simulated_phase = 0u;
#else
    gpio_init(PIN_SENSOR);
    gpio_set_dir(PIN_SENSOR, GPIO_IN);
    gpio_disable_pulls(PIN_SENSOR);
    gpio_set_irq_enabled_with_callback(
        PIN_SENSOR,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
        true,
        sensor_gpio_irq
    );
#endif
}

void sensor_configure(uint8_t window_size, uint16_t minimum_interval_us,
                      uint16_t activity_timeout_ms) {
    if (window_size < 4u) window_size = 4u;
    if (window_size > MAX_WINDOW_SIZE) window_size = MAX_WINDOW_SIZE;
    if (minimum_interval_us < 500u) minimum_interval_us = 500u;
    if (minimum_interval_us > 10000u) minimum_interval_us = 10000u;
    if (activity_timeout_ms < 100u) activity_timeout_ms = 100u;
    if (activity_timeout_ms > 10000u) activity_timeout_ms = 10000u;

    uint32_t irq_state = save_and_disable_interrupts();
    if (configured_window_size != window_size) {
        interval_count = 0u;
        window_ready = false;
    }
    configured_window_size = window_size;
    configured_minimum_interval_us = minimum_interval_us;
    configured_activity_timeout_us = (uint32_t)activity_timeout_ms * 1000u;
    restore_interrupts(irq_state);
}

void sensor_service(void) {
#if PICO_RP2350
    // Sensor windows are generated in sensor_take_window().
    return;
#else
    unsigned processed = 0;
    while (edge_tail != edge_head && processed < 8u) {
        processed++;
        uint8_t tail = edge_tail;
        uint32_t timestamp = edge_queue[tail];
        edge_tail = (uint8_t)((tail + 1u) & (EDGE_QUEUE_SIZE - 1u));

        if (!have_last_timestamp) {
            last_timestamp = timestamp;
            have_last_timestamp = true;
            continue;
        }

        uint32_t elapsed = timestamp - last_timestamp;
        if (elapsed <= configured_minimum_interval_us) {
            rejected_edges++;
            continue;
        }
        last_timestamp = timestamp;

        if (!window_ready) {
            intervals[interval_count++] = elapsed;
            if (interval_count == configured_window_size) {
                interval_count = 0;
                window_ready = true;
            }
        }
    }
#endif
}

bool sensor_take_window(sensor_stats_t *out, float sensitivity) {
#if PICO_RP2350
    if (out == NULL) return false;
    const uint32_t now = time_us_32();
    if ((int32_t)(now - simulated_next_window_us) < 0) return false;
    simulated_next_window_us = now + 20000u;

    // Ten intervals with a slow pressure sweep and a small alternating
    // expression component. Values stay in the normal sensor range while
    // producing useful pressure, modulation, transient, and bend motion.
    uint32_t simulated[MAX_WINDOW_SIZE];
    const uint32_t sweep = (simulated_phase % 64u) * 900u;
    for (unsigned index = 0u; index < configured_window_size; ++index) {
        const uint32_t alternating = (index & 1u) != 0u ? 700u : 0u;
        simulated[index] = 18000u - sweep + alternating;
    }
    simulated_phase = (simulated_phase + 1u) % 128u;
    sensor_analyze_intervals(simulated, configured_window_size,
                             sensitivity, out);
    return true;
#else
    if (!window_ready) {
        return false;
    }

    uint32_t local[MAX_WINDOW_SIZE];
    uint32_t irq_state = save_and_disable_interrupts();
    uint8_t count = configured_window_size;
    memcpy(local, intervals, (size_t)count * sizeof(local[0]));
    window_ready = false;
    restore_interrupts(irq_state);

    sensor_analyze_intervals(local, count, sensitivity, out);
    return true;
#endif
}

uint32_t sensor_dropped_edges(void) {
    return dropped_edges;
}

uint32_t sensor_rejected_edges(void) {
    return rejected_edges;
}

uint16_t sensor_activity_age_ms(void) {
    if (!have_last_timestamp) return 16383u;
    uint32_t age_ms = (uint32_t)(time_us_32() - last_timestamp) / 1000u;
    return (uint16_t)(age_ms > 16383u ? 16383u : age_ms);
}

bool sensor_has_recent_activity(void) {
#if PICO_RP2350
    return true;
#else
    return have_last_timestamp &&
        (uint32_t)(time_us_32() - last_timestamp) <=
            configured_activity_timeout_us;
#endif
}
