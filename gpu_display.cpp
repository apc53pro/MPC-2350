#include "gpu_display.h"
#include "gpu_bus.pio.h"
#include "gpu_ui.h"
#include "nano_gpu.h"
#include "globals.h"
#include "font.h"
#include "audio_runtime.h"
#include "usb_storage_mode.h"
#include <Arduino.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <initializer_list>

namespace {
constexpr unsigned kClock = 16, kWrite = 17, kData = 18;
constexpr unsigned kAbort = 35, kReset = 36, kAddress = 37, kRead = 38;
constexpr unsigned kReady = 39, kValid = 40;
constexpr uint32_t kTimeoutUs = 250000;
constexpr uint32_t kConfigurationWaitMs = 1000;
// Edit this one value to set the GPU HCLK burst rate in Hz.
constexpr uint32_t kHostClockHz = 250000;
static_assert(kHostClockHz > 0 && kHostClockHz <= 25000000,
              "GPU HCLK must be between 1 Hz and 25 MHz");
// GPIO-only reset and diagnostic pulses use whole microseconds per phase.
constexpr uint32_t kDiagnosticPhaseUs = (500000u + kHostClockHz - 1) / kHostClockHz;
constexpr uint64_t kIdlePins = (1ull << kWrite) | (1ull << kAddress) | (1ull << kRead);
constexpr uint64_t kControlPins = kIdlePins | (1ull << kClock);
constexpr uint64_t kBusPins = kControlPins | (0xffull << kData);
PIO const kPio = pio2;
static_assert(__GPIOCNT > kValid, "Select RP2350B: GPU handshakes require GPIO39/40");
static_assert(PICO_PIO_USE_GPIO_BASE, "GPU PIO requires the RP2350B GPIO base support");
static_assert(kValid - kData == gpu_bus_valid_bit, "GPU PIO data/valid pin layout changed");
NanoGpu gpu;
int pio_sm = -1;
uint pio_offset = 0;
bool pio_active = false;
int selected_address = -1;
unsigned service_counter = 0;
uint8_t address_sent = 0, address_sensed = 0;
uint8_t data_pins = 0;
bool read_held = false;
// GPU_DRIVE holds an address phase (ALE_N/WR_N LOW, HCLK LOW) without an edge.
bool drive_held = false;
// Last read handshake: RVALID before the first read edge, and the number of
// HCLK rising edges until RVALID was sampled HIGH (1 = the first edge).
bool read_pre_valid = false;
unsigned read_edges = 0;

uint8_t read_data_pins() {
  uint8_t byte = 0;
  for (unsigned bit = 0; bit < 8; ++bit)
    byte |= (uint8_t)((digitalRead(kData + bit) == HIGH) << bit);
  return byte;
}

void service() {
  if ((++service_counter & 31u) == 0) { usb_storage_poll(); audio_core0_poll(); }
}
void low() { digitalWrite(kClock, LOW); delayMicroseconds(kDiagnosticPhaseUs); }
void high() { digitalWrite(kClock, HIGH); delayMicroseconds(kDiagnosticPhaseUs); }
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
  if (pio_active) {
    pio_sm_set_enabled(kPio, (uint)pio_sm, false);
    pio_active = false;
    // A timeout can interrupt a high phase. Allow a full target period.
    if (digitalRead(kClock) == HIGH)
      delayMicroseconds((1000000u + kHostClockHz - 1) / kHostClockHz);
  }
  // Preload the SIO latches before switching away from PIO. Raise the
  // strobes first so an interrupted transfer cannot cause contention.
  gpio_put_masked64(kControlPins, kIdlePins);
  gpio_set_dir_masked64(kBusPins, kControlPins);
  for (unsigned pin : {kWrite, kRead, kAddress, kClock}) gpio_set_function(pin, GPIO_FUNC_SIO);
  for (unsigned bit = 0; bit < 8; ++bit) gpio_set_function(kData + bit, GPIO_FUNC_SIO);
  // Keep scanout alive for diagnosis. Only explicit initialization and the
  // GPU_LATCHTEST command assert GPU_RESET_N; a failed/partial packet is never
  // retried in this session.
  digitalWrite(kReset, HIGH);
  selected_address = -1;
  read_held = false; drive_held = false;
}

bool claim_pio() {
  // PIO1 belongs to PS/2. Reserve PIO2's GPIO base before audio starts.
  for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm) {
    if (pio_sm_is_claimed(kPio, sm)) return false;
  }
  const uint old_base = pio_get_gpio_base(kPio);
  const int sm = pio_claim_unused_sm(kPio, false);
  if (sm < 0) return false;
  if (pio_set_gpio_base(kPio, 16) != 0) {
    pio_sm_unclaim(kPio, (uint)sm);
    return false;
  }
  const int offset = pio_add_program(kPio, &gpu_bus_program);
  if (offset < 0) {
    pio_set_gpio_base(kPio, old_base);
    pio_sm_unclaim(kPio, (uint)sm);
    return false;
  }
  pio_sm = sm;
  pio_offset = (uint)offset;
  return true;
}

constexpr uint32_t pio_divider_for_hclk(uint32_t system_hz, uint32_t host_hz) {
  // Round the burst period up to whole system ticks before deriving the
  // 16.8 divider so fractional pulse widths cannot exceed the target rate.
  const uint64_t ticks = ((uint64_t)system_hz + host_hz - 1) / host_hz;
  if (ticks < gpu_bus_clock_cycles) return 256; // Minimum SM divider is 1.
  const uint64_t divider = (ticks * 256 + gpu_bus_clock_cycles - 1) / gpu_bus_clock_cycles;
  return divider <= 0xffffffu ? (uint32_t)divider : 0;
}

bool enable_pio() {
  if (pio_active) return true;
  const uint32_t divider = pio_divider_for_hclk(clock_get_hz(clk_sys), kHostClockHz);
  if (!divider) {
    Serial.println("GPU PIO: HCLK target is below the divider range.");
    return false;
  }
  if (pio_sm < 0 && !claim_pio()) {
    Serial.println("GPU PIO: PIO2 unavailable.");
    return false;
  }
  pio_sm_config config = gpu_bus_program_get_default_config(pio_offset);
  sm_config_set_sideset_pins(&config, kClock);
  sm_config_set_set_pins(&config, kAddress, 2);
  sm_config_set_out_pins(&config, kData, 8);
  sm_config_set_out_shift(&config, true, false, 32);
  sm_config_set_in_pins(&config, kData);
  sm_config_set_in_pin_count(&config, kValid - kData + 1);
  sm_config_set_in_shift(&config, false, false, 32);
  sm_config_set_jmp_pin(&config, kReady);
  sm_config_set_clkdiv_int_frac8(&config, divider >> 8, (uint8_t)divider);
  const uint sm = (uint)pio_sm;
  if (pio_sm_init(kPio, sm, pio_offset, &config) != 0) {
    Serial.println("GPU PIO: state machine configuration failed.");
    return false;
  }
  // Set idle levels before giving PIO control of the pads. WREADY and
  // RVALID remain SIO inputs; PIO samples them without changing the mux.
  pio_sm_set_pins_with_mask64(kPio, sm, kIdlePins, kBusPins);
  pio_sm_set_pindirs_with_mask64(kPio, sm, kControlPins, kBusPins);
  for (unsigned pin : {kWrite, kRead, kAddress, kClock}) pio_gpio_init(kPio, pin);
  for (unsigned bit = 0; bit < 8; ++bit) pio_gpio_init(kPio, kData + bit);
  pio_sm_set_enabled(kPio, sm, true);
  pio_active = true;
  return true;
}

bool receive_word(uint32_t &word, uint32_t start) {
  while (pio_sm_is_rx_fifo_empty(kPio, (uint)pio_sm)) {
    if ((uint32_t)(micros() - start) >= kTimeoutUs) return false;
  }
  word = pio_sm_get(kPio, (uint)pio_sm);
  return true;
}

bool write_cycle(uint8_t byte, bool address) {
  if (!enable_pio()) return false;
  if (address) address_sent = byte;
  const uint32_t command = ((uint32_t)byte << gpu_bus_write_data_shift) |
                           (0xffu << gpu_bus_write_dirs_shift) |
                           ((uint32_t)address << gpu_bus_write_address_shift);
  // Each transfer waits for its completion, so the TX FIFO has room.
  const uint32_t start = micros();
  pio_sm_put(kPio, (uint)pio_sm, command);
  uint32_t pads;
  if (!receive_word(pads, start)) {
    if (address) address_sensed = read_data_pins();
    release_bus();
    return false;
  }
  if (address) address_sensed = (uint8_t)pads;
  service();
  return true;
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
  const uint32_t start = micros();
  pio_sm_put(kPio, (uint)pio_sm, 1u);
  uint32_t pre, result;
  if (!receive_word(pre, start) || !receive_word(result, start)) {
    release_bus();
    return false;
  }
  read_pre_valid = (pre & (1u << gpu_bus_valid_bit)) != 0;
  read_edges = (0u - result) & ((1u << gpu_bus_edge_bits) - 1u);
  data_pins = (uint8_t)(result >> gpu_bus_edge_bits);
  *out = data_pins;
  service();
  return true;
}
bool read_byte(void *, uint8_t *out) { return read_at(0, out); }
// Offline diagnostics never run while the client is online or a hold is active.
bool refuse_diagnostic(const char *name) {
  if (gpu.ready()) {
    Serial.print(name); Serial.println(" is for an offline link; use GPU_STATUS."); return true;
  }
  if (read_held) { Serial.println("GPU_HOLD active; use GPU_RELEASE or GPU_RESET first."); return true; }
  if (drive_held) { Serial.println("GPU_DRIVE active; use GPU_RELEASE or GPU_RESET first."); return true; }
  return false;
}
// Hard-reset GPU logic exactly as startup does, after its configuration wait.
void pulse_gpu_reset() {
  digitalWrite(kReset, LOW);
  for (unsigned i = 0; i < 32; ++i) { high(); low(); }
  digitalWrite(kReset, HIGH);
  for (unsigned i = 0; i < 64; ++i) { high(); low(); }
}
bool read_register(void *, uint8_t address, uint8_t *out) { return read_at(address, out); }
uint32_t now_ms(void *) { return millis(); }
}

bool gpu_display_begin() {
  // Assert reset first: pinMode(OUTPUT_12MA) initially drives LOW. Keep GPU
  // logic reset while the other control outputs are configured.
  if (pio_active || read_held || drive_held) release_bus();
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
  pulse_gpu_reset();
  const bool ok = gpu.begin({nullptr, write_byte, read_byte, read_register, now_ms}, font);
  if (!ok) release_bus();
  gpu_display_diagnostics();
  if (!ok) gpu_display_probe();
  return ok;
}

void gpu_display_present() {
  if (!gpu.ready()) return;
  if (gpu.present_pending() && gpu.frame_ready() != NanoGpu::FrameReady) return;
  gpu_ui_prepare_terminal(gpu);
  if (!gpu.terminal_present(gridBuffer_char, char_grid_buffer_V_draw_offset)) {
    release_bus(); gpu_display_diagnostics(); gpu_display_regs();
  }
}

NanoGpu &gpu_display_client() { return gpu; }

bool gpu_display_abort() {
  if (!gpu.can_resume()) return false;
  release_bus();
  digitalWrite(kAbort, HIGH);
  for (unsigned i = 0; i < 4; ++i) { high(); low(); }
  digitalWrite(kAbort, LOW);
  if (gpu.resume_after_abort()) return true;
  release_bus();
  return false;
}

namespace {
uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
// One coherent status snapshot: register 1 requests it, 2..63 read the same copy.
bool read_status_snapshot(uint8_t status[64]) {
  status[0] = 0;
  for (unsigned address = 1; address < 64; ++address)
    if (!read_at(address, status + address)) {
      Serial.print("GPU REGS: timeout at register "); Serial.println(address); return false;
    }
  return true;
}
void print_status_snapshot(const char *label, const uint8_t s[64]) {
  const uint8_t f = s[1];
  Serial.print("GPU REGS "); Serial.print(label); Serial.print(": flags=0x"); Serial.print(f, HEX);
  Serial.print(" [");
  Serial.print((f & 0x01) ? "ready " : ""); Serial.print((f & 0x02) ? "busy " : "");
  Serial.print((f & 0x04) ? "sealed " : ""); Serial.print((f & 0x08) ? "core-fault " : "");
  Serial.print((f & 0x10) ? "reply-available " : ""); Serial.print((f & 0x20) ? "queue-full " : "");
  Serial.print((f & 0x40) ? "rx-full " : ""); Serial.print((f & 0x80) ? "transport-fault" : "");
  Serial.print("]; epoch="); Serial.print(le32(s + 2));
  Serial.print("; completed="); Serial.print(le32(s + 6));
  Serial.print("; fence="); Serial.print(le32(s + 10));
  Serial.print("; front="); Serial.print(s[14]);
  Serial.print("; cycles="); Serial.print(le32(s + 16));
  Serial.print("; core_fault="); Serial.print(le32(s + 32));
  Serial.print("; present="); Serial.print(le32(s + 36));
  Serial.print("; link_fault="); Serial.println(s[56]);
}
}

bool gpu_display_link_status(uint8_t out[64]) {
  return !read_held && !drive_held && read_status_snapshot(out);
}

void gpu_display_regs() {
  if (read_held) { Serial.println("GPU_HOLD active; use GPU_RELEASE or GPU_RESET first."); return; }
  if (drive_held) { Serial.println("GPU_DRIVE active; use GPU_RELEASE or GPU_RESET first."); return; }
  // Sideband status reads only: no packet is sent and no reply byte is consumed.
  Serial.print("GPU CLIENT: last_sent="); Serial.print(gpu.last_sequence());
  Serial.print("; awaited_opcode="); Serial.print(gpu.awaited_opcode());
  Serial.print("; awaited_sequence="); Serial.println(gpu.awaited_sequence());
  uint8_t first[64], second[64];
  if (!read_status_snapshot(first)) return;
  print_status_snapshot("t0", first);
  delay(500); // A second snapshot shows whether the core is still progressing.
  if (!read_status_snapshot(second)) return;
  print_status_snapshot("t+500ms", second);
  Serial.print("GPU REGS delta: cycles=+"); Serial.print(le32(second + 16) - le32(first + 16));
  Serial.print("; completed=+"); Serial.println(le32(second + 6) - le32(first + 6));
  release_bus();
}

void gpu_display_diagnostics() {
  if (read_held || drive_held) data_pins = read_data_pins();
  Serial.print("NANO GPU: "); Serial.print(gpu.ready() ? "READY" : "OFFLINE");
  Serial.print("; "); Serial.print(gpu.error());
  Serial.print("; presents="); Serial.print(gpu.presents());
  Serial.print("; hclk_target_hz="); Serial.print(kHostClockHz);
  Serial.print("; reply_wait_ms="); Serial.print(gpu.last_reply_wait_ms());
  Serial.print("/"); Serial.print(gpu.max_reply_wait_ms());
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
  Serial.print("GPU BUS: held="); Serial.print(read_held ? 1 : 0);
  Serial.print("; drive="); Serial.print(drive_held ? 1 : 0);
  Serial.print("; HCLK="); Serial.print(digitalRead(kClock));
  Serial.print("; WR_N="); Serial.print(digitalRead(kWrite));
  Serial.print("; RD_N="); Serial.print(digitalRead(kRead));
  Serial.print("; ALE_N="); Serial.print(digitalRead(kAddress));
  Serial.print("; ABORT="); Serial.println(digitalRead(kAbort));
}

void gpu_display_probe() {
  if (refuse_diagnostic("GPU_PROBE")) return;
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
  gpu_display_swaptest();
  gpu_display_diagnostics();
}

void gpu_display_swaptest() {
  if (refuse_diagnostic("GPU_SWAPTEST")) return;
  release_bus();
  // Refresh the status snapshot, then compare status and capability aliases.
  // These are sideband register reads, never command packets or a remap.
  // Edge counts tell a real FPGA handshake from a clock-linked RVALID: only
  // register 0x01 waits for a core-clock snapshot (several HCLK edges).
  const uint8_t addresses[] = {1, 2, 0x21, 0x23};
  uint8_t values[4];
  unsigned edges[4];
  bool pre[4];
  for (unsigned i = 0; i < 4; ++i) {
    if (!read_at(addresses[i], values + i)) {
      Serial.print("GPU SWAPTEST: timeout at register 0x");
      Serial.println(addresses[i], HEX);
      return; // read_at already released the bus on timeout.
    }
    edges[i] = read_edges; pre[i] = read_pre_valid;
  }
  Serial.print("GPU SWAPTEST:");
  for (unsigned i = 1; i < 4; ++i) {
    Serial.print(" "); Serial.print(values[i], HEX);
  }
  Serial.print("; flags=0x"); Serial.print(values[0], HEX);
  Serial.print("; edges=");
  for (unsigned i = 0; i < 4; ++i) { if (i) Serial.print(" "); Serial.print(edges[i]); }
  Serial.print("; pre=");
  for (unsigned i = 0; i < 4; ++i) { if (i) Serial.print(" "); Serial.print(pre[i] ? 1 : 0); }
  Serial.println("");
  release_bus();
}

void gpu_display_latchtest() {
  if (refuse_diagnostic("GPU_LATCHTEST")) return;
  release_bus();
  // Fresh GPU reset, then capability reads that never start at 0x40. Each
  // address differs from the previous one, so every read writes a new latch.
  pulse_gpu_reset();
  const uint8_t addresses[] = {0x41, 0x42, 0x43, 0x41};
  uint8_t values[4];
  unsigned edges[4];
  for (unsigned i = 0; i < 4; ++i) {
    if (!read_at(addresses[i], values + i)) {
      Serial.print("GPU LATCHTEST: timeout at register 0x");
      Serial.println(addresses[i], HEX);
      return; // read_at already released the bus on timeout.
    }
    edges[i] = read_edges;
  }
  Serial.print("GPU LATCHTEST:");
  for (unsigned i = 0; i < 4; ++i) { Serial.print(" "); Serial.print(values[i], HEX); }
  Serial.print("; edges=");
  for (unsigned i = 0; i < 4; ++i) { if (i) Serial.print(" "); Serial.print(edges[i]); }
  Serial.println("; expected=2 8 20 2");
  release_bus();
}

void gpu_display_drive(uint8_t byte) {
  if (gpu.ready()) { Serial.println("GPU_DRIVE is for an offline link; use GPU_STATUS."); return; }
  // Start from released strobes with HCLK LOW, then stop an address write
  // before its rising edge. No edge occurs, so the GPU latches nothing; RD_N
  // stays HIGH, so the GPU does not drive the data pins.
  release_bus();
  write_data(byte);
  digitalWrite(kAddress, LOW); digitalWrite(kWrite, LOW);
  delayMicroseconds(kDiagnosticPhaseUs);
  drive_held = true;
  Serial.print("GPU DRIVE: address phase held with 0x"); Serial.print(byte, HEX);
  Serial.println("; HCLK LOW, no clock edge; use GPU_RELEASE to end.");
  gpu_display_diagnostics();
}

void gpu_display_hold() {
  if (gpu.ready()) { Serial.println("GPU_HOLD is for an offline link; use GPU_STATUS."); return; }
  release_bus();
  // RVALID must stay LOW with RD_N HIGH, regardless of HCLK. This also
  // distinguishes a real read handshake from a clock-linked input.
  low();
  const int idle_low = digitalRead(kValid);
  high();
  const int idle_high = digitalRead(kValid);
  low();
  Serial.print("GPU IDLE: RVALID_LOWCLK="); Serial.print(idle_low);
  Serial.print("; RVALID_HIGHCLK="); Serial.print(idle_high);
  Serial.println("; expected=0/0");
  // Force a fresh address latch. This is a sideband read, never a packet.
  if (!select(65)) { Serial.println("GPU HOLD: address timeout; bus released."); return; }
  // PIO completed the address latch. Keep that FPGA address while returning
  // the pads to SIO for the manual read hold.
  release_bus();
  digitalWrite(kWrite, HIGH); digitalWrite(kRead, HIGH); release_data(); low();
  digitalWrite(kAddress, HIGH); digitalWrite(kRead, LOW); delayMicroseconds(kDiagnosticPhaseUs);
  const uint32_t start = micros();
  bool valid;
  for (;;) {
    high();
    valid = digitalRead(kValid) == HIGH;
    if (valid || (uint32_t)(micros() - start) >= kTimeoutUs) break;
    low();
  }
  // Deliberately keep the read active, even for bad data or missing RVALID,
  // so a meter can measure stable control/data levels at both boards.
  read_held = true;
  Serial.print("GPU HOLD: register=0x41; expected=0x02; ");
  Serial.println(valid ? "read held; use GPU_RELEASE to end." :
                         "RVALID timeout; read held; use GPU_RELEASE to end.");
  gpu_display_diagnostics();
}

void gpu_display_release() {
  if (!read_held && !drive_held) { Serial.println("GPU RELEASE: no hold active."); return; }
  // release_bus raises WR_N/ALE_N/RD_N before releasing data; HCLK is already
  // LOW for a drive hold, so ending it adds no rising edge.
  release_bus();
  Serial.println("GPU RELEASE: bus released; client remains offline.");
}
