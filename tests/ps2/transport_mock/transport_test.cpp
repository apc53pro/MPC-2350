#include <cassert>
#include <cstdio>
#include <vector>
#include "mock_hardware.h"

namespace mock {
enum Mode { Normal, Absent, NoAck };
Mode mode;
uint32_t now;
bool output[32], value[32];
bool irq_enabled, irq_pending;
bool check_wait_irqs;
void (*handler)();
bool started, response_sent;
uint32_t first_fall;
std::vector<int> host_bits;
std::vector<uint8_t> received;
std::vector<bool> idle_markers;
int receive_errors;
bool device_clock() {
  if (!started || now < first_fall || now >= first_fall + 770) return true;
  return ((now - first_fall) % 70) >= 35;
}
bool device_data() {
  return mode != Normal || !started || now < first_fall + 700 || now >= first_fall + 770;
}
void pump_irq() {
  if (irq_enabled && irq_pending && handler) {
    irq_pending = false;
    handler();
  }
}
void advance(uint32_t duration) {
  if (check_wait_irqs && duration) assert(irq_enabled);
  for (uint32_t i = 0; i < duration; ++i) {
    ++now;
    if (started && now >= first_fall && now < first_fall + 700 &&
        (now - first_fall) % 70 == 35) {
      host_bits.push_back(!(output[26] && !value[26]));
    }
  }
}
void enqueue(uint sm, uint8_t byte, bool after_idle = false) {
  const uint parity = 1 ^ (__builtin_parity((uint)byte));
  const uint frame = ((uint)byte << 1) | (parity << 9) | 1024;
  mock_pio.fifo[sm].push_back((frame << 20) | (after_idle ? 1u << 31 : 0u));
  irq_pending = true;
  pump_irq();
}
} // namespace mock

void gpio_init(uint8_t pin) { mock::output[pin] = false; mock::value[pin] = false; }
void gpio_pull_up(uint8_t) {}
void gpio_put(uint8_t pin, bool value) {
  // The real transport must never drive PS/2 HIGH.
  assert(!value);
  mock::value[pin] = value;
}
void gpio_set_dir(uint8_t pin, bool output) {
  const bool was_output = mock::output[pin];
  mock::output[pin] = output;
  if (pin == 1 && was_output && !output) {
    if (!mock::started && mock::output[26] && mock::mode != mock::Absent) {
      mock::started = true;
      mock::first_fall = mock::now + 40;
    } else if (mock::started && mock::now >= mock::first_fall + 770 && !mock::response_sent) {
      // Deliver FA immediately at clock release to stress the receiver-arm race.
      assert(mock_pio.enabled[1]);
      mock::response_sent = true;
      mock::enqueue(1, 0xfa);
    }
  }
}
bool gpio_get(uint8_t pin) {
  if (mock::output[pin] && !mock::value[pin]) return false;
  if (pin == 1) return mock::device_clock();
  if (pin == 26) return mock::device_data();
  return true;
}
uint32_t time_us_32() { return mock::now; }
bool gpio_get_dir(uint8_t pin) { return mock::output[pin]; }
uint32_t gpio_get_function(uint8_t) { return 5; } // GPIO_FUNC_SIO
void busy_wait_us_32(uint32_t us) { mock::advance(us); }
void tight_loop_contents() { mock::advance(1); }
uint32_t save_and_disable_interrupts() {
  const bool old = mock::irq_enabled;
  mock::irq_enabled = false;
  return old;
}
void restore_interrupts(uint32_t previous) { mock::irq_enabled = previous; mock::pump_irq(); }
void irq_set_exclusive_handler(uint, void (*handler)()) { mock::handler = handler; }
void irq_set_enabled(uint, bool enabled) { mock::irq_enabled = enabled; mock::pump_irq(); }

#include "ps2_pio_transport.cpp"

void callback(uint8_t byte, bool valid, bool after_idle) {
  if (valid) {
    mock::received.push_back(byte);
    mock::idle_markers.push_back(after_idle);
  } else ++mock::receive_errors;
}
void fresh(mock::Mode mode = mock::Normal) {
  mock::mode = mode;
  mock::now = 0;
  for (uint i = 0; i < 32; ++i) mock::output[i] = mock::value[i] = false;
  mock::irq_enabled = mock::irq_pending = false;
  mock::check_wait_irqs = false;
  mock::handler = nullptr;
  mock::started = mock::response_sent = false;
  mock::host_bits.clear(); mock::received.clear(); mock::receive_errors = 0;
  mock::idle_markers.clear();
  mock_pio = PioMock{};
  ready = flash_paused = false;
  assert(ps2_pio_begin(28, 27, 1, 26, callback, callback));
}

int main() {
  fresh();
  irq_set_enabled(PIO1_IRQ_0, false);
  mock::enqueue(1, 0x38, true);
  mock::enqueue(1, 0xff);
  mock::enqueue(1, 0x08, true);
  irq_set_enabled(PIO1_IRQ_0, true);
  assert((mock::received == std::vector<uint8_t>{0x38, 0xff, 0x08}));
  assert((mock::idle_markers == std::vector<bool>{true, false, true}));

  for (uint byte = 0; byte < 256; ++byte) {
    fresh();
    mock::check_wait_irqs = true;
    assert(ps2_pio_send_byte(Ps2Port::Mouse, (uint8_t)byte));
    assert(mock::host_bits.size() == 10); // Eight data, parity, stop.
    for (uint bit = 0; bit < 8; ++bit) assert(mock::host_bits[bit] == (int)((byte >> bit) & 1));
    assert(mock::host_bits[8] == (1 ^ __builtin_parity(byte)));
    assert(mock::host_bits[9] == 1);
    assert(mock::receive_errors == 0);
    assert(mock::received == std::vector<uint8_t>{0xfa});
    assert(mock::now < 20000);
    assert(!mock::output[1] && !mock::output[26]);
  }

  for (auto mode : {mock::Absent, mock::NoAck}) {
    fresh(mode);
    mock::check_wait_irqs = true;
    assert(!ps2_pio_send_byte(Ps2Port::Mouse, 0xf4));
    assert(mock::now >= 20000 && mock::now <= 20001);
    assert(mock::receive_errors == 1);
    const auto refused_at = mock::now;
    assert(!ps2_pio_send_byte(Ps2Port::Mouse, 0xf4)); // Recovery owns clock.
    assert(mock::now == refused_at);
    assert(mock::output[1]);
    mock::advance(151);
    ps2_pio_poll();
    assert(!ports[1].recovering && !mock::output[1]);
  }

  fresh();
  ps2_pio_flash_pause();
  assert(mock::output[28] && mock::output[1]);
  assert(!mock_pio.enabled[0] && !mock_pio.enabled[1]);
  assert(mock::now >= 150);
  Ps2PioDiagnostics input;
  ps2_pio_get_diagnostics(input);
  assert(input.ready && input.flash_paused && !(input.ctrl & 3u));
  assert(input.port[0].clock_output && input.port[1].clock_output);
  assert(!input.port[0].clock_high && !input.port[1].clock_high);
  assert(!ps2_pio_send_byte(Ps2Port::Mouse, 0xf4));
  mock::advance(50000);
  ps2_pio_flash_resume();
  assert(!mock::output[28] && !mock::output[1]);
  assert(mock_pio.enabled[0] && mock_pio.enabled[1]);
  assert(mock::receive_errors == 2);
  ps2_pio_get_diagnostics(input);
  assert(!input.flash_paused && (input.ctrl & 3u) == 3u);
  assert(!input.port[0].clock_output && !input.port[1].clock_output);

  // Diagnostics must report a disabled/stopped SM without altering it, and
  // report FIFO contents without draining away the evidence of a freeze.
  pio_sm_set_enabled(pio1, 0, false);
  mock_pio.sm[0].pc = 3;
  mock_pio.sm[0].clkdiv = 0x1980000;
  mock_pio.fifo[0].push_back(0x12345678);
  const auto errors = mock::receive_errors;
  ps2_pio_get_diagnostics(input);
  assert(!(input.ctrl & 1u) && input.port[0].pc == 3 && input.port[0].rx_level == 1);
  assert(input.port[0].clkdiv == 0x1980000 && input.port[0].clock_function == 5);
  assert(mock_pio.fifo[0].size() == 1 && !mock_pio.enabled[0]);
  assert(mock::receive_errors == errors);

  fresh();
  mock_pio.fdebug.value = 1u << ports[1].sm;
  ps2_pio_poll();
  assert(ports[1].recovering && mock::output[1]);
  assert(mock::receive_errors == 1);
  mock::advance(151);
  ps2_pio_poll();
  assert(!ports[1].recovering && !mock::output[1]);

  std::puts("Actual transport host mock: 256 TX bytes/parity/stop, immediate FA, missing device/ACK deadlines, recovery refusal, FIFO stall, flash inhibit passed");
}
