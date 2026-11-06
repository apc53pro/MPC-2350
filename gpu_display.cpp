#include "gpu_display.h"
#include "nano_gpu.h"
#include "globals.h"
#include "font.h"
#include "audio_runtime.h"
#include "usb_storage_mode.h"
#include <hardware/gpio.h>
#include <pico/time.h>
#include <initializer_list>

namespace {
constexpr unsigned kClock = 16, kWrite = 17, kData = 18;
constexpr unsigned kAbort = 35, kReset = 36, kAddress = 37, kRead = 38;
constexpr unsigned kReady = 39, kValid = 40;
constexpr uint32_t kTimeoutUs = 250000;
constexpr uint32_t kDataMask = 0xffu << kData;
static_assert(NUM_BANK0_GPIOS > kValid, "Select RP2350B: GPU handshakes require GPIO39/40");
NanoGpu gpu;
int selected_address = -1;
unsigned service_counter = 0;

void service() {
  if ((++service_counter & 31u) == 0) { usb_storage_poll(); audio_core0_poll(); }
}
void low() { gpio_put(kClock, false); busy_wait_us_32(1); }
void high() { gpio_put(kClock, true); busy_wait_us_32(1); }
void release_data() { gpio_set_dir_in_masked(kDataMask); }
void release_bus() {
  gpio_put(kWrite, true); gpio_put(kRead, true); gpio_put(kAddress, true);
  release_data(); gpio_put(kClock, false);
  // Keep scanout alive for diagnosis. Only explicit initialization asserts
  // GPU_RESET_N; a failed/partial packet is never retried in this session.
  gpio_put(kReset, true);
  selected_address = -1;
}
bool write_cycle(uint8_t byte, bool address) {
  gpio_put(kRead, true); gpio_put(kWrite, true); low();
  gpio_put_masked(kDataMask, (uint32_t)byte << kData);
  gpio_set_dir_out_masked(kDataMask);
  gpio_put(kAddress, !address); gpio_put(kWrite, false);
  busy_wait_us_32(1);
  const uint32_t start = time_us_32();
  do {
    const bool accepted = gpio_get(kReady); // Before the rising edge.
    high(); low();
    if (accepted) {
      gpio_put(kWrite, true); gpio_put(kAddress, true); release_data();
      service(); return true;
    }
  } while ((uint32_t)(time_us_32() - start) < kTimeoutUs);
  release_bus(); return false;
}
bool select(unsigned address) {
  if ((int)address == selected_address) return true;
  if (!write_cycle((uint8_t)address, true)) return false;
  selected_address = (int)address; return true;
}
bool write_byte(void *, uint8_t byte) {
  return select(0) && write_cycle(byte, false);
}
bool read_at(unsigned address, uint8_t *out) {
  if (!select(address)) return false;
  gpio_put(kWrite, true); gpio_put(kRead, true); release_data(); low();
  gpio_put(kAddress, true); gpio_put(kRead, false); busy_wait_us_32(1);
  const uint32_t start = time_us_32();
  do {
    high();
    const bool valid = gpio_get(kValid);
    if (valid) {
      *out = (uint8_t)(gpio_get_all() >> kData);
      gpio_put(kRead, true); // End this read before another rising edge.
    }
    low();
    if (valid) { service(); return true; }
  } while ((uint32_t)(time_us_32() - start) < kTimeoutUs);
  release_bus(); return false;
}
bool read_byte(void *, uint8_t *out) { return read_at(0, out); }
bool read_register(void *, uint8_t address, uint8_t *out) { return read_at(address, out); }
}

bool gpu_display_begin() {
  // Latch safe levels before enabling output drivers. GP39/40 are inputs;
  // GP39's onboard LED must never be driven by RP LED code.
  for (unsigned pin : {kClock, kWrite, kAbort, kReset, kAddress, kRead}) {
    gpio_init(pin);
    gpio_put(pin, pin == kWrite || pin == kAddress || pin == kRead);
    gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_dir(pin, GPIO_OUT);
  }
  for (unsigned pin = kData; pin < kData + 8; ++pin) gpio_init(pin);
  for (unsigned pin : {kReady, kValid}) { gpio_init(pin); gpio_disable_pulls(pin); }
  release_data(); selected_address = -1;
  for (unsigned i = 0; i < 32; ++i) { high(); low(); }
  gpio_put(kReset, true);
  for (unsigned i = 0; i < 64; ++i) { high(); low(); }
  const bool ok = gpu.begin({nullptr, write_byte, read_byte, read_register}, font);
  if (!ok) release_bus();
  gpu_display_diagnostics(); return ok;
}

void gpu_display_present() {
  if (gpu.ready() && !gpu.terminal_present(gridBuffer_char, char_grid_buffer_V_draw_offset)) {
    release_bus(); gpu_display_diagnostics();
  }
}

void gpu_display_diagnostics() {
  Serial.print("NANO GPU: "); Serial.print(gpu.ready() ? "READY" : "OFFLINE");
  Serial.print("; "); Serial.print(gpu.error());
  Serial.print("; presents="); Serial.print(gpu.presents());
  Serial.print("; RESET_N="); Serial.print(gpio_get(kReset));
  Serial.print("; WREADY="); Serial.print(gpio_get(kReady));
  Serial.print("; RVALID="); Serial.println(gpio_get(kValid));
  const uint8_t *status = gpu.boot_status();
  const uint32_t core_fault = (uint32_t)status[32] | ((uint32_t)status[33] << 8) |
      ((uint32_t)status[34] << 16) | ((uint32_t)status[35] << 24);
  Serial.print("GPU BOOT: flags=0x"); Serial.print(status[1], HEX);
  Serial.print("; core_fault="); Serial.print(core_fault);
  Serial.print("; link_fault="); Serial.println(status[56]);
}
