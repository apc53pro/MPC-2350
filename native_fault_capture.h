#ifndef NATIVE_FAULT_CAPTURE_H
#define NATIVE_FAULT_CAPTURE_H

#include <stdint.h>

enum NativeFaultPhase : uint32_t
{
  NARM_PHASE_USB = 1u,
  NARM_PHASE_AUDIO = 2u,
  NARM_PHASE_SCHEDULER = 3u,
  NARM_PHASE_TERMINAL = 4u,
  NARM_PHASE_OVERLAY = 5u,
  NARM_PHASE_NATIVE = 6u,
  NARM_PHASE_TRACE = 7u,
  NARM_PHASE_SERIAL = 8u,
  NARM_PHASE_LOADER = 9u,
  NARM_PHASE_CAPTURE_TEST = 10u,
  NARM_PHASE_INVOKE = 0x10000000u,
  NARM_PHASE_VIDEO = 0x20000000u,
  NARM_PHASE_FILE = 0x30000000u,
  NARM_PHASE_AUDIO_API = 0x40000000u,
  NARM_PHASE_KEYBOARD = 0x50000000u
};

void native_fault_begin();
void native_fault_trace_enable(bool enabled);
void native_fault_heartbeat();
void native_fault_checkpoint(uint32_t phase);
void native_fault_process(uint32_t pid);
void native_fault_irq_sample(uint32_t pc);
void native_fault_print_report();
void native_fault_selftest() __attribute__((noreturn));

#endif
