// Compile the actual board glue against GPIO/PIO/time stubs; no board build.
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
void gpu_ui_prepare_terminal(NanoGpu &) {}

void reset_fixture(bool ready, bool valid) {
  mock_pio2 = {};
  pio_sm = -1; pio_active = false; selected_address = -1;
  memset(mock_gpio::level, 0, sizeof(mock_gpio::level));
  memset(mock_gpio::output, 0, sizeof(mock_gpio::output));
  memset(mock_gpio::sio_level, 0, sizeof(mock_gpio::sio_level));
  memset(mock_gpio::sio_output, 0, sizeof(mock_gpio::sio_output));
  memset(mock_gpio::pio_level, 0, sizeof(mock_gpio::pio_level));
  memset(mock_gpio::pio_output, 0, sizeof(mock_gpio::pio_output));
  memset(mock_gpio::pio_mux, 0, sizeof(mock_gpio::pio_mux));
  memset(mock_gpio::drive, 0, sizeof(mock_gpio::drive));
  memset(mock_gpio::status, 0, sizeof(mock_gpio::status));
  mock_gpio::status[65] = 2; mock_gpio::status[66] = 8; mock_gpio::status[67] = 32;
  mock_gpio::ready = ready; mock_gpio::valid = valid;
  mock_gpio::valid_follows_clock = false; mock_gpio::clock_rises = 0;
  mock_gpio::write_wait_edges = 0; mock_gpio::packet_writes = 0;
  mock_gpio::swap_d5_d6 = false; mock_gpio::read_rises = 0;
  mock_gpio::latch_first_only = false; mock_gpio::latched_since_reset = false;
  mock_gpio::latch_and = 0xff; mock_gpio::latch_or = 0;
  mock_gpio::now = 0; mock_gpio::reset_clock_edges = 0;
  mock_gpio::sub_us_ticks = 0;
  mock_gpio::first_clock_us = UINT32_MAX; mock_gpio::configuration_ready_us = 0;
  mock_gpio::last_clock_edge_ticks = mock_gpio::min_clock_phase_ticks = UINT64_MAX;
  mock_gpio::last_rise_ticks = mock_gpio::min_clock_period_ticks = UINT64_MAX;
  mock_gpio::write_assert_ticks = mock_gpio::read_assert_ticks = 0;
  mock_gpio::min_write_setup_ticks = mock_gpio::min_read_setup_ticks = UINT64_MAX;
  mock_gpio::reset_writes.clear(); Serial.text.clear();
}

void check_failure_state() {
  assert(!gpu.ready());
  assert(!pio_active && !mock_pio2.enabled);
  assert(mock_gpio::output[36] && mock_gpio::level[36]);
  assert(mock_gpio::level[17] && mock_gpio::level[37] && mock_gpio::level[38]);
  assert(!mock_gpio::level[16]);
  for (unsigned pin = 18; pin < 26; ++pin) assert(!mock_gpio::output[pin]);
  for (unsigned pin = 16; pin < 26; ++pin) assert(!mock_gpio::pio_mux[pin]);
  assert(!mock_gpio::pio_mux[37] && !mock_gpio::pio_mux[38]);
  assert(!mock_gpio::output[39] && !mock_gpio::output[40]);
  for (unsigned pin : {16u, 17u, 35u, 36u, 37u, 38u}) assert(mock_gpio::drive[pin] == 12);
  assert(mock_gpio::reset_clock_edges == 32);
  assert(mock_gpio::first_clock_us >= 1000000);
  assert(mock_gpio::min_clock_period_ticks >= 15); // 25 MHz at 375 MHz clk_sys.
  assert(mock_gpio::min_clock_phase_ticks >= 6);
  assert(mock_gpio::min_write_setup_ticks >= 7);
  assert(mock_gpio::min_read_setup_ticks >= 8);
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
  reset_fixture(true, true);
  mock_pio2.claimed[1] = true;
  assert(!gpu_display_begin()); check_failure_state();
  assert(mock_pio2.claimed[1] && mock_pio2.base == 0 && mock_pio2.length == 0);
  assert(mock_gpio::clock_rises == 96); // Reset/idle clocks only. No GPIO transfer fallback.
  assert(Serial.text.find("GPU PIO: PIO2 unavailable.") != std::string::npos);
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
  // Ready, but no reply ever flagged: the status-polled wait hits its deadline.
  assert(!gpu_display_begin()); check_failure_state();
  assert(std::string(gpu.error()) == "reply wait timeout");
  assert(mock_gpio::now >= 1000000 + NanoGpu::kReplyDeadlineMs * 1000);
  reset_fixture(true, true);
  mock_gpio::status[1] = 0x11; mock_gpio::status[2] = 1; // Ready, reply available.
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
  assert(address_sent == 0x23 && address_sensed == 0x23);
  reset_fixture(true, true);
  memset(mock_gpio::status + 64, 0, 16);
  assert(!gpu_display_begin()); check_failure_state();
  assert(std::string(gpu.error()) == "invalid capability readback");
  assert(select(0));
  const unsigned rises_before_retry = mock_gpio::clock_rises;
  const unsigned writes_before_retry = mock_gpio::packet_writes;
  mock_gpio::write_wait_edges = 3;
  assert(write_byte(nullptr, 0xa5));
  assert(mock_gpio::clock_rises == rises_before_retry + 4);
  assert(mock_gpio::packet_writes == writes_before_retry + 1 && mock_gpio::last_data_write == 0xa5);
  // All byte patterns verify bit order and data-direction changes. The
  // mock executes the assembled PIO instruction words for each byte.
  for (unsigned byte = 0; byte < 256; ++byte) {
    assert(write_byte(nullptr, (uint8_t)byte));
    assert(mock_gpio::last_data_write == byte);
    for (unsigned pin = 18; pin < 26; ++pin) assert(!mock_gpio::output[pin]);
    mock_gpio::status[64] = (uint8_t)byte;
    uint8_t received = 0;
    assert(read_at(64, &received));
    assert(received == byte);
  }
  assert(mock_gpio::min_clock_period_ticks == 15);
  assert(mock_gpio::min_clock_phase_ticks >= 6);
  assert(mock_gpio::min_write_setup_ticks >= 7 && mock_gpio::min_write_setup_ticks != UINT64_MAX);
  assert(mock_gpio::min_read_setup_ticks >= 8 && mock_gpio::min_read_setup_ticks != UINT64_MAX);
  const uint8_t previous_packet_byte = mock_gpio::last_data_write;
  mock_gpio::status[65] = 2;
  unsigned edges = mock_gpio::clock_rises;
  gpu_display_hold();
  assert(read_held && !gpu.ready() && mock_gpio::address == 65 && data_pins == 2);
  assert(!pio_active && !mock_pio2.enabled);
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
  mock_gpio::valid = true;
  mock_gpio::status[1] = 1; mock_gpio::status[2] = 1;
  memset(mock_gpio::status + 32, 0, 16);
  mock_gpio::status[65] = 2; mock_gpio::status[67] = 0x20;
  Serial.text.clear();
  gpu_display_swaptest();
  assert(Serial.text.find("GPU SWAPTEST: 1 0 0; flags=0x1; edges=4 1 1 1; pre=0 0 0 0\n") != std::string::npos);
  assert(!read_held && mock_gpio::level[38] && !mock_gpio::level[16]);
  mock_gpio::swap_d5_d6 = true;
  Serial.text.clear();
  gpu_display_probe();
  assert(Serial.text.find("GPU CAPS: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0") != std::string::npos);
  assert(Serial.text.find("GPU SWAPTEST: 1 2 40; flags=0x1; edges=4 1 1 1; pre=0 0 0 0\n") != std::string::npos);
  assert(address_sent == 0x23 && address_sensed == 0x23 && mock_gpio::address == 0x43);
  assert(mock_gpio::last_data_write == previous_packet_byte);
  mock_gpio::swap_d5_d6 = false; mock_gpio::status[2] = 0;
  Serial.text.clear();
  gpu_display_swaptest();
  assert(Serial.text.find("GPU SWAPTEST: 0 0 0") != std::string::npos);
  // The reported all-zero pattern with an RVALID that follows HCLK: every
  // read, including the register-1 snapshot refresh, "completes" on edge 1.
  memset(mock_gpio::status, 0, 128);
  mock_gpio::valid_follows_clock = true;
  Serial.text.clear();
  gpu_display_probe();
  assert(Serial.text.find("GPU CAPS: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0") != std::string::npos);
  assert(Serial.text.find("GPU SWAPTEST: 0 0 0; flags=0x0; edges=1 1 1 1; pre=0 0 0 0\n") != std::string::npos);
  mock_gpio::valid_follows_clock = false;
  mock_gpio::status[65] = 2; mock_gpio::status[66] = 8; mock_gpio::status[67] = 0x20;
  mock_gpio::valid = false;
  gpu_display_swaptest();
  assert(Serial.text.find("GPU SWAPTEST: timeout at register 0x1") != std::string::npos);
  assert(mock_gpio::level[38] && !mock_gpio::level[16] && mock_gpio::level[36]);
  mock_gpio::valid = true;
  gpu_display_hold();
  edges = mock_gpio::clock_rises;
  gpu_display_swaptest();
  assert(read_held && mock_gpio::clock_rises == edges);
  gpu_display_latchtest(); // Refused while a read is held: no reset, no edge.
  assert(read_held && mock_gpio::clock_rises == edges && mock_gpio::level[36]);
  gpu_display_release();

  // GPU_LATCHTEST: a fresh reset, then 41 42 43 41 without reading 40 first.
  const unsigned latch_reset_edges = mock_gpio::reset_clock_edges;
  Serial.text.clear();
  gpu_display_latchtest();
  assert(Serial.text.find("GPU LATCHTEST: 2 8 20 2; edges=1 1 1 1; expected=2 8 20 2\n") != std::string::npos);
  assert(mock_gpio::reset_clock_edges == latch_reset_edges + 32);
  assert(!mock_gpio::reset_writes.empty() && mock_gpio::reset_writes.back());
  assert(mock_gpio::level[38] && !mock_gpio::level[16] && mock_gpio::level[36]);
  for (unsigned pin = 18; pin < 26; ++pin) assert(!mock_gpio::output[pin]);
  assert(mock_gpio::last_data_write == previous_packet_byte); // No packet byte.
  mock_gpio::latch_first_only = true; // Only the first latch after reset lands.
  Serial.text.clear();
  gpu_display_latchtest();
  assert(Serial.text.find("GPU LATCHTEST: 2 2 2 2;") != std::string::npos);
  mock_gpio::latch_first_only = false;
  mock_gpio::latch_and = 0x80; mock_gpio::latch_or = 0x40; // FPGA always sees 0x40.
  Serial.text.clear();
  gpu_display_latchtest();
  assert(Serial.text.find("GPU LATCHTEST: 0 0 0 0;") != std::string::npos);
  mock_gpio::latch_and = 0xff; mock_gpio::latch_or = 0;
  mock_gpio::valid = false;
  Serial.text.clear();
  gpu_display_latchtest();
  assert(Serial.text.find("GPU LATCHTEST: timeout at register 0x41") != std::string::npos);
  assert(mock_gpio::level[38] && !mock_gpio::level[16] && mock_gpio::level[36]);
  mock_gpio::valid = true;

  // GPU_DRIVE: hold an address phase with no rising edge; nothing is latched.
  mock_gpio::address = 0x11;
  edges = mock_gpio::clock_rises;
  Serial.text.clear();
  gpu_display_drive(0x55);
  assert(drive_held && !read_held && mock_gpio::clock_rises == edges && mock_gpio::address == 0x11);
  assert(!mock_gpio::level[16] && !mock_gpio::level[37] && !mock_gpio::level[17] && mock_gpio::level[38]);
  assert(mock_gpio::level[36]);
  for (unsigned bit = 0; bit < 8; ++bit) {
    assert(mock_gpio::output[18 + bit]);
    assert(mock_gpio::level[18 + bit] == (((0x55u >> bit) & 1u) != 0));
  }
  assert(Serial.text.find("GPU DRIVE: address phase held with 0x55; HCLK LOW, no clock edge") != std::string::npos);
  assert(Serial.text.find("GPU DATA: pins=0x55") != std::string::npos);
  assert(Serial.text.find("GPU BUS: held=0; drive=1; HCLK=0; WR_N=0; RD_N=1; ALE_N=0") != std::string::npos);
  Serial.text.clear();
  gpu_display_probe(); gpu_display_swaptest(); gpu_display_latchtest();
  assert(Serial.text.find("GPU_DRIVE active") != std::string::npos);
  assert(drive_held && mock_gpio::clock_rises == edges && mock_gpio::level[36]);
  gpu_display_drive(0x2A); // Re-driving releases and re-holds without an edge.
  assert(drive_held && mock_gpio::clock_rises == edges && mock_gpio::address == 0x11);
  for (unsigned bit = 0; bit < 8; ++bit)
    assert(mock_gpio::level[18 + bit] == (((0x2Au >> bit) & 1u) != 0));
  Serial.text.clear();
  gpu_display_release();
  assert(Serial.text.find("GPU RELEASE: bus released") != std::string::npos);
  assert(!drive_held && mock_gpio::clock_rises == edges && mock_gpio::address == 0x11);
  assert(mock_gpio::level[17] && mock_gpio::level[37] && mock_gpio::level[38] && !mock_gpio::level[16]);
  for (unsigned pin = 18; pin < 26; ++pin) assert(!mock_gpio::output[pin]);
  Serial.text.clear();
  gpu_display_release();
  assert(Serial.text.find("GPU RELEASE: no hold active.") != std::string::npos);
  // GPU_REGS: two coherent sideband snapshots, decoded, with no packet sent.
  memset(mock_gpio::status, 0, 64);
  mock_gpio::status[1] = 0x13; mock_gpio::status[2] = 1;          // ready busy reply; epoch 1
  mock_gpio::status[6] = 0x2C; mock_gpio::status[7] = 0x01;       // completed 300
  mock_gpio::status[10] = 0x2B; mock_gpio::status[11] = 0x01;     // fence 299
  mock_gpio::status[14] = 1; mock_gpio::status[16] = 0x40; mock_gpio::status[17] = 0x42; mock_gpio::status[18] = 0x0F;
  mock_gpio::status[36] = 7; mock_gpio::status[56] = 0;
  const uint8_t packet_before_regs = mock_gpio::last_data_write;
  Serial.text.clear();
  gpu_display_regs();
  assert(Serial.text.find("GPU CLIENT: last_sent=") != std::string::npos);
  assert(Serial.text.find("GPU REGS t0: flags=0x13 [ready busy reply-available ]; epoch=1; completed=300; "
                          "fence=299; front=1; cycles=1000000; core_fault=0; present=7; link_fault=0\n")
         != std::string::npos);
  assert(Serial.text.find("GPU REGS t+500ms: flags=0x13") != std::string::npos);
  assert(Serial.text.find("GPU REGS delta: cycles=+0; completed=+0\n") != std::string::npos);
  assert(mock_gpio::last_data_write == packet_before_regs); // Sideband only.
  assert(!read_held && mock_gpio::level[38] && !mock_gpio::level[16] && mock_gpio::level[36]);
  gpu_display_drive(0x10);
  Serial.text.clear();
  gpu_display_regs();
  assert(Serial.text.find("GPU_DRIVE active") != std::string::npos);
  gpu_display_release();
  mock_gpio::valid = false;
  Serial.text.clear();
  gpu_display_regs();
  assert(Serial.text.find("GPU REGS: timeout at register 1") != std::string::npos);
  mock_gpio::valid = true;
  gpu_display_drive(0x7F);
  assert(!gpu_display_begin()); // RESET also ends a drive hold.
  assert(!drive_held && mock_gpio::level[17] && mock_gpio::level[37]);
  for (unsigned pin = 18; pin < 26; ++pin) assert(!mock_gpio::output[pin]);
  // Isolate each retry loop and verify its 40 ns HCLK period.
  assert(select(0));
  mock_gpio::last_rise_ticks = mock_gpio::min_clock_period_ticks = UINT64_MAX;
  mock_gpio::write_wait_edges = 4;
  assert(write_byte(nullptr, 0xa5));
  assert(mock_gpio::min_clock_period_ticks == 15);
  assert(select(1));
  mock_gpio::last_rise_ticks = mock_gpio::min_clock_period_ticks = UINT64_MAX;
  uint8_t flags;
  assert(read_at(1, &flags) && read_edges == 4);
  assert(mock_gpio::min_clock_period_ticks == 15);
  std::cout << "PASS PIO transport and GPIO diagnostics: 256 write/read byte patterns, WREADY retry, PIO allocation failure, configuration startup wait, reset pulse, timeouts, "
               "startup fault and malformed reply leave reset HIGH and data released; "
               "startup diagnostics, address echo, capability probe, held reads, idle handshake check, "
               "release/reset transitions, D5/D6 swap and RVALID edge-count signatures, address-latch tests, edge-free drive holds and read/write HCLK periods verified.\n";
}
