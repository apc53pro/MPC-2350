#ifndef NATIVE_LIVE_DIAGNOSTICS_H
#define NATIVE_LIVE_DIAGNOSTICS_H

#include "native_scheduler_arch.h"
#include <string.h>

inline bool narm_saved_pc(const uint8_t *stack, uint32_t bytes,
                          const NativeArchContext &arch, uint32_t &pc)
{
  pc = 0u;
  const uintptr_t base = (uintptr_t)stack;
  const uintptr_t saved = (uintptr_t)arch.saved_psp;
  const bool fp = (arch.exception_return & 0x10u) == 0u;
  const uint32_t software_bytes = fp ? 96u : 32u;
  const uint32_t hardware_bytes = fp ? 104u : 32u;
  const uint32_t frame_bytes = software_bytes + hardware_bytes;
  if (!stack || !arch.saved_psp || base > UINTPTR_MAX - bytes ||
      bytes < frame_bytes || (saved & 3u) != 0u ||
      saved < base || saved > base + bytes - frame_bytes)
    return false;
  const uint32_t *hardware = (const uint32_t *)(saved + software_bytes);
  pc = hardware[6];
  return true;
}

enum NativeSerialCommand
{
  NARM_SERIAL_NONE, NARM_SERIAL_DIAG, NARM_SERIAL_FOCUS,
  NARM_SERIAL_CLOSE, NARM_SERIAL_HELP, NARM_SERIAL_CAPTURE_TEST, NARM_SERIAL_INVALID
};

class NativeSerialCommandParser
{
public:
  NativeSerialCommand feed(uint8_t byte)
  {
    if (byte == '\r' || byte == '\n')
    {
      if (discarding_) { reset(); return NARM_SERIAL_INVALID; }
      if (!used_) return NARM_SERIAL_NONE;
      line_[used_] = 0;
      NativeSerialCommand command = NARM_SERIAL_INVALID;
      if (!strcmp(line_, "NARM DIAG")) command = NARM_SERIAL_DIAG;
      else if (!strcmp(line_, "NARM FOCUS")) command = NARM_SERIAL_FOCUS;
      else if (!strcmp(line_, "NARM CLOSE")) command = NARM_SERIAL_CLOSE;
      else if (!strcmp(line_, "NARM HELP")) command = NARM_SERIAL_HELP;
      else if (!strcmp(line_, "NARM CAPTURE TEST")) command = NARM_SERIAL_CAPTURE_TEST;
      reset();
      return command;
    }
    if (discarding_) return NARM_SERIAL_NONE;
    if (byte < 32u || byte > 126u || used_ == sizeof(line_) - 1u)
      discarding_ = true;
    else
      line_[used_++] = byte >= 'a' && byte <= 'z' ? byte - 'a' + 'A' : byte;
    return NARM_SERIAL_NONE;
  }

private:
  char line_[32] = {};
  uint8_t used_ = 0u;
  bool discarding_ = false;
  void reset() { used_ = 0u; discarding_ = false; }
};

#endif
