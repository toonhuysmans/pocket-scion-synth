#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "sensor.h"

static bool near(float left, float right, float tolerance) {
    return fabsf(left - right) <= tolerance;
}

int main(void) {
    const uint32_t ramp[10] = {
        10000, 11000, 12000, 13000, 14000,
        15000, 16000, 17000, 18000, 19000,
    };
    sensor_stats_t stats;
    sensor_analyze_intervals(ramp, 2.0f, &stats);
    assert(stats.minimum_us == 10000);
    assert(stats.maximum_us == 19000);
    assert(stats.delta_us == 9000);
    assert(near(stats.mean_us, 14500.0f, 0.01f));
    assert(near(stats.variance, 9166666.0f, 2.0f));
    assert(near(stats.standard_deviation, 3027.6504f, 0.01f));
    assert(!stats.range_fault);
    assert(!stats.trigger);

    sensor_analyze_intervals(ramp, 3.0f, &stats);
    assert(stats.trigger);

    uint32_t boundary[10] = {
        10000, 510000, 10000, 10000, 10000,
        10000, 10000, 10000, 10000, 10000,
    };
    sensor_analyze_intervals(boundary, 100.0f, &stats);
    assert(stats.delta_us == 500000);
    assert(!stats.range_fault);

    boundary[1] = 510001;
    sensor_analyze_intervals(boundary, 100.0f, &stats);
    assert(stats.delta_us == 500001);
    assert(stats.range_fault);
    assert(!stats.trigger);

    puts("sensor mathematics: all tests passed");
    return 0;
}
