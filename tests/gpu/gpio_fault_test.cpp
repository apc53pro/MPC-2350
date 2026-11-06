// Compile the actual board glue against GPIO/time stubs; no Arduino build.
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
  memset(mock_gpio::status, 0, sizeof(mock_gpio::status));
  mock_gpio::ready = ready; mock_gpio::valid = valid;
  mock_gpio::now = 0; mock_gpio::reset_clock_edges = 0;
  mock_gpio::first_clock_us = UINT32_MAX; mock_gpio::configuration_ready_us = 0;
  mock_gpio::reset_writes.clear(); Serial.text.clear();
}

void check_failure_state() {
  assert(!gpu.ready());
  assert(mock_gpio::output[36] && mock_gpio::level[36]);
  assert(mock_gpio::level[17] && mock_gpio::level[37] && mock_gpio::level[38]);
  assert(!mock_gpio::level[16]);
  for (unsigned pin = 18; pin < 26; ++pin) assert(!mock_gpio::output[pin]);
  assert(!mock_gpio::output[39] && !mock_gpio::output[40]);
  assert(mock_gpio::reset_clock_edges == 32);
  assert(mock_gpio::first_clock_us >= 1000000);
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
  mock_gpio::configuration_ready_us = 800000; // FPGA takes 800 ms to configure.
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
  std::cout << "PASS actual GPIO glue: configuration startup wait, reset polarity/pulse, write/read timeouts, "
               "startup fault and malformed reply leave reset HIGH and data released; "
               "startup diagnostics, address pad echo and immutable capability probe verified.\n";
}
