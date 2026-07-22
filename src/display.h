#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void display_init(void);
void display_show_parameter(const char *name, int value, int minimum,
                            int maximum, unsigned program, unsigned bank,
                            bool simulated_sensor);
void display_show_menu_node(const char *name, unsigned index, unsigned count,
                            unsigned program, unsigned bank,
                            bool simulated_sensor);
void display_set_breadcrumb(const char *path);
void display_clear_band(unsigned band);
void display_screensaver_step(uint8_t phase, uint8_t motion,
                              uint8_t density, uint8_t sensor);
bool display_show_word(const char *word);

#ifdef __cplusplus
}
#endif
