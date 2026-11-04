#include "native_fault_capture.h"
#include "native_fault_record.h"
#include "native_fault_details.h"

#include <Arduino.h>
#include <hardware/exception.h>
#include <hardware/structs/m33.h>
#include <hardware/structs/psm.h>
#include <hardware/structs/watchdog.h>
#include <hardware/structs/powman.h>
#include <hardware/structs/qmi.h>
#include <hardware/watchdog.h>
#include <hardware/sync.h>
#include <pico/platform.h>

#if defined(RP2350_PSRAM_CS)
extern size_t __psram_size;
#endif

namespace
{
NativeFaultRecord last_record = {};
NativeFaultDetails last_details = {};
bool last_details_valid = false;
uint32_t last_compact[NARM_COMPACT_WORDS] = {};
bool last_compact_valid = false;
bool initialized = false;
volatile bool watchdog_armed = false;
exception_handler_t previous_fault_handler = nullptr;
uint32_t active_phase = 0u;
uint32_t active_pid = 0u;
uint32_t fault_claimed = 0u;

void store_stall_context()
{
  watchdog_hw->scratch[2] = active_phase;
  watchdog_hw->scratch[3] = active_pid;
}

inline __attribute__((always_inline)) void store_basic_fault(const NativeFaultRecord &record)
{
  watchdog_hw->scratch[0] = 0u;
  watchdog_hw->scratch[1] = record.pc;
  watchdog_hw->scratch[2] = record.detail;
  watchdog_hw->scratch[3] = record.extra;
  watchdog_hw->scratch[0] = record.tag;
  __dsb();
}

inline __attribute__((always_inline)) void store_compact_fault(const NativeFaultRecord &record,
    uintptr_t sp, uint32_t exc_return, uint32_t hfsr, uint32_t xpsr, uint32_t r3)
{
  // POWMAN SCRATCH0..7 are general-purpose retention registers. BOOT0..3
  // and the SDK's watchdog SCRATCH4..7 reboot-routing fields are untouched.
  powman_hw->scratch[NARM_COMPACT_CHECKSUM] = 0u;
  powman_hw->scratch[NARM_COMPACT_SP] = (uint32_t)sp;
  powman_hw->scratch[NARM_COMPACT_EXC_RETURN] = exc_return;
  powman_hw->scratch[NARM_COMPACT_HFSR] = hfsr;
  powman_hw->scratch[NARM_COMPACT_PHASE] = active_phase;
  powman_hw->scratch[NARM_COMPACT_PID] = active_pid;
  powman_hw->scratch[NARM_COMPACT_XPSR] = xpsr;
  powman_hw->scratch[NARM_COMPACT_R3] = r3;
  powman_hw->scratch[NARM_COMPACT_CHECKSUM] =
      narm_fault_compact_checksum(powman_hw->scratch, record);
  __dsb();
}
}

extern "C"
{
alignas(8) uint8_t narm_fault_emergency_stacks[1024];
// This SRAM section is excluded from C startup clearing. Its integrity and
// agreement with the watchdog record are checked before anything is reported.
// The four watchdog scratch words remain the independent fallback record.
__attribute__((section(".uninitialized_data.narm_fault_details"), aligned(8)))
volatile NativeFaultDetails narm_fault_retained_details;
void narm_fault_vector();

void __attribute__((noreturn))
__no_inline_not_in_flash_func(narm_fault_capture_core)(uint32_t *frame,
                                                       uint32_t exc_return,
                                                       uint32_t exception)
{
  // No Serial, allocation, SDK reboot call, or flash instruction fetch here.
  // Optional PSRAM reads follow a committed hardware fallback and range checks.
  const uint32_t core = get_core_num();
  if (__atomic_exchange_n(&fault_claimed, 1u, __ATOMIC_ACQ_REL) == 0u)
  {
    const uint32_t cfsr = m33_hw->cfsr;
    const uint32_t hfsr = m33_hw->hfsr;
    uint32_t pc = 0u;
    uint32_t extra = exc_return;
    uint32_t tag = NARM_FAULT_TAG | (core & 1u) | ((exception & 63u) << 4);
    NativeFaultRecord basic = {tag, pc, cfsr, extra};
    store_basic_fault(basic);
    store_compact_fault(basic, (uintptr_t)frame, exc_return, hfsr, 0u, 0u);

    uintptr_t psram_end = 0x11000000u;
    #if defined(RP2350_PSRAM_CS)
    if (__psram_size <= 16u * 1024u * 1024u) psram_end += __psram_size;
    #endif
    const bool frame_valid = narm_fault_frame_readable((uintptr_t)frame, cfsr,
        psram_end, (qmi_hw->direct_csr & QMI_DIRECT_CSR_EN_BITS) == 0u);
    if (frame_valid)
    {
      pc = frame[6];
      extra = frame[5];
      tag |= 2u;
      basic.tag = tag;
      basic.pc = pc;
      basic.extra = extra;
      store_basic_fault(basic);
      store_compact_fault(basic, (uintptr_t)frame, exc_return, hfsr, frame[7], frame[3]);
    }
    narm_fault_retained_details.magic = 0u;
    // Volatile word stores prevent substitution of a flash-resident memset.
    for (uint32_t i = 0u; i < NARM_DETAIL_WORDS; ++i)
      narm_fault_retained_details.words[i] = 0u;
    narm_fault_retained_details.words[NARM_DETAIL_TAG] = tag;
    narm_fault_retained_details.words[NARM_DETAIL_PC] = pc;
    narm_fault_retained_details.words[NARM_DETAIL_CFSR] = cfsr;
    narm_fault_retained_details.words[NARM_DETAIL_EXTRA] = extra;
    narm_fault_retained_details.words[NARM_DETAIL_SP] = (uint32_t)(uintptr_t)frame;
    narm_fault_retained_details.words[NARM_DETAIL_EXC_RETURN] = exc_return;
    narm_fault_retained_details.words[NARM_DETAIL_HFSR] = hfsr;
    narm_fault_retained_details.words[NARM_DETAIL_PHASE] = active_phase;
    narm_fault_retained_details.words[NARM_DETAIL_PID] = active_pid;
    if (frame_valid)
    {
      for (uint32_t i = 0u; i < 8u; ++i)
        narm_fault_retained_details.words[NARM_DETAIL_FRAME + i] = frame[i];
      // Core words come first on Armv8-M. Skip the eager FP extension and
      // optional alignment word before copying the interrupted caller's stack.
      uintptr_t tail = (uintptr_t)frame + ((exc_return & 0x10u) ? 32u : 104u) +
                       ((frame[7] & 0x200u) ? 4u : 0u);
      uint32_t count = 0u;
      const bool external_frame = (uintptr_t)frame >= 0x11000000u && (uintptr_t)frame < psram_end;
      const uintptr_t tail_begin = external_frame ? 0x11000000u : 0x20000000u;
      const uintptr_t tail_end = external_frame ? psram_end : 0x20082000u;
      for (; count < 16u && tail >= tail_begin && tail <= tail_end - 4u;
             ++count, tail += 4u)
        narm_fault_retained_details.words[NARM_DETAIL_STACK + count] =
            *(const volatile uint32_t *)tail;
      narm_fault_retained_details.words[NARM_DETAIL_STACK_COUNT] = count;
    }
    narm_fault_retained_details.checksum =
        narm_fault_details_checksum(narm_fault_retained_details);
    __dsb();
    narm_fault_retained_details.magic = NARM_DETAIL_MAGIC;
    // Scratch 4-7 are reserved for SDK/ROM reboot routing. Select normal boot.
    watchdog_hw->scratch[4] = 0u;
    __dsb();
    psm_hw->wdsel = PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS);
    __dsb();
    watchdog_hw->ctrl = WATCHDOG_CTRL_TRIGGER_BITS;
    __dsb();
  }
  while (true) __asm volatile ("nop");
}
}

void native_fault_begin()
{
  if (initialized) return;
  initialized = true;
  if (watchdog_caused_reboot())
  {
    NativeFaultRecord record = {watchdog_hw->scratch[0], watchdog_hw->scratch[1],
                               watchdog_hw->scratch[2], watchdog_hw->scratch[3]};
    if (narm_fault_record_valid(record))
    {
      last_record = record;
      if (narm_fault_compact_valid(powman_hw->scratch, record))
      {
        for (uint32_t i = 0u; i < NARM_COMPACT_WORDS; ++i)
          last_compact[i] = powman_hw->scratch[i];
        last_compact_valid = true;
      }
      if (narm_fault_details_valid(narm_fault_retained_details, record))
      {
        for (uint32_t i = 0u; i < NARM_DETAIL_WORDS; ++i)
          last_details.words[i] = narm_fault_retained_details.words[i];
        last_details_valid = true;
      }
    }
  }
  narm_fault_retained_details.magic = 0u;
  powman_hw->scratch[NARM_COMPACT_CHECKSUM] = 0u;
  watchdog_hw->scratch[0] = 0u;
}

void native_fault_trace_enable(bool enabled)
{
  native_fault_begin();
  if (enabled && !watchdog_armed)
  {
    previous_fault_handler = exception_set_exclusive_handler(HARDFAULT_EXCEPTION,
                                                             narm_fault_vector);
    watchdog_hw->scratch[0] = 0u;
    watchdog_hw->scratch[1] = 0u;
    store_stall_context();
    watchdog_hw->scratch[0] = NARM_STALL_TAG;
    watchdog_enable(6000u, true);
    watchdog_armed = true;
    Serial.println("NARM FAULT-R7: stall watchdog ON (6000 ms)");
  }
  else if (!enabled && watchdog_armed)
  {
    watchdog_disable();
    watchdog_armed = false;
    exception_restore_handler(HARDFAULT_EXCEPTION, previous_fault_handler);
    watchdog_hw->scratch[0] = 0u;
    Serial.println("NARM FAULT-R7: stall watchdog OFF");
  }
}

void native_fault_heartbeat()
{
  if (watchdog_armed) watchdog_update();
}

void native_fault_checkpoint(uint32_t phase)
{
  active_phase = phase;
  if (watchdog_armed) store_stall_context();
}

void native_fault_process(uint32_t pid)
{
  active_pid = pid;
  if (watchdog_armed) store_stall_context();
}

void native_fault_irq_sample(uint32_t pc)
{
  if (watchdog_armed && pc) watchdog_hw->scratch[1] = pc;
}

void native_fault_print_report()
{
  if (!narm_fault_record_valid(last_record))
  {
    Serial.println("NARM-CRASH FAULT-R7 NONE");
    return;
  }
  if (last_record.tag == NARM_STALL_TAG)
  {
    Serial.print("NARM-CRASH FAULT-R7 CACHED=1 KIND=WATCHDOG LAST_IRQ_PC=0x");
    Serial.print(last_record.pc, HEX);
    Serial.print(" PHASE=0x"); Serial.print(last_record.detail, HEX);
    Serial.print(" PID="); Serial.println(last_record.extra);
  }
  else
  {
    Serial.print("NARM-CRASH FAULT-R7 CACHED=1 KIND=FAULT CORE=");
    Serial.print(last_record.tag & 1u);
    Serial.print(" EXCEPTION="); Serial.print((last_record.tag >> 4) & 63u);
    Serial.print(" FRAME_VALID="); Serial.print((last_record.tag & 2u) != 0u ? 1u : 0u);
    Serial.print(" PC=0x"); Serial.print(last_record.pc, HEX);
    Serial.print(" CFSR=0x"); Serial.print(last_record.detail, HEX);
    Serial.print(" LR_OR_EXC_RETURN=0x"); Serial.println(last_record.extra, HEX);
    if (!last_details_valid && !last_compact_valid)
    {
      Serial.println("NARM-FAULT-DETAIL R7 UNAVAILABLE");
      return;
    }
    const uint32_t phase = last_details_valid ? last_details.words[NARM_DETAIL_PHASE] : last_compact[NARM_COMPACT_PHASE];
    const uint32_t pid = last_details_valid ? last_details.words[NARM_DETAIL_PID] : last_compact[NARM_COMPACT_PID];
    const uint32_t sp = last_details_valid ? last_details.words[NARM_DETAIL_SP] : last_compact[NARM_COMPACT_SP];
    const uint32_t exc = last_details_valid ? last_details.words[NARM_DETAIL_EXC_RETURN] : last_compact[NARM_COMPACT_EXC_RETURN];
    const uint32_t hfsr = last_details_valid ? last_details.words[NARM_DETAIL_HFSR] : last_compact[NARM_COMPACT_HFSR];
    const uint32_t xpsr = last_details_valid ? last_details.words[NARM_DETAIL_FRAME + 7u] : last_compact[NARM_COMPACT_XPSR];
    Serial.print("NARM-FAULT-DETAIL R7 SOURCE="); Serial.print(last_details_valid ? "SRAM" : "POWMAN");
    Serial.print(" PHASE=0x"); Serial.print(phase, HEX);
    Serial.print(" PID="); Serial.print(pid);
    Serial.print(" SP=0x"); Serial.print(sp, HEX);
    Serial.print(" EXC_RETURN=0x"); Serial.print(exc, HEX);
    Serial.print(" HFSR=0x"); Serial.print(hfsr, HEX);
    Serial.print(" XPSR=");
    if (last_record.tag & 2u)
    {
      Serial.print("0x"); Serial.println(xpsr, HEX);
    }
    else Serial.println("UNAVAILABLE");
    if ((last_record.tag & 2u) && last_details_valid)
    {
      Serial.print("NARM-FAULT-REGS R7");
      const char *names[7] = {" R0=0x", " R1=0x", " R2=0x", " R3=0x", " R12=0x", " LR=0x", " PC=0x"};
      for (uint32_t i = 0u; i < 7u; ++i)
      {
        Serial.print(names[i]); Serial.print(last_details.words[NARM_DETAIL_FRAME + i], HEX);
      }
      Serial.println();
    }
    else if (last_record.tag & 2u)
    {
      Serial.print("NARM-FAULT-REGS R7 SOURCE=POWMAN R3=0x"); Serial.print(last_compact[NARM_COMPACT_R3], HEX);
      Serial.print(" LR=0x"); Serial.print(last_record.extra, HEX);
      Serial.print(" PC=0x"); Serial.println(last_record.pc, HEX);
    }
    else Serial.println("NARM-FAULT-REGS R7 UNAVAILABLE");
    if (!last_details_valid)
    {
      Serial.println("NARM-FAULT-STACK R7 UNAVAILABLE");
      return;
    }
    const uint32_t count = last_details.words[NARM_DETAIL_STACK_COUNT];
    Serial.print("NARM-FAULT-STACK R7 WORDS="); Serial.print(count);
    for (uint32_t i = 0u; i < count; ++i)
    {
      Serial.print(" 0x"); Serial.print(last_details.words[NARM_DETAIL_STACK + i], HEX);
    }
    Serial.println();
  }
}

namespace
{
void __attribute__((noreturn)) __no_inline_not_in_flash_func(capture_test_trap)()
{
  #if defined(__arm__) || defined(__thumb__)
  __asm volatile ("udf #0" ::: "memory");
  #endif
  while (true) __asm volatile ("nop");
}
}

void native_fault_selftest()
{
  native_fault_trace_enable(true);
  native_fault_checkpoint(NARM_PHASE_CAPTURE_TEST);
  native_fault_process(0u);
  Serial.println("NARM CAPTURE TEST: intentional reboot; read NARM DIAG after reconnecting");
  Serial.flush();
  delay(100u);
  capture_test_trap();
}
