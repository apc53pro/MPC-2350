#ifndef NATIVE_FAULT_DETAILS_H
#define NATIVE_FAULT_DETAILS_H

#include "native_fault_record.h"

enum NativeFaultDetailWord : uint32_t
{
  NARM_DETAIL_TAG, NARM_DETAIL_PC, NARM_DETAIL_CFSR, NARM_DETAIL_EXTRA,
  NARM_DETAIL_SP, NARM_DETAIL_EXC_RETURN, NARM_DETAIL_HFSR,
  NARM_DETAIL_PHASE, NARM_DETAIL_PID, NARM_DETAIL_FRAME,
  NARM_DETAIL_STACK_COUNT = NARM_DETAIL_FRAME + 8u,
  NARM_DETAIL_STACK,
  NARM_DETAIL_WORDS = NARM_DETAIL_STACK + 16u
};

struct NativeFaultDetails
{
  uint32_t magic;
  uint32_t checksum;
  uint32_t words[NARM_DETAIL_WORDS];
};

static constexpr uint32_t NARM_DETAIL_MAGIC = 0x4e463700u;

enum NativeFaultCompactWord : uint32_t
{
  NARM_COMPACT_CHECKSUM, NARM_COMPACT_SP, NARM_COMPACT_EXC_RETURN,
  NARM_COMPACT_HFSR, NARM_COMPACT_PHASE, NARM_COMPACT_PID,
  NARM_COMPACT_XPSR, NARM_COMPACT_R3, NARM_COMPACT_WORDS
};

inline __attribute__((always_inline)) uint32_t
narm_fault_compact_checksum(const volatile uint32_t *words, const NativeFaultRecord &record)
{
  uint32_t checksum = NARM_DETAIL_MAGIC;
  checksum = (checksum << 5 | checksum >> 27) ^ record.tag;
  checksum = (checksum << 5 | checksum >> 27) ^ record.pc;
  checksum = (checksum << 5 | checksum >> 27) ^ record.detail;
  checksum = (checksum << 5 | checksum >> 27) ^ record.extra;
  for (uint32_t i = 1u; i < NARM_COMPACT_WORDS; ++i)
    checksum = (checksum << 5 | checksum >> 27) ^ words[i];
  return checksum | 1u;
}

inline bool narm_fault_compact_valid(const volatile uint32_t *words, const NativeFaultRecord &record)
{
  return (record.tag & NARM_FAULT_TAG_MASK) == NARM_FAULT_TAG &&
         words[NARM_COMPACT_CHECKSUM] == narm_fault_compact_checksum(words, record);
}

inline __attribute__((always_inline)) bool
narm_fault_frame_readable(uintptr_t sp, uint32_t cfsr, uintptr_t psram_end, bool qmi_available)
{
  if (narm_fault_frame_safe(sp, cfsr)) return true;
  // PSRAM is attempted only for non-memory faults, with XIP available and
  // the whole core frame inside detected physical RAM. The basic report and
  // hardware scratch context are committed before any such read is attempted.
  return narm_fault_frame_safe(0x20000000u, cfsr) && (cfsr & 0xffffu) == 0u &&
         qmi_available && (sp & 3u) == 0u && psram_end >= 0x11000020u &&
         psram_end <= 0x12000000u && sp >= 0x11000000u && sp <= psram_end - 32u;
}

inline __attribute__((always_inline)) uint32_t
narm_fault_details_checksum(const volatile NativeFaultDetails &details)
{
  uint32_t checksum = 0x2350f006u;
  for (uint32_t i = 0u; i < NARM_DETAIL_WORDS; ++i)
    checksum = (checksum << 5 | checksum >> 27) ^ details.words[i];
  return checksum;
}

inline bool narm_fault_details_valid(const volatile NativeFaultDetails &details,
                                      const NativeFaultRecord &record)
{
  return (record.tag & NARM_FAULT_TAG_MASK) == NARM_FAULT_TAG &&
         details.magic == NARM_DETAIL_MAGIC &&
         details.checksum == narm_fault_details_checksum(details) &&
         details.words[NARM_DETAIL_TAG] == record.tag &&
         details.words[NARM_DETAIL_PC] == record.pc &&
         details.words[NARM_DETAIL_CFSR] == record.detail &&
         details.words[NARM_DETAIL_EXTRA] == record.extra &&
         details.words[NARM_DETAIL_STACK_COUNT] <= 16u;
}

#endif
