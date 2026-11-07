#pragma once
#include <stdint.h>

enum clock_index { clk_sys };
inline uint32_t clock_get_hz(clock_index) { return 375000000; }
