#pragma once
#include <hardware/gpio.h>
inline void busy_wait_us_32(uint32_t delay) { mock_gpio::now += delay; }
inline uint32_t time_us_32() { return mock_gpio::now; }
inline void sleep_ms(uint32_t delay) { mock_gpio::now += delay * 1000; }
