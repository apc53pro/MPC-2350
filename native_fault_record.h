#ifndef NATIVE_FAULT_RECORD_H
#define NATIVE_FAULT_RECORD_H

#include <stdint.h>
#include <stddef.h>

struct NativeFaultRecord
{
  uint32_t tag;
  uint32_t pc;
  uint32_t detail;
  uint32_t extra;
};

static constexpr uint32_t NARM_FAULT_TAG_MASK = 0xfffff000u;
static constexpr uint32_t NARM_FAULT_TAG = 0x4e414000u;
static constexpr uint32_t NARM_STALL_TAG = 0x4e415000u;

inline bool narm_fault_record_valid(const NativeFaultRecord &record)
{
  return (record.tag & NARM_FAULT_TAG_MASK) == NARM_FAULT_TAG ||
         record.tag == NARM_STALL_TAG;
}

// A failed stack operation may not have produced an exception frame at all.
// A PSRAM bus fault also makes reading a PSRAM-resident frame unsafe. The
// base check therefore accepts internal SRAM only. The extended capture
// applies a separate guarded policy for non-memory faults with PSRAM stacks.
inline __attribute__((always_inline)) bool narm_fault_frame_safe(uintptr_t sp, uint32_t cfsr)
{
  static constexpr uint32_t STACK_FAILURES = 0x00103838u;
  return (cfsr & STACK_FAILURES) == 0u && (sp & 3u) == 0u &&
         sp >= 0x20000000u && sp <= 0x20082000u - 32u;
}

#endif
