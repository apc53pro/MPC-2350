#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include <hardware/clocks.h>
#include <hardware/vreg.h>
#include <hardware/regs/qmi.h>
#include <hardware/structs/qmi.h>
#include "pico/platform.h"
#include "hardware/sync.h"
#include "hardware/structs/xip_ctrl.h"
#include "globals.h"
#include "draw_funcs.h"
#include "hsync.pio.h"
#include "vsync.pio.h"
#include "rgb.pio.h"
#include "mms.h"
#include "ps2_keyboard_driver.h"
#include "keyboard_runtime.h"
#include "terminal.h"
#include "font.h"
#include "audio_runtime.h"
#include "graphics_3d_runtime.h"
#include "native_runtime.h"
#include "system_overlay.h"
#include "usb_storage_mode.h"
#include <FatFSFlash.h>

// ================= RP2350 clock / external bus configuration =================
// Stay below the marginal 420 MHz core profile, but keep QMI memory clocks fast.
#define RP2350_CPU_KHZ 408000UL
#define RP2350_CPU_HZ  408000000UL

// QMI clock = clk_sys / divider.
// At 408 MHz: flash = 102 MHz, PSRAM = 102 MHz.
#define RP2350_FLASH_QMI_DIV 4u
#define RP2350_PSRAM_SAFE_QMI_DIV 10u
#define RP2350_PSRAM_QMI_DIV 4u
#define RP2350_PSRAM_RXDELAY 5u

// Direct serial mode is separate from the memory mapped QMI windows.
// Keep it conservative: 408 / 20 = 20.4 MHz for command/control transactions.
#define RP2350_QMI_DIRECT_DIV 20u

// Keep the high-voltage profile while testing overclock stability under VGA/DMA load.
#define RP2350_VREG_TARGET VREG_VOLTAGE_1_35

// Keep the VGA timing engines independent from CPU overclocking.
#define VGA_PIO_TIMING_HZ 25175000.0f

// The RGB PIO program was delay-tuned for the old 300 MHz full-speed PIO clock.
// At 408 MHz the divider becomes 1.36, preserving the same RGB timing.
#define RGB_PIO_TIMING_HZ 300000000.0f
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

bool terminal_on = true;

int SYS_HALF_RES_BUFFER_PARTITION_INDEX;
int SYS_Z_BUFFER_PARTITION_INDEX;
int SYS_Z_STAMP_BUFFER_PARTITION_INDEX;

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

static inline void __not_in_flash_func(qmi_set_direct_clkdiv)(uint32_t div)
{
  qmi_hw->direct_csr = (qmi_hw->direct_csr & ~QMI_DIRECT_CSR_CLKDIV_BITS) | ((div & 0xffu) << QMI_DIRECT_CSR_CLKDIV_LSB);
}

static inline void __not_in_flash_func(qmi_set_flash_clkdiv)(uint32_t div)
{
  qmi_hw->m[0].timing = (qmi_hw->m[0].timing & ~QMI_M0_TIMING_CLKDIV_BITS) | ((div & 0xffu) << QMI_M0_TIMING_CLKDIV_LSB);
}

static inline void __not_in_flash_func(qmi_set_psram_clkdiv)(uint32_t div)
{
  // The M0 and M1 timing registers use the same CLKDIV bit layout. Some SDK
  // header versions only expose the M0 names, so keep the M0 fallback.
  #if defined(QMI_M1_TIMING_CLKDIV_BITS) && defined(QMI_M1_TIMING_CLKDIV_LSB)
  qmi_hw->m[1].timing = (qmi_hw->m[1].timing & ~QMI_M1_TIMING_CLKDIV_BITS)
                      | ((div & 0xffu) << QMI_M1_TIMING_CLKDIV_LSB);
  #else
  qmi_hw->m[1].timing = (qmi_hw->m[1].timing & ~QMI_M0_TIMING_CLKDIV_BITS) | ((div & 0xffu) << QMI_M0_TIMING_CLKDIV_LSB);
  #endif
}

static inline void __not_in_flash_func(qmi_set_psram_rxdelay)(uint32_t rxdelay)
{
  #if defined(QMI_M1_TIMING_RXDELAY_BITS) && defined(QMI_M1_TIMING_RXDELAY_LSB)
  qmi_hw->m[1].timing = (qmi_hw->m[1].timing & ~QMI_M1_TIMING_RXDELAY_BITS)
                      | ((rxdelay & 0x7u) << QMI_M1_TIMING_RXDELAY_LSB);
  #else
  qmi_hw->m[1].timing = (qmi_hw->m[1].timing & ~QMI_M0_TIMING_RXDELAY_BITS) | ((rxdelay & 0x7u) << QMI_M0_TIMING_RXDELAY_LSB);
  #endif
}

static void __not_in_flash_func(qmi_apply_timing)(uint32_t psram_div)
{
  qmi_set_flash_clkdiv(RP2350_FLASH_QMI_DIV);
  qmi_set_psram_clkdiv(psram_div);
  qmi_set_psram_rxdelay(RP2350_PSRAM_RXDELAY);
  qmi_set_direct_clkdiv(RP2350_QMI_DIRECT_DIV);

  rp2350_clock_barrier();

  // Force one flash XIP transaction after changing QMI timing. RP2350 QMI
  // divider changes can otherwise be delayed past the following clock switch.
  volatile uint32_t flash_dummy = *(volatile uint32_t *)XIP_BASE;
  (void)flash_dummy;

  rp2350_clock_barrier();

  #if defined(XIP_CTRL_WRITABLE_M1_BITS)
  xip_ctrl_hw->ctrl |= XIP_CTRL_WRITABLE_M1_BITS;
  #endif

  rp2350_clock_barrier();
}

static void __not_in_flash_func(rp2350_apply_clock_stage)(uint32_t sys_hz,
                                                          uint32_t vco_hz,
                                                          uint32_t postdiv1,
                                                          uint32_t postdiv2,
                                                          uint32_t psram_div)
{
  uint32_t irq_state = save_and_disable_interrupts();

  qmi_apply_timing(psram_div);
  set_sys_clock_pll(vco_hz, postdiv1, postdiv2);
  qmi_apply_timing(psram_div);

  clock_configure(clk_peri,
                  0,
                  CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  sys_hz,
                  sys_hz);

  restore_interrupts(irq_state);
}

static void __not_in_flash_func(apply_408mhz_clocking)(void)
{
  // Do not run this as a global constructor. If the clock/QMI transition fails,
  // setup() can still prove it was reached by lighting GP0 first.
  vreg_disable_voltage_limit();
  vreg_set_voltage(RP2350_VREG_TARGET);

  // Stage up with conservative PSRAM timing while changing PLLs.
  rp2350_apply_clock_stage(300000000u, 1200000000u, 4u, 1u, RP2350_PSRAM_SAFE_QMI_DIV);
  rp2350_apply_clock_stage(360000000u, 1440000000u, 4u, 1u, RP2350_PSRAM_SAFE_QMI_DIV);
  rp2350_apply_clock_stage(RP2350_CPU_HZ, 1224000000u, 3u, 1u, RP2350_PSRAM_SAFE_QMI_DIV);

  // Final external-bus settings for the firmware:
  // flash  = 408 / 4 = 102 MHz
  // PSRAM  = 408 / 4 = 102 MHz, with RXDELAY 5
  // direct = 408 / 20 = 20.4 MHz
  qmi_apply_timing(RP2350_PSRAM_QMI_DIV);
}

void setup() 
{
  // Immediate sign that setup() was reached before the overclock attempt.
  pinMode(0, OUTPUT);
  digitalWrite(0, HIGH);

  apply_408mhz_clocking();

  graphics_mode = false;
  keyboard_runtime_reset();
  Serial.begin(115200);
 
  // GP0 was already enabled before the overclock attempt.
  // The PS/2 driver runs on Core 1; setup1() installs its clock ISR.

  memset(gridBuffer_backColor, 255, sizeof(gridBuffer_backColor));
  memset(gridBuffer_char, ' ', sizeof(gridBuffer_char));
  memset(gridBuffer_charColor, 0b00011100, sizeof(gridBuffer_charColor));

  clearVga4BitBuffer(0);
  presentVga4BitBuffer();

  PIO pio = pio0;
  uint hsync_offset = pio_add_program(pio, &hsync_program);
  uint vsync_offset = pio_add_program(pio, &vsync_program);
  uint rgb_offset = pio_add_program(pio, &rgb_program);

  // Manually select a few state machines from pio instance pio0.
  uint hsync_sm = 0;
  uint vsync_sm = 1;
  uint rgb_sm = 2;

  // Call the initialization functions that are defined within each PIO file.
  // Why not create these programs here? By putting the initialization function in
  // the pio file, then all information about how to use/setup that state machine
  // is consolidated in one place. Here in the C, we then just import and use it.
  hsync_program_init(pio, hsync_sm, hsync_offset, HSYNC);
  vsync_program_init(pio, vsync_sm, vsync_offset, VSYNC);
  rgb_program_init(pio, rgb_sm, rgb_offset, 18);


  /////////////////////////////////////////////////////////////////////////////////////////////////////
  // ============================== PIO DMA Channels =================================================
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  // DMA channels - 0 sends color data, 1 reconfigures and restarts 0
  int rgb_chan_0 = dma_claim_unused_channel(true);
  int rgb_chan_1 = dma_claim_unused_channel(true);

  // Channel Zero (sends color data to PIO VGA machine)
  dma_channel_config c0 = dma_channel_get_default_config(rgb_chan_0);  // default configs
  channel_config_set_transfer_data_size(&c0, DMA_SIZE_8);              // 8-bit txfers
  channel_config_set_read_increment(&c0, true);                        // yes read incrementing
  channel_config_set_write_increment(&c0, false);                      // no write incrementing
  channel_config_set_dreq(&c0, DREQ_PIO0_TX2) ;                        // DREQ_PIO0_TX2 pacing (FIFO)
  channel_config_set_chain_to(&c0, rgb_chan_1);                        // chain to other channel

  dma_channel_configure(
      rgb_chan_0,                 // Channel to be configured
      &c0,                        // The configuration we just created
      &pio->txf[rgb_sm],          // write address (RGB PIO TX FIFO)
      &vga_data_array,            // The initial read address (pixel color array)
      TXCOUNT,                    // Number of transfers; in this case each is 1 byte.
      false                       // Don't start immediately.
  );

  // Channel One (reconfigures the first channel)
  dma_channel_config c1 = dma_channel_get_default_config(rgb_chan_1);   // default configs
  channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);              // 32-bit txfers
  channel_config_set_read_increment(&c1, false);                        // no read incrementing
  channel_config_set_write_increment(&c1, false);                       // no write incrementing
  channel_config_set_chain_to(&c1, rgb_chan_0);                         // chain to other channel

  dma_channel_configure(
      rgb_chan_1,                         // Channel to be configured
      &c1,                                // The configuration we just created
      &dma_hw->ch[rgb_chan_0].read_addr,  // Write address (channel 0 read address)
      &address_pointer,                   // Read address (POINTER TO AN ADDRESS)
      1,                                  // Number of transfers, in this case each is 4 byte
      false                               // Don't start immediately.
  );

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  // Initialize PIO state machine counters. This passes the information to the state machines
  // that they retrieve in the first 'pull' instructions, before the .wrap_target directive
  // in the assembly. Each uses these values to initialize some counting registers.
  pio_sm_put_blocking(pio, hsync_sm, H_ACTIVE);
  pio_sm_put_blocking(pio, vsync_sm, V_ACTIVE);
  pio_sm_put_blocking(pio, rgb_sm, RGB_ACTIVE);


  // Start the PIO machines in sync. RGB has its own divider derived from clk_sys.
  pio_enable_sm_mask_in_sync(pio, ((1u << hsync_sm) | (1u << vsync_sm) | (1u << rgb_sm)));

  // Start DMA channel 0. Once started, the contents of the pixel color array
  // will be continously DMA's to the PIO machines that are driving the screen.
  // To change the contents of the screen, we need only change the contents
  // of that array.
  dma_start_channel_mask((1u << rgb_chan_0));

  //WRSR(1); //FOR THE LOVE OF GOD REMEMBER TO WRITE 1 INSTEAD OF 0 OR ELSE THE RAM STATUS REGISTER WILL ACTIVATE THE RAM HOLD PIN FEATURE AND FUCK EVERYTHING MAKING FOR UNPREDICTABLE BEHAVIOUR
  //CLEAR_RAM();

  //This function allocates system memory space and fills it with the system font
  fillMemoryWithFont();

  //This below allocates system memory for the half res buffer
  SYS_HALF_RES_BUFFER_PARTITION_INDEX = alloc_partition("SYS_HALF_RES_BUFFER", SYSTEM, 19200);
  SYS_Z_BUFFER_PARTITION_INDEX = alloc_partition("SYS_Z_BUFFER", SYSTEM, 76800);
  SYS_Z_STAMP_BUFFER_PARTITION_INDEX = alloc_partition("SYS_Z_STAMP_BUFFER", SYSTEM, 19200);

  
  //STARTUP TERMINAL MESSAGE
  echo_message("====== UNNAMED RP2350 COMPUTER");
  echo_message("====== SYSTEM MEMORY: " + String((float)(memory_size*4)/(float)1000) + "KB");
  echo_message("====== PRIMARY RUNTIME: NARM");
  echo_message("");

  // Release Core 1 to initialize I2S only after Core 0 has finished all
  // startup allocations and constructed the terminal banner. Until this
  // point setup1() continues servicing the keyboard while it waits.
  audio_runtime_init();
  
}

void setup1()
{
  //SETUP PS/2 COMMUNICATION
  pinMode(PS2_CLOCK, INPUT_PULLUP); //PS2 CLOCK
  pinMode(PS2_DATA, INPUT_PULLUP); //PS2 DATA

  attachInterrupt(digitalPinToInterrupt(PS2_CLOCK), ps2_keyboard_clock_isr, FALLING);

  // I2S is started lazily from loop1() after Core 0 publishes readiness.
  // Calling audio_core1_begin() here is too early and only returns false.
}

bool run = false;
String ARG_STRUCTURE;
String returnType;
String returnContentField;

int height = 100;
int width = 70;

int rpt = 0;

uint32_t us;
void loop() 
{
  usb_storage_poll();
  audio_core0_poll();
  native_scheduler_poll();
  if(native_terminal_has_focus() && graphics_mode == false)
  {
    terminal(true, true, true);
  }

  if(native_terminal_has_focus() && graphics_mode == true)
  {
    draw_half_res_buffer();
  }

  system_overlay_poll();

  audio_core0_poll();
  
}

void loop1()
{
  fatfs_flash_core1_poll();
  ps2_keyboard_poll();

  // Never hold keyboard startup behind audio readiness. Once Core 0 has
  // finished initialization, this starts I2S exactly once; later calls are
  // inexpensive because audio_core1_begin() immediately returns true.
  if(audio_runtime_ready())
  {
    audio_core1_begin();
  }

  audio_core1_poll();
  tri3d_core1_worker_poll();
  audio_core1_poll();
}
