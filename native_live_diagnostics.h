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

inline NativeSerialCommand narm_serial_command(const char *line)
{
  if (!line) return NARM_SERIAL_NONE;
  const auto upper = [](char ch) {
    return ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : ch;
  };
  const char prefix[] = "NARM";
  for (unsigned i = 0; i < 4; ++i)
  {
    if (upper(line[i]) != prefix[i]) return NARM_SERIAL_NONE;
  }
  if (line[4] != 0 && line[4] != ' ') return NARM_SERIAL_NONE;
  char command[32] = {};
  for (unsigned i = 0; line[i]; ++i)
  {
    if (i == sizeof(command) - 1u || line[i] < 32 || line[i] > 126)
      return NARM_SERIAL_INVALID;
    command[i] = upper(line[i]);
  }
  if (!strcmp(command, "NARM DIAG")) return NARM_SERIAL_DIAG;
  if (!strcmp(command, "NARM FOCUS")) return NARM_SERIAL_FOCUS;
  if (!strcmp(command, "NARM CLOSE")) return NARM_SERIAL_CLOSE;
  if (!strcmp(command, "NARM HELP")) return NARM_SERIAL_HELP;
  if (!strcmp(command, "NARM CAPTURE TEST")) return NARM_SERIAL_CAPTURE_TEST;
  return NARM_SERIAL_INVALID;
}

#endif
