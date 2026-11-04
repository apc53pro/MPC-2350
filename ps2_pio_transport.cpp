#include "ps2_pio_transport.h"
#include "ps2_rx.pio.h"

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pio.h>
#include <hardware/sync.h>
#include <pico/platform.h>
#include <pico/time.h>

namespace {
constexpr uint32_t kPioFrequency = 1000000u;
constexpr uint32_t kCommandTimeoutUs = 20000u;
PIO const kPio = pio1;
struct Port {
  uint8_t clock;
  uint8_t data;
  uint8_t sm;
  Ps2ReceiveCallback receive;
  bool transmitting;
  bool recovering;
  uint32_t recover_after_us;
};
Port ports[2] = {};
uint program_offset = 0;
bool ready = false;
bool flash_paused = false;

inline uint32_t rx_stall_mask(const Port &port) {
  return 1u << (PIO_FDEBUG_RXSTALL_LSB + port.sm);
}

// GPIO stays in SIO input mode for reception: PIO input sampling is independent
// of the output mux. A host drives only LOW, by changing direction, and never
// drives either open-collector PS/2 line HIGH.
inline void drive_low(uint8_t pin) {
  gpio_put(pin, false);
  gpio_set_dir(pin, GPIO_OUT);
}
inline void release_line(uint8_t pin) {
  gpio_set_dir(pin, GPIO_IN);
}

void reset_receiver(Port &port) {
  pio_sm_set_enabled(kPio, port.sm, false);
  pio_sm_clear_fifos(kPio, port.sm);
  pio_sm_restart(kPio, port.sm);
  pio_sm_exec(kPio, port.sm, pio_encode_jmp(program_offset));
  kPio->fdebug = rx_stall_mask(port);
}

void receive_error(Port &port) {
  if (port.receive) port.receive(0, false, false);
}

void start_recovery(Port &port) {
  // Do not spin in the IRQ: hold clock low now and release it from the poll
  // after a complete host-inhibit interval. This also aborts a partial byte.
  drive_low(port.clock);
  reset_receiver(port);
  __atomic_store_n(&port.recovering, true, __ATOMIC_RELEASE);
  port.recover_after_us = time_us_32() + 150u;
  receive_error(port);
}

void drain_port(Port &port) {
  if (port.transmitting || port.recovering) return;
  if (kPio->fdebug & rx_stall_mask(port)) {
    // PUSH stalled with a full FIFO. The device may have transmitted while
    // the SM was stopped, so discard the uncertain stream and start afresh.
    start_recovery(port);
    return;
  }
  while (!pio_sm_is_rx_fifo_empty(kPio, port.sm)) {
    const uint32_t word = pio_sm_get(kPio, port.sm);
    const uint16_t frame = (uint16_t)((word >> 20) & 0x7ffu);
    const uint8_t data = (uint8_t)(frame >> 1);
    const bool valid = !(frame & 1u) && (frame & (1u << 10)) &&
        ((__builtin_parity((unsigned int)data) ^ ((frame >> 9) & 1u)) != 0);
    if (!valid) {
      start_recovery(port);
      return;
    }
    if (port.receive) port.receive(data, true, (word & (1u << 31)) != 0);
  }
}

void __not_in_flash_func(receive_irq)() {
  if (!ready || flash_paused) return;
  drain_port(ports[0]);
  drain_port(ports[1]);
}

bool wait_line(uint8_t pin, bool high, uint32_t deadline) {
  while (gpio_get(pin) != high) {
    if ((int32_t)(time_us_32() - deadline) >= 0) return false;
    tight_loop_contents();
  }
  return true;
}

void configure_receiver(Port &port, uint8_t clock, uint8_t data,
                        uint sm, Ps2ReceiveCallback callback) {
  port = {clock, data, (uint8_t)sm, callback, false, false, 0u};
  gpio_init(clock);
  gpio_init(data);
  gpio_pull_up(clock);
  gpio_pull_up(data);
  release_line(clock);
  release_line(data);
  pio_sm_config config = ps2_rx_program_get_default_config(program_offset);
  sm_config_set_in_pins(&config, data);
  sm_config_set_jmp_pin(&config, clock);
  sm_config_set_in_shift(&config, true, false, 32);
  sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
  sm_config_set_clkdiv(&config,
      (float)clock_get_hz(clk_sys) / (float)kPioFrequency);
  pio_sm_init(kPio, sm, program_offset, &config);
  kPio->fdebug = rx_stall_mask(port);
}
} // namespace

bool ps2_pio_begin(uint8_t keyboard_clock, uint8_t keyboard_data,
                   uint8_t mouse_clock, uint8_t mouse_data,
                   Ps2ReceiveCallback keyboard_callback,
                   Ps2ReceiveCallback mouse_callback) {
  if (get_core_num() != 1u) return false;
  if (ready) return true;
  const uint8_t pins[] = {keyboard_clock, keyboard_data, mouse_clock, mouse_data};
  for (uint i = 0; i < 4; ++i) {
    if (pins[i] >= 32u) return false;
    for (uint j = 0; j < i; ++j) if (pins[i] == pins[j]) return false;
  }
  if (!pio_can_add_program(kPio, &ps2_rx_program)) return false;
  const int keyboard_sm = pio_claim_unused_sm(kPio, false);
  if (keyboard_sm < 0) return false;
  const int mouse_sm = pio_claim_unused_sm(kPio, false);
  if (mouse_sm < 0) {
    pio_sm_unclaim(kPio, (uint)keyboard_sm);
    return false;
  }
  program_offset = pio_add_program(kPio, &ps2_rx_program);
  configure_receiver(ports[0], keyboard_clock, keyboard_data,
                     (uint)keyboard_sm, keyboard_callback);
  configure_receiver(ports[1], mouse_clock, mouse_data,
                     (uint)mouse_sm, mouse_callback);
  irq_set_exclusive_handler(PIO1_IRQ_0, receive_irq);
  pio_set_irq0_source_enabled(kPio,
      pio_get_rx_fifo_not_empty_interrupt_source((uint)keyboard_sm), true);
  pio_set_irq0_source_enabled(kPio,
      pio_get_rx_fifo_not_empty_interrupt_source((uint)mouse_sm), true);
  __atomic_store_n(&ready, true, __ATOMIC_RELEASE);
  irq_set_enabled(PIO1_IRQ_0, true);
  pio_enable_sm_mask_in_sync(kPio, (1u << keyboard_sm) | (1u << mouse_sm));
  return true;
}

void ps2_pio_poll() {
  if (!ready || flash_paused || get_core_num() != 1u) return;
  const uint32_t irq_state = save_and_disable_interrupts();
  for (Port &port : ports) {
    if (port.recovering &&
        (int32_t)(time_us_32() - port.recover_after_us) >= 0) {
      reset_receiver(port);
      __atomic_store_n(&port.recovering, false, __ATOMIC_RELEASE);
      pio_sm_set_enabled(kPio, port.sm, true);
      release_line(port.clock);
    }
  }
  drain_port(ports[0]);
  drain_port(ports[1]);
  restore_interrupts(irq_state);
}

bool ps2_pio_send_byte(Ps2Port which, uint8_t byte) {
  const uint index = (uint)which;
  if (!ready || flash_paused || index >= 2u || get_core_num() != 1u) return false;
  Port &port = ports[index];
  if (port.transmitting || port.recovering) return false;
  const uint32_t deadline = time_us_32() + kCommandTimeoutUs;
  uint32_t irq_state = save_and_disable_interrupts();
  drain_port(port);
  if (port.recovering) {
    restore_interrupts(irq_state);
    return false;
  }
  __atomic_store_n(&port.transmitting, true, __ATOMIC_RELEASE);
  drive_low(port.clock);
  reset_receiver(port);
  restore_interrupts(irq_state);

  busy_wait_us_32(150u); // Host inhibit/request-to-send (minimum 100 us).
  drive_low(port.data); // Start bit before releasing clock to the device.
  busy_wait_us_32(10u);
  release_line(port.clock);
  bool ok = wait_line(port.clock, false, deadline);
  uint8_t parity = 1u;
  for (uint bit = 0; ok && bit < 8u; ++bit) {
    const bool high = (byte & (1u << bit)) != 0;
    if (high) release_line(port.data); else drive_low(port.data);
    parity ^= (uint8_t)high;
    ok = wait_line(port.clock, true, deadline) &&
         wait_line(port.clock, false, deadline);
  }
  if (ok) {
    if (parity) release_line(port.data); else drive_low(port.data);
    ok = wait_line(port.clock, true, deadline) &&
         wait_line(port.clock, false, deadline);
  }
  release_line(port.data); // Stop bit, then device's low ACK bit.
  if (ok) ok = wait_line(port.clock, true, deadline) &&
               wait_line(port.clock, false, deadline);
  if (ok) ok = wait_line(port.data, false, deadline);
  if (ok) ok = wait_line(port.clock, true, deadline);
  // Inhibit immediately after the ACK clock so the protocol response cannot
  // begin in the short interval before the receive SM is restarted.
  drive_low(port.clock);
  if (ok) ok = wait_line(port.data, true, deadline);

  irq_state = save_and_disable_interrupts();
  release_line(port.data);
  reset_receiver(port);
  __atomic_store_n(&port.transmitting, false, __ATOMIC_RELEASE);
  if (!ok) start_recovery(port);
  else {
    pio_sm_set_enabled(kPio, port.sm, true);
    release_line(port.clock);
  }
  restore_interrupts(irq_state);
  return ok;
}

void ps2_pio_flash_pause() {
  if (!ready || flash_paused || get_core_num() != 1u) return;
  const uint32_t irq_state = save_and_disable_interrupts();
  drain_port(ports[0]);
  drain_port(ports[1]);
  __atomic_store_n(&flash_paused, true, __ATOMIC_RELEASE);
  for (Port &port : ports) {
    drive_low(port.clock);
    reset_receiver(port);
  }
  restore_interrupts(irq_state);
  // A device interrupted in mid-byte will retransmit when clock is released.
  // Ensure this is recognized as host inhibit even for a short flash program.
  busy_wait_us_32(150u);
}

void ps2_pio_flash_resume() {
  if (!ready || !flash_paused || get_core_num() != 1u) return;
  const uint32_t irq_state = save_and_disable_interrupts();
  for (Port &port : ports) {
    reset_receiver(port);
    __atomic_store_n(&port.recovering, false, __ATOMIC_RELEASE);
    receive_error(port);
    pio_sm_set_enabled(kPio, port.sm, true);
  }
  __atomic_store_n(&flash_paused, false, __ATOMIC_RELEASE);
  for (Port &port : ports) release_line(port.clock);
  restore_interrupts(irq_state);
}

void ps2_pio_get_diagnostics(Ps2PioDiagnostics &out) {
  out = {};
  out.ready = __atomic_load_n(&ready, __ATOMIC_ACQUIRE);
  if(!out.ready) return;
  out.flash_paused = __atomic_load_n(&flash_paused, __ATOMIC_ACQUIRE);
  out.ctrl = kPio->ctrl;
  out.fdebug = kPio->fdebug;
  out.irq_enable = kPio->irq_ctrl[0].inte;
  out.irq_pending = kPio->irq_ctrl[0].ints;
  out.program_offset = program_offset;
  for(uint i = 0; i < 2; ++i) {
    const Port &port = ports[i];
    Ps2PioPortDiagnostics &d = out.port[i];
    d.sm = port.sm;
    d.pc = pio_sm_get_pc(kPio, port.sm);
    d.rx_level = pio_sm_get_rx_fifo_level(kPio, port.sm);
    d.clkdiv = kPio->sm[port.sm].clkdiv;
    d.execctrl = kPio->sm[port.sm].execctrl;
    d.pinctrl = kPio->sm[port.sm].pinctrl;
    d.clock_pin = port.clock; d.data_pin = port.data;
    d.clock_high = gpio_get(port.clock); d.data_high = gpio_get(port.data);
    d.clock_output = gpio_get_dir(port.clock); d.data_output = gpio_get_dir(port.data);
    d.clock_function = (uint32_t)gpio_get_function(port.clock);
    d.data_function = (uint32_t)gpio_get_function(port.data);
    d.transmitting = __atomic_load_n(&port.transmitting, __ATOMIC_ACQUIRE);
    d.recovering = __atomic_load_n(&port.recovering, __ATOMIC_ACQUIRE);
  }
}
