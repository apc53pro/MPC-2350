#ifndef PS2_KEYBOARD_DRIVER_H
#define PS2_KEYBOARD_DRIVER_H

#include <stdint.h>

struct Ps2KeyboardDiagnostics
{
  uint32_t received_bytes;
  uint32_t recoveries;
  uint32_t last_transition;
  uint32_t shortcut_presses[3]; // Alt+F2, Alt+F3, Alt+F4
};

// Physical PS/2 transport entry points. Decoded input is published through
// keyboard_runtime; consumers should not depend on this driver directly.
void ps2_keyboard_clock_isr();
void ps2_keyboard_poll();
void ps2_keyboard_get_diagnostics(Ps2KeyboardDiagnostics &diagnostics);

#endif
