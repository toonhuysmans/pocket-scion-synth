#pragma once

#include "controls.h"
#include "synth.h"

#if PICO_RP2350
void pico2_menu_init(void);
void pico2_menu_show(synth_t *synth);
bool pico2_menu_handle(synth_t *synth, control_event_t event);
#endif
