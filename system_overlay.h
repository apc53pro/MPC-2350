#ifndef SYSTEM_OVERLAY_H
#define SYSTEM_OVERLAY_H

#include <Arduino.h>
#include "native_abi.h"

void system_overlay_show_focus(uint32_t pid, const char *name);
void system_overlay_show_mode(uint32_t pid,
                              const char *name,
                              NativeRunMode selected,
                              NativeRunMode effective);
void system_overlay_poll();

#endif
