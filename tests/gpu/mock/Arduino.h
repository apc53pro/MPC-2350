#pragma once
#include <stdint.h>
#include <cassert>
#include <vector>

#define __GPIOCNT 48
enum PinStatus { LOW = 0, HIGH = 1 };
enum PinMode { INPUT, OUTPUT, OUTPUT_12MA };

namespace mock_gpio {
inline bool level[48]{}, output[48]{};
inline bool sio_level[48]{}, sio_output[48]{};
inline bool pio_level[48]{}, pio_output[48]{}, pio_mux[48]{};
inline unsigned drive[48]{};
inline bool ready = false, valid = false;
inline bool valid_follows_clock = false;
inline bool swap_d5_d6 = false;
inline uint8_t status[128]{};
inline unsigned address = 0, reset_clock_edges = 0;
inline unsigned clock_rises = 0;
inline unsigned write_wait_edges = 0, packet_writes = 0;
// Like nano_host_link: RVALID needs a read edge; register 1 waits for a
// core-clock snapshot handshake (4 HCLK rising edges in RTL simulation).
inline unsigned read_rises = 0, status_refresh_rises = 4;
// Address-latch fault models: latch only the first address after a reset,
// or force bits seen by the FPGA as (byte & latch_and) | latch_or.
inline bool latch_first_only = false, latched_since_reset = false;
inline uint8_t latch_and = 0xff, latch_or = 0;
inline uint8_t last_data_write = 0;
inline uint32_t now = 0;
inline uint32_t sub_us_ticks = 0;
inline uint32_t first_clock_us = UINT32_MAX, configuration_ready_us = 0;
inline uint64_t last_clock_edge_ticks = UINT64_MAX, min_clock_phase_ticks = UINT64_MAX;
inline uint64_t last_rise_ticks = UINT64_MAX, min_clock_period_ticks = UINT64_MAX;
inline uint64_t write_assert_ticks = 0, read_assert_ticks = 0;
inline uint64_t min_write_setup_ticks = UINT64_MAX, min_read_setup_ticks = UINT64_MAX;
inline std::vector<bool> reset_writes;

constexpr uint32_t ticks_per_us() { return 375; }
inline uint64_t clock_ticks() { return (uint64_t)now * ticks_per_us() + sub_us_ticks; }
inline void advance_system_ticks(unsigned ticks) {
  sub_us_ticks += ticks;
  const unsigned ticks_per_microsecond = ticks_per_us();
  if (sub_us_ticks >= ticks_per_microsecond) {
    ++now;
    sub_us_ticks -= ticks_per_microsecond;
  }
}

inline uint8_t wire_byte(uint8_t value) {
  if (!swap_d5_d6) return value;
  return (uint8_t)((value & ~0x60u) | ((value & 0x20u) << 1) | ((value & 0x40u) >> 1));
}

inline void set_level(unsigned pin, bool value) {
  if (pin == 36) reset_writes.push_back(value);
  if (pin == 36 && !value) latched_since_reset = false;
  if (pin == 17 && !value && level[17]) write_assert_ticks = clock_ticks();
  if (pin == 38 && !value && level[38]) { read_assert_ticks = clock_ticks(); read_rises = 0; }
  if (pin == 16 && value != level[16]) {
    const uint64_t ticks = clock_ticks();
    if (last_clock_edge_ticks != UINT64_MAX && ticks - last_clock_edge_ticks < min_clock_phase_ticks)
      min_clock_phase_ticks = ticks - last_clock_edge_ticks;
    last_clock_edge_ticks = ticks;
  }
  if (pin == 16 && value && !level[16]) {
    const uint64_t ticks = clock_ticks();
    if (last_rise_ticks != UINT64_MAX && ticks - last_rise_ticks < min_clock_period_ticks)
      min_clock_period_ticks = ticks - last_rise_ticks;
    last_rise_ticks = ticks;
    ++clock_rises;
    if (first_clock_us == UINT32_MAX) first_clock_us = now;
    if (!level[36]) ++reset_clock_edges;
    if (!level[17] && ticks - write_assert_ticks < min_write_setup_ticks)
      min_write_setup_ticks = ticks - write_assert_ticks;
    if (!level[38]) {
      assert(level[17] && level[37]);
      for (unsigned bit = 0; bit < 8; ++bit) assert(!output[18 + bit]);
      ++read_rises;
    }
    if (!level[38] && ticks - read_assert_ticks < min_read_setup_ticks)
      min_read_setup_ticks = ticks - read_assert_ticks;
    if (level[36] && ready && now >= configuration_ready_us && !level[17] && level[38]) {
      uint8_t byte = 0;
      for (unsigned i = 0; i < 8; ++i) {
        assert(output[18 + i]);
        byte |= (uint8_t)(level[18 + i] << i);
      }
      if (write_wait_edges) --write_wait_edges;
      else if (!level[37]) {
        if (!latch_first_only || !latched_since_reset)
          address = (uint8_t)((wire_byte(byte) & latch_and) | latch_or);
        latched_since_reset = true;
      }
      else { last_data_write = wire_byte(byte); ++packet_writes; }
    }
  }
  level[pin] = value;
}

inline void update_pin(unsigned pin) {
  output[pin] = pio_mux[pin] ? pio_output[pin] : sio_output[pin];
  set_level(pin, pio_mux[pin] ? pio_level[pin] : sio_level[pin]);
}
}

inline void pinMode(unsigned pin, PinMode mode) {
  assert(pin < __GPIOCNT);
  // Match installed Arduino-Pico: gpio_init clears the output latch, so
  // pinMode(OUTPUT) after digitalWrite(HIGH) would lose the HIGH value.
  mock_gpio::pio_mux[pin] = false;
  mock_gpio::sio_output[pin] = mode != INPUT;
  if (mode != INPUT) mock_gpio::drive[pin] = mode == OUTPUT_12MA ? 12 : 4;
  mock_gpio::sio_level[pin] = false;
  mock_gpio::update_pin(pin);
}
inline void digitalWrite(unsigned pin, PinStatus value) {
  assert(pin < __GPIOCNT);
  mock_gpio::sio_level[pin] = value == HIGH;
  if (!mock_gpio::pio_mux[pin]) mock_gpio::update_pin(pin);
}
inline PinStatus digitalRead(unsigned pin) {
  assert(pin < __GPIOCNT);
  const bool configured = mock_gpio::now >= mock_gpio::configuration_ready_us;
  bool value;
  if (pin >= 18 && pin < 26 && !mock_gpio::output[pin] &&
      !mock_gpio::level[38] && mock_gpio::level[17] && mock_gpio::level[37])
    value = ((mock_gpio::wire_byte(mock_gpio::status[mock_gpio::address]) >> (pin - 18)) & 1u) != 0;
  else if (pin == 39) value = configured && mock_gpio::ready && mock_gpio::level[38] && !mock_gpio::write_wait_edges;
  else if (pin == 40) value = mock_gpio::valid_follows_clock ? mock_gpio::level[16] :
      configured && mock_gpio::valid && !mock_gpio::level[38] &&
      mock_gpio::level[17] && mock_gpio::level[37] &&
      mock_gpio::read_rises >= (mock_gpio::address == 1 ? mock_gpio::status_refresh_rises : 1u);
  else value = mock_gpio::level[pin];
  return value ? HIGH : LOW;
}
inline void delayMicroseconds(uint32_t us) { mock_gpio::now += us; }
inline void delay(uint32_t ms) { mock_gpio::now += ms * 1000; }
inline uint32_t micros() { return mock_gpio::now; }
inline uint32_t millis() { return mock_gpio::now / 1000; }
