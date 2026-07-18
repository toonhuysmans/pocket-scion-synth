#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t minimum_us;
    uint32_t maximum_us;
    uint32_t delta_us;
    float mean_us;
    float variance;
    float standard_deviation;
    bool range_fault;
    bool trigger;
} sensor_stats_t;

void sensor_init(void);
void sensor_service(void);
bool sensor_take_window(sensor_stats_t *out, float sensitivity);
void sensor_analyze_intervals(
    const uint32_t intervals[10],
    float sensitivity,
    sensor_stats_t *out
);
uint32_t sensor_dropped_edges(void);
bool sensor_has_recent_activity(uint32_t maximum_age_us);
