#pragma once
#include <cstdint>
#include <deque>
using uint = unsigned;
#define PICO_NO_HARDWARE 0
#define PICO_PIO_VERSION 1
#define __not_in_flash_func(name) name
#define PIO_FDEBUG_RXSTALL_LSB 0
#define PIO1_IRQ_0 17
#define PIO_FIFO_JOIN_RX 2
#define GPIO_IN false
#define GPIO_OUT true
#define clk_sys 0
struct W1C {
  uint32_t value = 0;
  operator uint32_t() const { return value; }
  W1C& operator=(uint32_t mask) { value &= ~mask; return *this; }
};
struct PioMock {
  uint32_t ctrl = 0;
  struct { uint32_t inte = 0, ints = 0; } irq_ctrl[2];
  struct { uint32_t pc = 0, clkdiv = 0, execctrl = 0, pinctrl = 0; } sm[4];
  W1C fdebug;
  uint claims = 0;
  bool enabled[4] = {};
  std::deque<uint32_t> fifo[4];
};
using PIO = PioMock*;
inline PioMock mock_pio;
#define pio1 (&mock_pio)
struct pio_sm_config {};
struct pio_program {
  const uint16_t* instructions;
  uint8_t length;
  int8_t origin;
  uint8_t pio_version;
  uint8_t used_gpio_ranges;
};
enum pio_interrupt_source_t { rx0, rx1, rx2, rx3 };
inline pio_sm_config pio_get_default_sm_config() { return {}; }
inline void sm_config_set_wrap(pio_sm_config*, uint, uint) {}
inline void sm_config_set_in_pins(pio_sm_config*, uint) {}
inline void sm_config_set_jmp_pin(pio_sm_config*, uint) {}
inline void sm_config_set_in_shift(pio_sm_config*, bool, bool, uint) {}
inline void sm_config_set_fifo_join(pio_sm_config*, uint) {}
inline void sm_config_set_clkdiv(pio_sm_config*, float) {}
inline uint32_t clock_get_hz(int) { return 408000000u; }
inline void pio_sm_set_enabled(PIO p, uint sm, bool value) {
  p->enabled[sm] = value;
  if(value) p->ctrl |= 1u << sm; else p->ctrl &= ~(1u << sm);
}
inline void pio_sm_clear_fifos(PIO p, uint sm) { p->fifo[sm].clear(); }
inline void pio_sm_restart(PIO, uint) {}
inline void pio_sm_exec(PIO p, uint sm, uint16_t op) { p->sm[sm].pc = op & 31u; }
inline uint8_t pio_sm_get_pc(PIO p, uint sm) { return (uint8_t)p->sm[sm].pc; }
inline uint pio_sm_get_rx_fifo_level(PIO p, uint sm) { return (uint)p->fifo[sm].size(); }
inline uint16_t pio_encode_jmp(uint target) { return (uint16_t)target; }
inline bool pio_sm_is_rx_fifo_empty(PIO p, uint sm) { return p->fifo[sm].empty(); }
inline uint32_t pio_sm_get(PIO p, uint sm) {
  const auto value = p->fifo[sm].front(); p->fifo[sm].pop_front(); return value;
}
inline bool pio_can_add_program(PIO, const pio_program*) { return true; }
inline int pio_claim_unused_sm(PIO p, bool) {
  for (uint i = 0; i < 4; ++i) if (!(p->claims & (1u << i))) {
    p->claims |= 1u << i; return (int)i;
  }
  return -1;
}
inline void pio_sm_unclaim(PIO p, uint sm) { p->claims &= ~(1u << sm); }
inline uint pio_add_program(PIO, const pio_program*) { return 0; }
inline void pio_sm_init(PIO, uint, uint, const pio_sm_config*) {}
inline void pio_set_irq0_source_enabled(PIO p, pio_interrupt_source_t source, bool enabled) {
  if(enabled) p->irq_ctrl[0].inte |= 1u << source;
  else p->irq_ctrl[0].inte &= ~(1u << source);
}
inline pio_interrupt_source_t pio_get_rx_fifo_not_empty_interrupt_source(uint sm) {
  return (pio_interrupt_source_t)sm;
}
inline void pio_enable_sm_mask_in_sync(PIO p, uint mask) {
  for (uint i = 0; i < 4; ++i) if (mask & (1u << i)) pio_sm_set_enabled(p, i, true);
}
inline uint get_core_num() { return 1; }
void gpio_init(uint8_t);
void gpio_pull_up(uint8_t);
void gpio_put(uint8_t, bool);
void gpio_set_dir(uint8_t, bool);
bool gpio_get(uint8_t);
bool gpio_get_dir(uint8_t);
uint32_t gpio_get_function(uint8_t);
uint32_t time_us_32();
void busy_wait_us_32(uint32_t);
void tight_loop_contents();
uint32_t save_and_disable_interrupts();
void restore_interrupts(uint32_t);
void irq_set_exclusive_handler(uint, void (*)());
void irq_set_enabled(uint, bool);
