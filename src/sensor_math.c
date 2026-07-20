#include "sensor.h"

#include <math.h>
#include <stdint.h>

#define MAX_DELTA_US 500000u

void sensor_analyze_intervals(
    const uint32_t *intervals,
    uint8_t count,
    float sensitivity,
    sensor_stats_t *out
) {
    sensor_stats_t result = {
        .minimum_us = UINT32_MAX,
        .maximum_us = 0,
    };
    float sum = 0.0f;
    for (unsigned i = 0; i < count; ++i) {
        if (intervals[i] < result.minimum_us) result.minimum_us = intervals[i];
        if (intervals[i] > result.maximum_us) result.maximum_us = intervals[i];
        sum += (float)intervals[i];
    }
    result.mean_us = sum / (float)count;

    float squared_error_sum = 0.0f;
    for (unsigned i = 0; i < count; ++i) {
        float error = (float)intervals[i] - result.mean_us;
        squared_error_sum += error * error;
    }
    result.variance = squared_error_sum / (float)(count - 1u);
    result.standard_deviation = sqrtf(result.variance);
    result.delta_us = result.maximum_us - result.minimum_us;
    result.range_fault = result.delta_us > MAX_DELTA_US;
    result.trigger = !result.range_fault &&
        ((float)result.delta_us < sensitivity * result.standard_deviation);
    *out = result;
}
