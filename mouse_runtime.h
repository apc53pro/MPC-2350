#ifndef MOUSE_RUNTIME_H
#define MOUSE_RUNTIME_H

#include <stdint.h>
#include "native_abi.h"
#include "mouse_trace.h"

enum MouseButton : uint32_t
{
  MOUSE_BUTTON_LEFT = 1u << 0,
  MOUSE_BUTTON_RIGHT = 1u << 1,
  MOUSE_BUTTON_MIDDLE = 1u << 2,
  MOUSE_BUTTON_BACK = 1u << 3,
  MOUSE_BUTTON_FORWARD = 1u << 4
};

struct MouseState
{
  int32_t x;
  int32_t y;
  uint32_t buttons;
  int32_t wheel;
  uint32_t reports;
  bool connected;
};

// Initialize and service on Core 1. Polling only handles initialization and
// retry commands; streaming reports update coordinates in the receive IRQ.
void mouse_runtime_init();
void mouse_runtime_poll();
void mouse_runtime_receive(uint8_t byte, bool valid, bool after_idle);

// Optional Core 1 foreground request after rewiring or reconnecting a device
// that does not announce itself with the normal power-on self-test bytes.
void mouse_runtime_reconnect();

// Coherent snapshot for the cursor renderer or other Core 0 consumers.
// Coordinates are physical 640x480 screen pixels; the cursor hotspot is (0,0).
void mouse_runtime_get_state(MouseState &state);
// Bounded, read-only snapshot for USB diagnostics; false means the trace was
// being written during all three attempts. Does not acquire the input lock.
bool mouse_runtime_get_trace(MouseTraceSnapshot &trace);

// Focus-owned NARM input. These operations take a bounded cross-core lock;
// no process pointers or callbacks are retained by the receive handler.
void mouse_input_set_focus(uint32_t pid);
void mouse_input_release(uint32_t pid);
void mouse_input_get_state(uint32_t pid, NativeMouseStateV1 &state);
bool mouse_input_poll_event(uint32_t pid, NativeMouseEventV1 &event);
void mouse_input_read_motion(uint32_t pid, NativeMouseMotionV1 &motion);
bool mouse_input_set_cursor_visible(uint32_t pid, bool visible);
bool mouse_input_set_relative_mode(uint32_t pid, bool enabled);
uint32_t mouse_input_dropped_events(uint32_t pid);

#endif
