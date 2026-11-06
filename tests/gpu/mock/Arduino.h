#pragma once
#include <stdint.h>
#include <cassert>
#include <vector>

#define __GPIOCNT 48
enum PinStatus { LOW = 0, HIGH = 1 };
enum PinMode { INPUT, OUTPUT, OUTPUT_12MA };

namespace mock_gpio {
inline bool level[48]{}, output[48]{};
inline unsigned drive[48]{};
inline bool ready = false, valid = false;
inline uint8_t status[128]{};
inline unsigned address = 0, reset_clock_edges = 0;
inline uint8_t last_data_write = 0;
inline uint32_t now = 0;
inline uint32_t first_clock_us = UINT32_MAX, configuration_ready_us = 0;
inline uint32_t last_clock_edge_us = UINT32_MAX, min_clock_phase_us = UINT32_MAX;
inline uint32_t write_assert_us = 0, read_assert_us = 0;
inline uint32_t min_write_setup_us = UINT32_MAX, min_read_setup_us = UINT32_MAX;
inline std::vector<bool> reset_writes;

inline void set_level(unsigned pin, bool value) {
  if (pin == 36) reset_writes.push_back(value);
  if (pin == 17 && !value && level[17]) write_assert_us = now;
  if (pin == 38 && !value && level[38]) read_assert_us = now;
  if (pin == 16 && value != level[16]) {
    if (last_clock_edge_us != UINT32_MAX && now - last_clock_edge_us < min_clock_phase_us)
      min_clock_phase_us = now - last_clock_edge_us;
    last_clock_edge_us = now;
  }
  if (pin == 16 && value && !level[16]) {
    if (first_clock_us == UINT32_MAX) first_clock_us = now;
    if (!level[36]) ++reset_clock_edges;
    if (!level[17] && now - write_assert_us < min_write_setup_us)
      min_write_setup_us = now - write_assert_us;
    if (!level[38] && now - read_assert_us < min_read_setup_us)
      min_read_setup_us = now - read_assert_us;
    if (level[36] && ready && now >= configuration_ready_us && !level[17] && level[38]) {
      uint8_t byte = 0;
      for (unsigned i = 0; i < 8; ++i) {
        assert(output[18 + i]);
        byte |= (uint8_t)(level[18 + i] << i);
      }
      if (!level[37]) address = byte;
      else last_data_write = byte;
    }
  }
  level[pin] = value;
}
}

inline void pinMode(unsigned pin, PinMode mode) {
  assert(pin < __GPIOCNT);
  // Match installed Arduino-Pico: gpio_init clears the output latch, so
  // pinMode(OUTPUT) after digitalWrite(HIGH) would lose the HIGH value.
  mock_gpio::output[pin] = mode != INPUT;
  if (mode != INPUT) mock_gpio::drive[pin] = mode == OUTPUT_12MA ? 12 : 4;
  mock_gpio::set_level(pin, false);
}
inline void digitalWrite(unsigned pin, PinStatus value) {
  assert(pin < __GPIOCNT);
  mock_gpio::set_level(pin, value == HIGH);
}
inline PinStatus digitalRead(unsigned pin) {
  assert(pin < __GPIOCNT);
  const bool configured = mock_gpio::now >= mock_gpio::configuration_ready_us;
  bool value;
  if (pin >= 18 && pin < 26 && !mock_gpio::output[pin] &&
      !mock_gpio::level[38] && mock_gpio::level[17] && mock_gpio::level[37])
    value = ((mock_gpio::status[mock_gpio::address] >> (pin - 18)) & 1u) != 0;
  else if (pin == 39) value = configured && mock_gpio::ready;
  else if (pin == 40) value = configured && mock_gpio::valid;
  else value = mock_gpio::level[pin];
  return value ? HIGH : LOW;
}
inline void delayMicroseconds(uint32_t us) { mock_gpio::now += us; }
inline void delay(uint32_t ms) { mock_gpio::now += ms * 1000; }
inline uint32_t micros() { return mock_gpio::now; }
