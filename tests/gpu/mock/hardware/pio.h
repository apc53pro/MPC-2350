#pragma once
#include "gpio.h"
#include <deque>

#define PICO_PIO_VERSION 1
#define PICO_PIO_USE_GPIO_BASE 1
constexpr unsigned NUM_PIO_STATE_MACHINES = 4;
using uint = unsigned int;

struct pio_program {
  const uint16_t *instructions;
  unsigned length;
  int origin;
  unsigned pio_version, used_gpio_ranges;
};

struct pio_sm_config {
  unsigned wrap_start{}, wrap_end{}, side_base{}, side_count{};
  unsigned set_base{}, set_count{}, out_base{}, out_count{}, in_base{}, in_count{}, jmp_pin{};
  uint32_t divider{};
  bool in_right{}, out_right{};
};

// Execute the checked-in PIO instruction words against the same pin/FPGA
// model used by the GPIO diagnostics. This covers only this program's ops.
struct MockPio {
  unsigned base{}, pc{}, offset{}, length{}, fractional_ticks{};
  bool enabled{}, claimed[4]{}, pending{};
  uint16_t instructions[32]{};
  uint32_t tx{}, x{}, y{}, isr{}, osr{};
  std::deque<uint32_t> rx;
  pio_sm_config config;
};
using PIO = MockPio *;
inline MockPio mock_pio2;
inline PIO const pio2 = &mock_pio2;

inline pio_sm_config pio_get_default_sm_config() { return {}; }
inline void sm_config_set_wrap(pio_sm_config *c, unsigned start, unsigned end) { c->wrap_start = start; c->wrap_end = end; }
inline void sm_config_set_sideset(pio_sm_config *c, unsigned count, bool optional, bool dirs) {
  assert(!optional && !dirs); c->side_count = count;
}
inline void sm_config_set_sideset_pins(pio_sm_config *c, unsigned pin) { c->side_base = pin; }
inline void sm_config_set_set_pins(pio_sm_config *c, unsigned pin, unsigned count) { c->set_base = pin; c->set_count = count; }
inline void sm_config_set_out_pins(pio_sm_config *c, unsigned pin, unsigned count) { c->out_base = pin; c->out_count = count; }
inline void sm_config_set_out_shift(pio_sm_config *c, bool right, bool autopull, unsigned threshold) {
  assert(!autopull && threshold == 32); c->out_right = right;
}
inline void sm_config_set_in_pins(pio_sm_config *c, unsigned pin) { c->in_base = pin; }
inline void sm_config_set_in_pin_count(pio_sm_config *c, unsigned count) { c->in_count = count; }
inline void sm_config_set_in_shift(pio_sm_config *c, bool right, bool autopush, unsigned threshold) {
  assert(!autopush && threshold == 32); c->in_right = right;
}
inline void sm_config_set_jmp_pin(pio_sm_config *c, unsigned pin) { c->jmp_pin = pin; }
inline void sm_config_set_clkdiv_int_frac8(pio_sm_config *c, uint32_t integer, uint8_t fraction) {
  c->divider = integer * 256 + fraction;
}

inline bool pio_sm_is_claimed(PIO pio, unsigned sm) { return pio->claimed[sm]; }
inline unsigned pio_get_gpio_base(PIO pio) { return pio->base; }
inline int pio_claim_unused_sm(PIO pio, bool required) {
  assert(!required);
  for (unsigned sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm)
    if (!pio->claimed[sm]) { pio->claimed[sm] = true; return (int)sm; }
  return -1;
}
inline void pio_sm_unclaim(PIO pio, unsigned sm) { pio->claimed[sm] = false; }
inline int pio_set_gpio_base(PIO pio, unsigned base) {
  if (pio->length) return -1;
  pio->base = base;
  return 0;
}
inline int pio_add_program(PIO pio, const pio_program *program) {
  if (pio->length) return -1;
  assert(program->length <= 32 && program->origin == -1 && program->pio_version == 1);
  pio->length = program->length;
  pio->offset = 32 - pio->length;
  for (unsigned i = 0; i < pio->length; ++i) {
    uint16_t word = program->instructions[i];
    if ((word >> 13) == 0) word = (uint16_t)(word + pio->offset);
    pio->instructions[pio->offset + i] = word;
  }
  return (int)pio->offset;
}
inline int pio_sm_init(PIO pio, unsigned sm, unsigned pc, const pio_sm_config *config) {
  assert(sm == 0 && pio->claimed[sm] && pio->base == 16);
  assert(config->side_base == 16 && config->side_count == 2);
  assert(config->out_base == 18 && config->out_count == 8 && config->out_right);
  assert(config->in_base == 18 && config->in_count == 23 && !config->in_right);
  assert(config->set_base == 37 && config->set_count == 2 && config->jmp_pin == 39);
  assert(config->divider >= 256 && config->divider <= 0xffffffu);
  pio->config = *config; pio->pc = pc; pio->enabled = false; pio->pending = false;
  pio->x = pio->y = pio->isr = pio->osr = pio->fractional_ticks = 0; pio->rx.clear();
  return 0;
}
inline void pio_sm_set_enabled(PIO pio, unsigned sm, bool enabled) { assert(sm == 0); pio->enabled = enabled; }
inline void pio_gpio_init(PIO, unsigned pin) { gpio_set_function(pin, GPIO_FUNC_PIO2); }

inline void pio_mock_write_pins(uint64_t mask, uint64_t value, bool dirs) {
  for (; mask; mask &= mask - 1) {
    const unsigned pin = (unsigned)__builtin_ctzll(mask);
    const bool next = (value & (1ull << pin)) != 0;
    bool &current = dirs ? mock_gpio::pio_output[pin] : mock_gpio::pio_level[pin];
    if (current == next) continue;
    current = next;
    if (mock_gpio::pio_mux[pin]) mock_gpio::update_pin(pin);
  }
}
inline void pio_sm_set_pins_with_mask64(PIO pio, unsigned, uint64_t value, uint64_t mask) {
  assert(!pio->enabled); pio_mock_write_pins(mask, value, false);
}
inline void pio_sm_set_pindirs_with_mask64(PIO pio, unsigned, uint64_t value, uint64_t mask) {
  assert(!pio->enabled); pio_mock_write_pins(mask, value, true);
}

inline void pio_mock_step(PIO pio) {
  assert(pio->enabled && pio->pc >= pio->offset && pio->pc < pio->offset + pio->length);
  const auto &c = pio->config;
  const uint16_t word = pio->instructions[pio->pc];
  const unsigned opcode = word >> 13, arg = word & 0xff;
  unsigned pc = pio->pc == c.wrap_end ? c.wrap_start : pio->pc + 1;
  // Update WR_N before recording a rising HCLK edge from the same instruction.
  for (int bit = 1; bit >= 0; --bit) {
    const unsigned pin = c.side_base + (unsigned)bit;
    pio_mock_write_pins(1ull << pin, ((uint64_t)(word >> (11 + bit)) & 1u) << pin, false);
  }
  switch (opcode) {
    case 0: {
      bool take = false;
      switch (arg >> 5) {
        case 0: take = true; break;
        case 1: take = pio->x == 0; break;
        case 4: take = pio->y-- != 0; break;
        case 6: take = digitalRead(c.jmp_pin) == HIGH; break;
        default: assert(false);
      }
      if (take) pc = arg & 31;
      break;
    }
    case 2: { // IN Y, n, shifting left.
      const unsigned count = arg & 31;
      assert((arg >> 5) == 2 && count && !c.in_right);
      pio->isr = (pio->isr << count) | (pio->y & ((1u << count) - 1));
      break;
    }
    case 3: { // OUT pins, X, NULL, or PINDIRS, shifting right.
      const unsigned count = arg & 31;
      assert(count && c.out_right);
      const uint32_t bits = pio->osr & ((1u << count) - 1);
      pio->osr >>= count;
      switch (arg >> 5) {
        case 0: case 4:
          pio_mock_write_pins(((1ull << c.out_count) - 1) << c.out_base,
                              (uint64_t)bits << c.out_base, (arg >> 5) == 4);
          break;
        case 1: pio->x = bits; break;
        case 3: break;
        default: assert(false);
      }
      break;
    }
    case 4: // PULL block or PUSH block.
      assert(arg == 0xa0 || arg == 0x20);
      if (arg == 0xa0) {
        if (!pio->pending) return;
        pio->osr = pio->tx; pio->pending = false;
      } else {
        assert(pio->rx.size() < 4);
        pio->rx.push_back(pio->isr); pio->isr = 0;
      }
      break;
    case 5: {
      uint32_t value = 0;
      switch (arg & 7) {
        case 0:
          for (unsigned bit = 0; bit < c.in_count; ++bit)
            if (digitalRead(c.in_base + bit) == HIGH) value |= 1u << bit;
          break;
        case 2: value = pio->y; break;
        case 3: break;
        case 6: value = pio->isr; break;
        default: assert(false);
      }
      assert(((arg >> 3) & 3) <= 1);
      if (arg & 8) value = ~value;
      switch (arg >> 5) {
        case 2: pio->y = value; break;
        case 6: pio->isr = value; break;
        case 7: pio->osr = value; break;
        default: assert(false);
      }
      break;
    }
    case 7:
      assert((arg >> 5) == 0);
      pio_mock_write_pins(((1ull << c.set_count) - 1) << c.set_base, (uint64_t)arg << c.set_base, false);
      break;
    default: assert(false);
  }
  const unsigned cycles = 1 + ((word >> 8) & 7);
  const unsigned ticks = cycles * c.divider + pio->fractional_ticks;
  mock_gpio::advance_system_ticks(ticks / 256);
  pio->fractional_ticks = ticks % 256;
  pio->pc = pc;
}

inline void pio_sm_put(PIO pio, unsigned sm, uint32_t word) {
  assert(sm == 0 && pio->enabled && !pio->pending);
  pio->tx = word; pio->pending = true;
}
inline bool pio_sm_is_rx_fifo_empty(PIO pio, unsigned sm) {
  assert(sm == 0);
  if (pio->rx.empty()) pio_mock_step(pio);
  return pio->rx.empty();
}
inline uint32_t pio_sm_get(PIO pio, unsigned sm) {
  assert(sm == 0 && !pio->rx.empty());
  const uint32_t word = pio->rx.front();
  pio->rx.pop_front();
  return word;
}
