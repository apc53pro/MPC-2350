#pragma once
#include <Arduino.h>

enum gpio_function_t { GPIO_FUNC_SIO, GPIO_FUNC_PIO2 };

inline void gpio_set_function(unsigned pin, gpio_function_t function) {
  mock_gpio::pio_mux[pin] = function == GPIO_FUNC_PIO2;
  mock_gpio::update_pin(pin);
}

inline void gpio_put_masked64(uint64_t mask, uint64_t value) {
  for (unsigned pin = 0; pin < __GPIOCNT; ++pin)
    if (mask & (1ull << pin)) digitalWrite(pin, value & (1ull << pin) ? HIGH : LOW);
}

inline void gpio_set_dir_masked64(uint64_t mask, uint64_t value) {
  for (unsigned pin = 0; pin < __GPIOCNT; ++pin) {
    if (!(mask & (1ull << pin))) continue;
    mock_gpio::sio_output[pin] = (value & (1ull << pin)) != 0;
    if (!mock_gpio::pio_mux[pin]) mock_gpio::update_pin(pin);
  }
}
