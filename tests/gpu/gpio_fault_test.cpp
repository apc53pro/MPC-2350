// Compile the actual board glue against Arduino GPIO/time stubs; no board build.
#define GLOBALS_H
#define AUDIO_RUNTIME_H
#define USB_STORAGE_MODE_H
#include <stdint.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

constexpr int HEX = 16;
struct TestSerial {
  std::string text;
  void print(const char *s) { text += s; }
  template<class T> void print(T value) { text += std::to_string(value); }
  void print(uint8_t value, int) {
    std::ostringstream out; out << std::hex << (unsigned)value; text += out.str();
  }
  template<class T> void println(T value) { print(value); text += '\n'; }
  void println(uint8_t value, int base) { print(value, base); text += '\n'; }
} Serial;
char gridBuffer_char[40][60];
int char_grid_buffer_V_draw_offset = 0;
void usb_storage_poll() {}
void audio_core0_poll() {}

#include "../../gpu_display.cpp"

void reset_fixture(bool ready, bool valid) {
  memset(mock_gpio::level, 0, sizeof(mock_gpio::level));
  memset(mock_gpio::output, 0, sizeof(mock_gpio::output));
  memset(mock_gpio::drive, 0, sizeof(mock_gpio::drive));
  memset(mock_gpio::status, 0, sizeof(mock_gpio::status));
  mock_gpio::status[65] = 2; mock_gpio::status[66] = 8; mock_gpio::status[67] = 32;
  mock_gpio::ready = ready; mock_gpio::valid = valid;
  mock_gpio::valid_follows_clock = false; mock_gpio::clock_rises = 0;
  mock_gpio::now = 0; mock_gpio::reset_clock_edges = 0;
  mock_gpio::first_clock_us = UINT32_MAX; mock_gpio::configuration_ready_us = 0;
  mock_gpio::last_clock_edge_us = UINT32_MAX; mock_gpio::min_clock_phase_us = UINT32_MAX;
  mock_gpio::write_assert_us = 0; mock_gpio::read_assert_us = 0;
  mock_gpio::min_write_setup_us = UINT32_MAX; mock_gpio::min_read_setup_us = UINT32_MAX;
  mock_gpio::reset_writes.clear(); Serial.text.clear();
}

void check_failure_state() {
  assert(!gpu.ready());
  assert(mock_gpio::output[36] && mock_gpio::level[36]);
  assert(mock_gpio::level[17] && mock_gpio::level[37] && mock_gpio::level[38]);
  assert(!mock_gpio::level[16]);
  for (unsigned pin = 18; pin < 26; ++pin) assert(!mock_gpio::output[pin]);
  assert(!mock_gpio::output[39] && !mock_gpio::output[40]);
  for (unsigned pin : {16u, 17u, 35u, 36u, 37u, 38u}) assert(mock_gpio::drive[pin] == 12);
  assert(mock_gpio::reset_clock_edges == 32);
  assert(mock_gpio::first_clock_us >= 1000000);
  assert(mock_gpio::min_clock_phase_us >= 50); // HCLK never exceeds 10 kHz.
  assert(mock_gpio::min_write_setup_us >= 50);
  assert(mock_gpio::min_read_setup_us >= 50);
  assert(mock_gpio::reset_writes.front() == false);
  bool released = false;
  for (bool level : mock_gpio::reset_writes) {
    if (level) released = true;
    if (released) assert(level); // A failure must never assert reset again.
  }
  assert(released);
  assert(Serial.text.find("RESET_N=1") != std::string::npos);
}

int main() {
  reset_fixture(false, false);
  assert(!gpu_display_begin()); check_failure_state(); // WREADY timeout.
  assert(mock_gpio::now >= 1250000);
  reset_fixture(true, false);
  assert(!gpu_display_begin()); check_failure_state(); // RVALID timeout.
  assert(mock_gpio::now >= 1250000);
  reset_fixture(true, true);
  mock_gpio::configuration_ready_us = 100000; // FPGA takes 800 ms to configure.
  mock_gpio::status[1] = 9; mock_gpio::status[2] = 1;
  mock_gpio::status[32] = 8; mock_gpio::status[56] = 2;
  assert(!gpu_display_begin()); check_failure_state(); // GPU startup fault.
  assert(gpu.boot_status()[1] == 9 && gpu.boot_status()[32] == 8 && gpu.boot_status()[56] == 2);
  assert(Serial.text.find("core_fault=8; link_fault=2") != std::string::npos);
  reset_fixture(true, true);
  mock_gpio::status[1] = 1; mock_gpio::status[2] = 1;
  assert(!gpu_display_begin()); check_failure_state(); // Malformed GET_CAPS reply.
  assert(std::string(gpu.error()) == "invalid reply header");
  reset_fixture(true, true);
  memset(mock_gpio::status + 1, 0xff, 63);
  assert(!gpu_display_begin()); check_failure_state();
  assert(std::string(gpu.error()) == "all-ones startup readback");
  mock_gpio::status[64] = 0; mock_gpio::status[65] = 2;
  mock_gpio::status[66] = 8; mock_gpio::status[67] = 32;
  gpu_display_probe(); check_failure_state();
  assert(Serial.text.find("GPU CAPS: 0 2 8 20") != std::string::npos);
  assert(address_sent == 79 && address_sensed == 79);
  reset_fixture(true, true);
  memset(mock_gpio::status + 64, 0, 16);
  assert(!gpu_display_begin()); check_failure_state();
  assert(std::string(gpu.error()) == "invalid capability readback");
  // All byte patterns verify bit order and data-direction changes. The
  // Arduino pinMode output-latch clearing is deliberately modeled above.
  for (unsigned byte = 0; byte < 256; ++byte) {
    assert(write_cycle((uint8_t)byte, false));
    assert(mock_gpio::last_data_write == byte);
    for (unsigned pin = 18; pin < 26; ++pin) assert(!mock_gpio::output[pin]);
    mock_gpio::status[64] = (uint8_t)byte;
    uint8_t received = 0;
    assert(read_at(64, &received));
    assert(received == byte);
  }
  assert(mock_gpio::min_clock_phase_us >= 50);
  assert(mock_gpio::min_write_setup_us >= 50 && mock_gpio::min_write_setup_us != UINT32_MAX);
  assert(mock_gpio::min_read_setup_us >= 50 && mock_gpio::min_read_setup_us != UINT32_MAX);
  const uint8_t previous_packet_byte = mock_gpio::last_data_write;
  mock_gpio::status[65] = 2;
  unsigned edges = mock_gpio::clock_rises;
  gpu_display_hold();
  assert(read_held && !gpu.ready() && mock_gpio::address == 65 && data_pins == 2);
  assert(mock_gpio::clock_rises == edges + 3); // Idle check, address, read.
  assert(mock_gpio::level[16] && !mock_gpio::level[38]);
  assert(mock_gpio::level[17] && mock_gpio::level[37] && mock_gpio::level[36]);
  assert(digitalRead(39) == LOW && digitalRead(40) == HIGH);
  for (unsigned pin = 18; pin < 26; ++pin) assert(!mock_gpio::output[pin]);
  assert(Serial.text.find("RVALID_LOWCLK=0; RVALID_HIGHCLK=0; expected=0/0") != std::string::npos);
  edges = mock_gpio::clock_rises;
  gpu_display_diagnostics(); gpu_display_probe();
  assert(read_held && mock_gpio::clock_rises == edges);
  assert(Serial.text.find("GPU_HOLD active") != std::string::npos);
  gpu_display_hold(); // Repeating HOLD must relatch, not reuse a cached address.
  assert(read_held && mock_gpio::clock_rises == edges + 3 && mock_gpio::address == 65);
  gpu_display_release();
  assert(!read_held && !mock_gpio::level[16] && mock_gpio::level[38]);
  assert(mock_gpio::level[17] && mock_gpio::level[37] && mock_gpio::level[36]);
  assert(digitalRead(40) == LOW);
  mock_gpio::status[65] = 0;
  gpu_display_hold(); // Invalid zero data must remain held for measurement.
  assert(read_held && data_pins == 0 && mock_gpio::level[16] && !mock_gpio::level[38]);
  gpu_display_release();
  mock_gpio::ready = false;
  gpu_display_hold();
  assert(!read_held && mock_gpio::level[38] && !mock_gpio::level[16]);
  assert(Serial.text.find("address timeout; bus released") != std::string::npos);
  mock_gpio::ready = true; mock_gpio::valid = false;
  const uint32_t before_timeout = mock_gpio::now;
  gpu_display_hold();
  assert(mock_gpio::now - before_timeout >= 250000);
  assert(read_held && mock_gpio::level[16] && !mock_gpio::level[38]);
  assert(Serial.text.find("RVALID timeout; read held") != std::string::npos);
  gpu_display_release();
  mock_gpio::valid = true; mock_gpio::valid_follows_clock = true;
  gpu_display_hold();
  assert(read_held);
  assert(Serial.text.find("RVALID_LOWCLK=0; RVALID_HIGHCLK=1; expected=0/0") != std::string::npos);
  assert(mock_gpio::last_data_write == previous_packet_byte); // HOLD sent no packet.
  mock_gpio::valid_follows_clock = false; mock_gpio::valid = false;
  const unsigned reset_edges_before = mock_gpio::reset_clock_edges;
  assert(!gpu_display_begin()); // RESET also exits a held read safely.
  assert(!read_held && mock_gpio::level[38] && !mock_gpio::level[16]);
  assert(mock_gpio::level[36] && mock_gpio::reset_clock_edges == reset_edges_before + 32);
  std::cout << "PASS Arduino GPIO glue: 256 write/read byte patterns, configuration startup wait, reset pulse, timeouts, "
               "startup fault and malformed reply leave reset HIGH and data released; "
               "startup diagnostics, address echo, capability probe, held reads, idle handshake check, "
               "release/reset transitions and 50 us clock/setup minima verified.\n";
}
