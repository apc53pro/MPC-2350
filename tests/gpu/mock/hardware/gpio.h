#pragma once
#include <stdint.h>
#include <vector>

#define NUM_BANK0_GPIOS 48
constexpr bool GPIO_OUT = true;
constexpr unsigned GPIO_DRIVE_STRENGTH_12MA = 12;
namespace mock_gpio {
inline bool level[48]{}, output[48]{};
inline bool ready = false, valid = false;
inline uint8_t status[128]{};
inline unsigned address = 0, reset_clock_edges = 0;
inline uint32_t now = 0;
inline uint32_t first_clock_us = UINT32_MAX, configuration_ready_us = 0;
inline std::vector<bool> reset_writes;
}
inline void gpio_init(unsigned pin) { mock_gpio::level[pin] = false; mock_gpio::output[pin] = false; }
inline void gpio_put(unsigned pin, bool value) {
  using namespace mock_gpio;
  if (pin == 36) reset_writes.push_back(value);
  if (pin == 16 && value && !level[16]) {
    if (first_clock_us == UINT32_MAX) first_clock_us = now;
    if (!level[36]) ++reset_clock_edges;
    if (!level[17] && !level[37] && level[38]) {
      address = 0;
      for (unsigned i = 0; i < 8; ++i) address |= (unsigned)level[18 + i] << i;
    }
  }
  level[pin] = value;
}
inline bool gpio_get(unsigned pin) {
  const bool configured = mock_gpio::now >= mock_gpio::configuration_ready_us;
  return pin == 39 ? (configured && mock_gpio::ready)
       : pin == 40 ? (configured && mock_gpio::valid) : mock_gpio::level[pin];
}
inline uint32_t gpio_get_all() { return (uint32_t)mock_gpio::status[mock_gpio::address] << 18; }
inline void gpio_put_masked(uint32_t mask, uint32_t value) {
  for (unsigned pin = 0; pin < 32; ++pin)
    if (mask & (1u << pin)) mock_gpio::level[pin] = (value & (1u << pin)) != 0;
}
inline void gpio_set_dir(unsigned pin, bool output) { mock_gpio::output[pin] = output; }
inline void gpio_set_dir_in_masked(uint32_t mask) {
  for (unsigned pin = 0; pin < 32; ++pin) if (mask & (1u << pin)) mock_gpio::output[pin] = false;
}
inline void gpio_set_dir_out_masked(uint32_t mask) {
  for (unsigned pin = 0; pin < 32; ++pin) if (mask & (1u << pin)) mock_gpio::output[pin] = true;
}
inline void gpio_set_drive_strength(unsigned, unsigned) {}
inline void gpio_disable_pulls(unsigned) {}
