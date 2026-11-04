#ifndef PS2_KEYBOARD_DRIVER_H
#define PS2_KEYBOARD_DRIVER_H

#include <stdint.h>

enum Ps2KeyboardPollStage : uint32_t
{
  PS2_POLL_IDLE = 0, PS2_POLL_TRANSPORT = 1, PS2_POLL_MOUSE = 2,
  PS2_POLL_RESYNC = 3, PS2_POLL_QUEUE = 4, PS2_POLL_DECODE = 5
};

struct Ps2KeyboardDiagnostics
{
  uint32_t received_bytes;
  uint32_t recoveries;
  uint32_t last_transition;
  uint32_t shortcut_presses[3]; // Alt+F2, Alt+F3, Alt+F4
  uint32_t clock_edges; // PIO: eleven sampled bits per validated frame.
  uint32_t invalid_frames;
  uint32_t queue_overflows;
  uint32_t polls;
  uint32_t queue_pending;
  uint32_t last_edge_us;
  uint32_t poll_stage;
  uint32_t parsed_bytes;
};

// Physical PS/2 transport entry points. Decoded input is published through
// keyboard_runtime; consumers should not depend on this driver directly.
void ps2_keyboard_receive_byte(uint8_t byte, bool valid, bool after_idle = false);
void ps2_keyboard_poll();
void ps2_keyboard_get_diagnostics(Ps2KeyboardDiagnostics &diagnostics);

#endif
