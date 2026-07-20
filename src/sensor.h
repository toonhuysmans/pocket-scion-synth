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

#ifdef __cplusplus
extern "C" {
#endif

void sensor_init(void);
void sensor_configure(uint8_t window_size, uint16_t minimum_interval_us,
                      uint16_t activity_timeout_ms);
void sensor_service(void);
bool sensor_take_window(sensor_stats_t *out, float sensitivity);
void sensor_analyze_intervals(
    const uint32_t *intervals,
    uint8_t count,
    float sensitivity,
    sensor_stats_t *out
);
uint32_t sensor_dropped_edges(void);
uint32_t sensor_rejected_edges(void);
uint16_t sensor_activity_age_ms(void);
bool sensor_has_recent_activity(void);

#ifdef __cplusplus
}
#endif
