#ifndef NATIVE_STACK_DIAGNOSTICS_H
#define NATIVE_STACK_DIAGNOSTICS_H

#include <stddef.h>
#include <stdint.h>

// PendSV/SVC save r4-r11 (32 bytes), plus s16-s31 (64 bytes) when
// FP is active. They store through r1, so PSPLIM cannot protect those
// stores. Keep this much memory below the architectural stack limit.
static const uint32_t NARM_STACK_CONTEXT_RESERVE_BYTES = 96u;
static const uint8_t NARM_STACK_FILL_BYTE = 0xcdu;

inline uint32_t narm_stack_headroom(uintptr_t base, uint32_t bytes,
                                    uintptr_t lowest_hardware_psp)
{
  if (bytes < NARM_STACK_CONTEXT_RESERVE_BYTES ||
      base > UINTPTR_MAX - bytes)
    return 0u;
  const uintptr_t limit = base + NARM_STACK_CONTEXT_RESERVE_BYTES;
  if (lowest_hardware_psp < limit || lowest_hardware_psp > base + bytes)
    return 0u;
  return (uint32_t)(lowest_hardware_psp - limit);
}

// A fill-pattern estimate supplements sampled PSP: brief calls may finish
// between timer ticks. Unwritten local arrays can make this estimate too
// optimistic, so report it separately from the sampled PSP headroom.
inline uint32_t narm_stack_untouched(const uint8_t *base, uint32_t bytes)
{
  if (!base || bytes < NARM_STACK_CONTEXT_RESERVE_BYTES) return 0u;
  uint32_t prefix = 0u;
  while (prefix < bytes && base[prefix] == NARM_STACK_FILL_BYTE) ++prefix;
  return prefix > NARM_STACK_CONTEXT_RESERVE_BYTES
           ? prefix - NARM_STACK_CONTEXT_RESERVE_BYTES : 0u;
}

#endif
