#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void display_init(void);
void display_show_parameter(const char *name, int value, int minimum,
                            int maximum, unsigned program, unsigned bank,
                            bool simulated_sensor);

#ifdef __cplusplus
}
#endif
