#include "gpu_display.h"
#include "nano_gpu.h"
#include "globals.h"
#include "font.h"
#include "audio_runtime.h"
#include "usb_storage_mode.h"
#include <Arduino.h>
#include <initializer_list>

namespace {
constexpr unsigned kClock = 16, kWrite = 17, kData = 18;
constexpr unsigned kAbort = 35, kReset = 36, kAddress = 37, kRead = 38;
constexpr unsigned kReady = 39, kValid = 40;
constexpr uint32_t kTimeoutUs = 250000;
constexpr uint32_t kConfigurationWaitMs = 1000;
static_assert(__GPIOCNT > kValid, "Select RP2350B: GPU handshakes require GPIO39/40");
NanoGpu gpu;
int selected_address = -1;
unsigned service_counter = 0;
uint8_t address_sent = 0, address_sensed = 0;
uint8_t data_pins = 0;

uint8_t read_data_pins() {
  uint8_t byte = 0;
  for (unsigned bit = 0; bit < 8; ++bit)
    byte |= (uint8_t)((digitalRead(kData + bit) == HIGH) << bit);
  return byte;
}

void service() {
  if ((++service_counter & 31u) == 0) { usb_storage_poll(); audio_core0_poll(); }
}
void low() { digitalWrite(kClock, LOW); delayMicroseconds(1); }
void high() { digitalWrite(kClock, HIGH); delayMicroseconds(1); }
void release_data() {
  for (unsigned bit = 0; bit < 8; ++bit) pinMode(kData + bit, INPUT);
}
void write_data(uint8_t byte) {
  for (unsigned bit = 0; bit < 8; ++bit) {
    // Arduino-Pico pinMode(OUTPUT) clears the output latch. Set the mode
    // before writing each bit, with both bus strobes still inactive.
    pinMode(kData + bit, OUTPUT);
    digitalWrite(kData + bit, (byte & (1u << bit)) ? HIGH : LOW);
  }
}
void release_bus() {
  digitalWrite(kWrite, HIGH); digitalWrite(kRead, HIGH); digitalWrite(kAddress, HIGH);
  release_data(); digitalWrite(kClock, LOW);
  // Keep scanout alive for diagnosis. Only explicit initialization asserts
  // GPU_RESET_N; a failed/partial packet is never retried in this session.
  digitalWrite(kReset, HIGH);
  selected_address = -1;
}
bool write_cycle(uint8_t byte, bool address) {
  digitalWrite(kRead, HIGH); digitalWrite(kWrite, HIGH); low();
  write_data(byte);
  digitalWrite(kAddress, address ? LOW : HIGH); digitalWrite(kWrite, LOW);
  delayMicroseconds(1);
  if (address) {
    address_sent = byte; address_sensed = read_data_pins();
  }
  const uint32_t start = micros();
  do {
    const bool accepted = digitalRead(kReady) == HIGH; // Before the rising edge.
    high(); low();
    if (accepted) {
      digitalWrite(kWrite, HIGH); digitalWrite(kAddress, HIGH); release_data();
      service(); return true;
    }
  } while ((uint32_t)(micros() - start) < kTimeoutUs);
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
  digitalWrite(kWrite, HIGH); digitalWrite(kRead, HIGH); release_data(); low();
  digitalWrite(kAddress, HIGH); digitalWrite(kRead, LOW); delayMicroseconds(1);
  const uint32_t start = micros();
  do {
    high();
    const bool valid = digitalRead(kValid) == HIGH;
    if (valid) {
      // Keep RD_N asserted and HCLK HIGH throughout both observations.
      data_pins = read_data_pins();
      *out = data_pins;
      digitalWrite(kRead, HIGH); // End this read before another rising edge.
    }
    low();
    if (valid) { service(); return true; }
  } while ((uint32_t)(micros() - start) < kTimeoutUs);
  release_bus(); return false;
}
bool read_byte(void *, uint8_t *out) { return read_at(0, out); }
bool read_register(void *, uint8_t address, uint8_t *out) { return read_at(address, out); }
}

bool gpu_display_begin() {
  // Assert reset first: pinMode(OUTPUT_12MA) initially drives LOW. Keep GPU
  // logic reset while the other control outputs are configured.
  pinMode(kReset, OUTPUT_12MA); digitalWrite(kReset, LOW);
  delayMicroseconds(1);
  for (unsigned pin : {kClock, kWrite, kAbort, kAddress, kRead}) {
    pinMode(pin, OUTPUT_12MA);
    digitalWrite(pin, (pin == kWrite || pin == kAddress || pin == kRead) ? HIGH : LOW);
  }
  for (unsigned pin : {kReady, kValid}) pinMode(pin, INPUT);
  release_data(); selected_address = -1;
  address_sent = 0; address_sensed = 0;
  data_pins = 0;
  // Flash configuration can take several hundred milliseconds. Keep GPU
  // logic reset asserted and the bus released before attempting any transfer.
  // This is a startup allowance, not a configuration-DONE measurement.
  delay(kConfigurationWaitMs);
  for (unsigned i = 0; i < 32; ++i) { high(); low(); }
  digitalWrite(kReset, HIGH);
  for (unsigned i = 0; i < 64; ++i) { high(); low(); }
  const bool ok = gpu.begin({nullptr, write_byte, read_byte, read_register}, font);
  if (!ok) release_bus();
  gpu_display_diagnostics();
  if (!ok) gpu_display_probe();
  return ok;
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
  Serial.print("; RESET_N="); Serial.print(digitalRead(kReset));
  Serial.print("; WREADY="); Serial.print(digitalRead(kReady));
  Serial.print("; RVALID="); Serial.println(digitalRead(kValid));
  const uint8_t *status = gpu.boot_status();
  const uint32_t core_fault = (uint32_t)status[32] | ((uint32_t)status[33] << 8) |
      ((uint32_t)status[34] << 16) | ((uint32_t)status[35] << 24);
  Serial.print("GPU BOOT: flags=0x"); Serial.print(status[1], HEX);
  Serial.print("; core_fault="); Serial.print(core_fault);
  Serial.print("; link_fault="); Serial.println(status[56]);
  Serial.print("GPU ADDRESS: sent=0x"); Serial.print(address_sent, HEX);
  Serial.print("; sensed=0x"); Serial.println(address_sensed, HEX);
  Serial.print("GPU DATA: pins=0x"); Serial.print(data_pins, HEX);
  Serial.println("");
}

void gpu_display_probe() {
  if (gpu.ready()) { Serial.println("GPU_PROBE is for an offline link; use GPU_STATUS."); return; }
  // Read immutable sideband capabilities without submitting or retrying a
  // packet. Even a core fault must leave these registers accessible.
  Serial.print("GPU CAPS:");
  for (unsigned address = 64; address < 80; ++address) {
    uint8_t value;
    if (!read_at(address, &value)) {
      Serial.print(" timeout at register "); Serial.println(address); return;
    }
    Serial.print(" "); Serial.print(value, HEX);
  }
  Serial.println("");
  release_bus();
  gpu_display_diagnostics();
}
