#include "pico/stdlib.h"
#include <hardware/clocks.h>
#include <hardware/vreg.h>
#include <hardware/regs/qmi.h>
#include <hardware/structs/qmi.h>
#include "pico/platform.h"
#include "hardware/sync.h"
#include "hardware/structs/xip_ctrl.h"
#include "rp2350_qmi_timing.h"
#include "globals.h"
#include "mms.h"
#include "ps2_keyboard_driver.h"
#include "ps2_pio_transport.h"
#include "mouse_runtime.h"
#include "keyboard_runtime.h"
#include "terminal.h"
#include "gpu_display.h"
#include "terminal_funcs.h"
#include "audio_runtime.h"
#include "native_runtime.h"
#include "native_fault_capture.h"
#include "usb_storage_mode.h"
#include <FatFSFlash.h>

// ================= RP2350 clock / external bus configuration =================
// Change this one value to switch the native crash test profile.
#define RP2350_TEST_CPU_MHZ 375

#if RP2350_TEST_CPU_MHZ == 408
// Flash 102 MHz, PSRAM 51 MHz.
#define RP2350_FLASH_QMI_DIV 4u
#define RP2350_PSRAM_QMI_DIV 8u
#elif RP2350_TEST_CPU_MHZ == 375
// Flash 93.75 MHz, PSRAM 125 MHz.
#define RP2350_FLASH_QMI_DIV 4u
#define RP2350_PSRAM_QMI_DIV 3u
#elif RP2350_TEST_CPU_MHZ == 350
// Flash 87.5 MHz, PSRAM 87.5 MHz.
#define RP2350_FLASH_QMI_DIV 4u
#define RP2350_PSRAM_QMI_DIV 4u
#elif RP2350_TEST_CPU_MHZ == 300
// Flash 100 MHz, PSRAM 100 MHz.
#define RP2350_FLASH_QMI_DIV 3u
#define RP2350_PSRAM_QMI_DIV 3u
#else
#error RP2350_TEST_CPU_MHZ must be 300, 350, 375, or 408
#endif

#define RP2350_CPU_KHZ (RP2350_TEST_CPU_MHZ * 1000UL)
#define RP2350_CPU_HZ  (RP2350_TEST_CPU_MHZ * 1000000UL)
#define RP2350_FLASH_SAFE_QMI_DIV 4u
#define RP2350_PSRAM_SAFE_QMI_DIV 10u
#define RP2350_PSRAM_RXDELAY 5u

// Direct serial mode is separate from the memory mapped QMI windows.
// The direct serial clock is 15 MHz at 300 MHz and 20.4 MHz at 408 MHz.
#define RP2350_QMI_DIRECT_DIV 20u

#define RP2350_VREG_TARGET VREG_VOLTAGE_1_35
// ============================================================================

#ifndef XIP_BASE
  #ifdef XIP_MAIN_BASE
    #define XIP_BASE XIP_MAIN_BASE
  #else
    #define XIP_BASE 0x10000000u
  #endif
#endif

SdFat SD;

uint8_t PS2_CLOCK = 28;
uint8_t PS2_DATA = 27;

// PS/2 mouse uses GPIO1/26; GPU control and handshakes use GPIO35..40.
static constexpr uint8_t PS2_MOUSE_CLOCK = 1;
static constexpr uint8_t PS2_MOUSE_DATA = 26;
static uint32_t ps2_clock_ready = 0;
static uint32_t core1_clock_parked = 0;
static uint32_t system_clocks_ready = 0;

// Acknowledge only after Core 1 is in SRAM with its IRQs disabled. Core 0
// may then make QMI idle without a competing flash instruction fetch.
static void __no_inline_not_in_flash_func(rp2350_wait_for_clock_setup)()
{
  const uint32_t irq_state = save_and_disable_interrupts();
  __atomic_store_n(&core1_clock_parked, 1u, __ATOMIC_RELEASE);
  while (!__atomic_load_n(&system_clocks_ready, __ATOMIC_ACQUIRE))
    __asm volatile ("nop");
  restore_interrupts(irq_state);
}

static bool rp2350_wait_for_core1_clock_park()
{
  const uint32_t start = time_us_32();
  while (!__atomic_load_n(&core1_clock_parked, __ATOMIC_ACQUIRE))
    if ((uint32_t)(time_us_32() - start) >= 100000u) return false;
  return true;
}

bool terminal_on = true;


#ifndef QMI_M0_TIMING_RXDELAY_BITS
  #define QMI_M0_TIMING_RXDELAY_LSB 8u
  #define QMI_M0_TIMING_RXDELAY_BITS (0x7u << QMI_M0_TIMING_RXDELAY_LSB)
#endif

static inline void __not_in_flash_func(rp2350_clock_barrier)(void)
{
  #if defined(__arm__) || defined(__thumb__) || defined(__ARM_ARCH)
  __asm volatile ("dsb sy" ::: "memory");
  __asm volatile ("isb sy" ::: "memory");
  #elif defined(__riscv)
  __asm volatile ("fence iorw, iorw" ::: "memory");
  __asm volatile ("fence.i" ::: "memory");
  #else
  __sync_synchronize();
  #endif
}

static inline void __not_in_flash_func(rp2350_clock_wait)(volatile uint32_t loops)
{
  while(loops--)
  {
    __asm volatile ("nop");
  }
}

static constexpr uint32_t qmi_psram_cs_fields(Rp2350PsramCsTiming timing)
{
  return (timing.max_select << QMI_M1_TIMING_MAX_SELECT_LSB) |
         (timing.min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB);
}

static void __no_inline_not_in_flash_func(qmi_apply_timing)(uint32_t psram_div,
                                                           uint32_t cs_fields,
                                                           uint32_t flash_div)
{
  // Only CLKDIV may change during an active memory transfer. Before changing
  // RXDELAY or CS timings, block new XIP requests and drain the current one.
  // Core 1 is parked in SRAM; audio DMA has not been started yet.
  const uint32_t irq_state = save_and_disable_interrupts();
  const uint32_t direct_csr =
      (qmi_hw->direct_csr & ~QMI_DIRECT_CSR_CLKDIV_BITS) |
      (RP2350_QMI_DIRECT_DIV << QMI_DIRECT_CSR_CLKDIV_LSB);
  qmi_hw->direct_csr = direct_csr | QMI_DIRECT_CSR_EN_BITS;
  while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS)
    __asm volatile ("nop");

  qmi_hw->m[0].timing =
      (qmi_hw->m[0].timing & ~QMI_M0_TIMING_CLKDIV_BITS) |
      (flash_div << QMI_M0_TIMING_CLKDIV_LSB);
  qmi_hw->m[1].timing =
      (qmi_hw->m[1].timing & ~(QMI_M1_TIMING_CLKDIV_BITS |
                               QMI_M1_TIMING_RXDELAY_BITS |
                               QMI_M1_TIMING_MAX_SELECT_BITS |
                               QMI_M1_TIMING_MIN_DESELECT_BITS)) |
      (psram_div << QMI_M1_TIMING_CLKDIV_LSB) |
      (RP2350_PSRAM_RXDELAY << QMI_M1_TIMING_RXDELAY_LSB) | cs_fields;

  // No flash/PSRAM access or non-SRAM function call while direct mode is on.
  rp2350_clock_barrier();
  qmi_hw->direct_csr = direct_csr & ~QMI_DIRECT_CSR_EN_BITS;
  rp2350_clock_barrier();
  // Bypass the XIP cache: a cached read cannot prove the new divider latched.
  const volatile uint32_t flash_dummy =
      *(volatile uint32_t *)XIP_NOCACHE_NOALLOC_BASE;
  (void)flash_dummy;
  rp2350_clock_barrier();

  #if defined(XIP_CTRL_WRITABLE_M1_BITS)
  xip_ctrl_hw->ctrl |= XIP_CTRL_WRITABLE_M1_BITS;
  #endif
  rp2350_clock_barrier();
  restore_interrupts(irq_state);
}

static void __not_in_flash_func(rp2350_apply_clock_stage)(uint32_t sys_hz,
                                                          uint32_t vco_hz,
                                                          uint32_t postdiv1,
                                                          uint32_t postdiv2,
                                                          uint32_t psram_div)
{
  const uint32_t old_hz = clock_get_hz(clk_sys);
  const uint32_t before_fields = qmi_psram_cs_fields(
      rp2350_psram_cs_transition(old_hz, sys_hz, psram_div));
  const uint32_t after_fields = qmi_psram_cs_fields(
      rp2350_psram_cs_timing(sys_hz, psram_div));
  uint32_t irq_state = save_and_disable_interrupts();

  qmi_apply_timing(psram_div, before_fields, RP2350_FLASH_SAFE_QMI_DIV);
  set_sys_clock_pll(vco_hz, postdiv1, postdiv2);
  qmi_apply_timing(psram_div, after_fields, RP2350_FLASH_SAFE_QMI_DIV);

  clock_configure(clk_peri,
                  0,
                  CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  sys_hz,
                  sys_hz);

  restore_interrupts(irq_state);
}

static bool __not_in_flash_func(apply_clock_profile)(void)
{
  // Refuse an unsupported startup clock instead of disabling the CS limit.
  if (!rp2350_psram_cs_timing(clock_get_hz(clk_sys),
                             RP2350_PSRAM_SAFE_QMI_DIV).valid()) return false;
  // Do not run this as a global constructor. If the clock/QMI transition fails,
  // setup() can still prove it was reached by lighting GP0 first.
  vreg_disable_voltage_limit();
  vreg_set_voltage(RP2350_VREG_TARGET);

  // Keep both bus dividers conservative until the PLL reaches the test clock.
#if RP2350_TEST_CPU_MHZ == 408
  rp2350_apply_clock_stage(300000000u, 1200000000u, 4u, 1u, RP2350_PSRAM_SAFE_QMI_DIV);
  rp2350_apply_clock_stage(360000000u, 1440000000u, 4u, 1u, RP2350_PSRAM_SAFE_QMI_DIV);
  rp2350_apply_clock_stage(RP2350_CPU_HZ, 1224000000u, 3u, 1u, RP2350_PSRAM_SAFE_QMI_DIV);
#elif RP2350_TEST_CPU_MHZ == 375
  rp2350_apply_clock_stage(RP2350_CPU_HZ, 1500000000u, 4u, 1u, RP2350_PSRAM_SAFE_QMI_DIV);
#elif RP2350_TEST_CPU_MHZ == 350
  rp2350_apply_clock_stage(RP2350_CPU_HZ, 1400000000u, 4u, 1u, RP2350_PSRAM_SAFE_QMI_DIV);
#else
  rp2350_apply_clock_stage(RP2350_CPU_HZ, 1200000000u, 4u, 1u, RP2350_PSRAM_SAFE_QMI_DIV);
#endif
  qmi_apply_timing(RP2350_PSRAM_QMI_DIV,
                   qmi_psram_cs_fields(rp2350_psram_cs_timing(
                       RP2350_CPU_HZ, RP2350_PSRAM_QMI_DIV)),
                   RP2350_FLASH_QMI_DIV);
  return true;
}

void setup() 
{
  // Immediate sign that setup() was reached before the overclock attempt.
  pinMode(0, OUTPUT);
  digitalWrite(0, HIGH);

  const bool clock_override_applied = rp2350_wait_for_core1_clock_park() &&
                                      apply_clock_profile();
  __atomic_store_n(&system_clocks_ready, 1u, __ATOMIC_RELEASE);

  keyboard_runtime_reset();
  __atomic_store_n(&ps2_clock_ready, 1u, __ATOMIC_RELEASE);
  Serial.begin(115200);
  Serial.print("QMI-STABILITY-R5 SYS_HZ=");
  Serial.print(clock_get_hz(clk_sys));
  Serial.print(" M1_TIMING=0x");
  Serial.println(qmi_hw->m[1].timing, HEX);
  if (!clock_override_applied)
    Serial.println("QMI-STABILITY-R5: override skipped (park/clock unavailable)");
 
  // GP0 was already enabled before the overclock attempt.
  // PIO receives PS/2 frames independently; Core 1 drains complete frames.

  memset(gridBuffer_char, ' ', sizeof(gridBuffer_char));
  gpu_display_begin();

  // Startup terminal message.
  echo_message("====== MPC-2350 / NANO-20K-GPU");
  echo_message("====== SYSTEM MEMORY: " + String((float)(memory_size*4)/(float)1000) + "KB");
  echo_message("====== NARM GPU RUNTIME");
  echo_message("");

  // Release Core 1 to initialize I2S only after Core 0 has finished all
  // startup allocations and constructed the terminal banner. Until this
  // point setup1() continues servicing the keyboard while it waits.
  gpu_display_present();
  audio_runtime_init();
  
}

void setup1()
{
  rp2350_wait_for_clock_setup();
  // The receiver divider must be calculated after Core 0's final PLL change.
  while(!__atomic_load_n(&ps2_clock_ready, __ATOMIC_ACQUIRE)) tight_loop_contents();
  mouse_runtime_init();
  ps2_pio_begin(PS2_CLOCK, PS2_DATA, PS2_MOUSE_CLOCK, PS2_MOUSE_DATA,
                ps2_keyboard_receive_byte, mouse_runtime_receive);

  // I2S is started lazily from loop1() after Core 0 publishes readiness.
  // Calling audio_core1_begin() here is too early and only returns false.
}

// USB serial remains available if the display link is absent or faults.
// Commands are bounded and use the same parser as PS/2 terminal input.
void loop()
{
  usb_storage_poll();
  audio_core0_poll();
  static String serial_line;
  unsigned serial_budget = 64;
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serial_line.length()) {
        if (!native_serial_dispatch(serial_line.c_str())) terminal_funcs(serial_line);
        serial_line = "";
      }
    } else if (serial_line.length() < 256) serial_line += c;
    if (--serial_budget == 0) break;
  }
  native_scheduler_poll();
  if (native_terminal_has_focus()) terminal(true, true, true);
  audio_core0_poll();
}

void loop1()
{
  fatfs_flash_core1_poll();
  ps2_keyboard_poll();
  if (audio_runtime_ready()) audio_core1_begin();
  audio_core1_poll();
}
