#include "sensor.h"

#include <string.h>

#include "board_pins.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/time.h"

#define EDGE_QUEUE_SIZE 32u
#define WINDOW_SIZE 10u
#define MIN_INTERVAL_US 2500u

static volatile uint32_t edge_queue[EDGE_QUEUE_SIZE];
static volatile uint8_t edge_head;
static volatile uint8_t edge_tail;
static volatile uint32_t dropped_edges;

static uint32_t intervals[WINDOW_SIZE];
static uint8_t interval_count;
static uint32_t last_timestamp;
static bool have_last_timestamp;
static bool window_ready;

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
    gpio_init(PIN_SENSOR);
    gpio_set_dir(PIN_SENSOR, GPIO_IN);
    gpio_disable_pulls(PIN_SENSOR);
    gpio_set_irq_enabled_with_callback(
        PIN_SENSOR,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
        true,
        sensor_gpio_irq
    );
}

void sensor_service(void) {
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
        if (elapsed <= MIN_INTERVAL_US) {
            continue;
        }
        last_timestamp = timestamp;

        if (!window_ready) {
            intervals[interval_count++] = elapsed;
            if (interval_count == WINDOW_SIZE) {
                interval_count = 0;
                window_ready = true;
            }
        }
    }
}

bool sensor_take_window(sensor_stats_t *out, float sensitivity) {
    if (!window_ready) {
        return false;
    }

    uint32_t local[WINDOW_SIZE];
    uint32_t irq_state = save_and_disable_interrupts();
    memcpy(local, intervals, sizeof(local));
    window_ready = false;
    restore_interrupts(irq_state);

    sensor_analyze_intervals(local, sensitivity, out);
    return true;
}

uint32_t sensor_dropped_edges(void) {
    return dropped_edges;
}
