#ifndef NATIVE_SCHEDULER_ARCH_H
#define NATIVE_SCHEDULER_ARCH_H

#include <stddef.h>
#include <stdint.h>

struct NativeArchContext
{
  uint32_t *saved_psp;
  uint32_t exception_return;
  uint32_t control;
  uint32_t psplim;
};

enum NativeArchSwitchReason : uint32_t
{
  NARM_ARCH_SWITCH_NONE = 0,
  NARM_ARCH_SWITCH_TIMESLICE = 1,
  NARM_ARCH_SWITCH_YIELD = 2,
  NARM_ARCH_SWITCH_EXIT = 3
};

#if defined(__cplusplus) && UINTPTR_MAX == 0xffffffffu
static_assert(sizeof(NativeArchContext) == 16, "NativeArchContext layout drift");
static_assert(offsetof(NativeArchContext, saved_psp) == 0, "saved_psp offset drift");
static_assert(offsetof(NativeArchContext, exception_return) == 4, "exception_return offset drift");
static_assert(offsetof(NativeArchContext, control) == 8, "control offset drift");
static_assert(offsetof(NativeArchContext, psplim) == 12, "psplim offset drift");
#endif

extern "C" uint32_t narm_arch_resume(NativeArchContext *context);
extern "C" void narm_arch_yield(uint32_t reason);
extern "C" void narm_arch_abort(int32_t status) __attribute__((noreturn));
extern "C" void narm_arch_task_return();
extern "C" void narm_arch_svcall_handler();
extern "C" void narm_arch_pendsv_handler();
extern "C" void narm_arch_probe_entry();
extern "C" const uint8_t narm_arch_probe_end[];

#endif
