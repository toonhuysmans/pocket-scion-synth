#ifndef _BOARDS_POCKET_SCION_PICO2_H
#define _BOARDS_POCKET_SCION_PICO2_H

// The Pico 2 exposes the same 0-29 GPIO numbering used by the Pocket SCION
// firmware, but selects the RP2350 ARM platform, boot block, and 4 MiB flash.
pico_board_cmake_set(PICO_PLATFORM, rp2350)
#include "boards/pico2.h"

#undef PICO_BOARD
#define PICO_BOARD "pocket_scion_pico2"

#endif
