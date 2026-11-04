#include "mouse_input_state.h"
#include "mouse_protocol.h"
#include <cassert>
#include <iostream>

static void report(MouseInputState &input, unsigned buttons, int dx = 0, int dy = 0)
{ input.report(320, 240, buttons, true, {dx, dy, 0}, 1234); }

static void focus_and_clicks()
{
  MouseInputState input;
  report(input, 1); // Held before the program receives focus.
  input.focus(10);
  NativeMouseStateV1 state;
  input.state(10, state);
  assert(state.x == 320 && state.y == 240 && state.buttons == 0);
  assert(state.flags == (NARM_MOUSE_CONNECTED | NARM_MOUSE_FOCUSED | NARM_MOUSE_CURSOR_VISIBLE));
  NativeMouseEventV1 event;
  report(input, 0);
  assert(!input.poll(10, event)); // No orphan release for an inherited button.
  report(input, 1);
  report(input, 0);
  assert(!input.poll(11, event));
  assert(input.poll(10, event) && event.type == NARM_MOUSE_EVENT_PRESS);
  assert(event.buttons == 1 && event.changed_buttons == 1 && event.timestamp_us == 1234);
  assert(input.poll(10, event) && event.type == NARM_MOUSE_EVENT_RELEASE && event.buttons == 0);
  assert(!input.poll(10, event));
  report(input, 0, 4, -2);
  report(input, 0, 5, -3);
  NativeMouseMotionV1 motion;
  input.read_motion(11, motion);
  assert(motion.dx == 0 && motion.dy == 0);
  input.read_motion(10, motion);
  assert(motion.dx == 9 && motion.dy == -5);
  input.read_motion(10, motion);
  assert(motion.dx == 0 && motion.dy == 0);
  assert(input.poll(10, event) && event.type == NARM_MOUSE_EVENT_MOVE);
  assert(event.dx == 9 && event.dy == -5 && !input.poll(10, event));
  assert(!input.set_cursor(11, false) && !input.set_relative(11, true));
  assert(input.set_cursor(10, false) && input.set_relative(10, true));
  input.state(10, state);
  assert((state.flags & NARM_MOUSE_RELATIVE) && !(state.flags & NARM_MOUSE_CURSOR_VISIBLE));
  input.focus(11);
  input.state(11, state);
  assert(!(state.flags & NARM_MOUSE_RELATIVE) && (state.flags & NARM_MOUSE_CURSOR_VISIBLE));
  input.state(10, state);
  assert(state.x == 0 && state.y == 0 && state.buttons == 0 && state.flags == NARM_MOUSE_CONNECTED);
  assert(!input.poll(11, event));
  input.release(10); // Reaping an old process must not disturb the owner.
  assert(input.owner() == 11);
  assert(input.set_relative(11, true));
  input.release(11);
  int32_t x, y; bool visible;
  input.cursor(x, y, visible);
  assert(visible && !input.relative_mode() && input.owner() == 0);
}

static void overflow_and_saturation()
{
  MouseInputState input;
  report(input, 0); input.focus(10);
  // Thirty-one discrete transitions plus one movement event fill the queue.
  report(input, 0, 1);
  for(unsigned i = 0; i < 31; ++i) report(input, (i & 1) ? 0 : 1);
  report(input, 0); // Release evicts the movement, retaining all 32 transitions.
  NativeMouseEventV1 event;
  unsigned count = 0;
  while(input.poll(10, event)) { assert(event.type != NARM_MOUSE_EVENT_MOVE); ++count; }
  assert(count == 32 && event.buttons == 0 && input.dropped(10) == 1);
  input.focus(11);
  for(unsigned i = 0; i < 32; ++i) report(input, (i & 1) ? 0 : 1);
  report(input, 0, 10); // Movement cannot evict a click.
  assert(input.dropped(11) == 1);
  report(input, 1); // Explicit resync instead of silently dropping a release/press.
  assert(input.poll(11, event) && event.type == NARM_MOUSE_EVENT_RESYNC && event.buttons == 1);
  assert(input.poll(11, event) && event.type == NARM_MOUSE_EVENT_PRESS && event.buttons == 1);
  assert(!input.poll(11, event) && input.dropped(11) == 33);
  NativeMouseMotionV1 motion;
  input.read_motion(11, motion); // Clear motion independently.
  input.report(0, 0, 1, true, {INT32_MAX, INT32_MIN, INT32_MAX}, 1);
  input.report(0, 0, 1, true, {100, -100, 100}, 2);
  input.read_motion(11, motion);
  assert(motion.dx == INT32_MAX && motion.dy == INT32_MIN && motion.wheel == INT32_MAX);
  input.report(0, 0, 0, false, {}, 3);
  while(input.poll(11, event)) {}
  assert(event.type == (NARM_MOUSE_EVENT_RELEASE | NARM_MOUSE_EVENT_DISCONNECT));
}

static void raw_deltas_and_relative_position()
{
  ps2mouse::Decoder decoder;
  MouseState state = {639, 0, 0, 0, 0, true};
  NativeMouseMotionV1 motion;
  assert(!decoder.feed(0x08, true, state, &motion));
  assert(!decoder.feed(25, false, state, &motion));
  assert(decoder.feed(20, false, state, &motion));
  assert(state.x == 639 && state.y == 0 && motion.dx == 25 && motion.dy == -20);
  assert(!decoder.feed(0x38, true, state, &motion, false));
  assert(!decoder.feed(246, false, state, &motion, false));
  assert(decoder.feed(251, false, state, &motion, false));
  assert(state.x == 639 && state.y == 0 && motion.dx == -10 && motion.dy == 5);
  decoder.set_device_id(4);
  for(unsigned i = 0; i < 3; ++i) assert(!decoder.feed(i ? 0 : 0x08, i == 0, state, &motion));
  assert(decoder.feed(0x3f, false, state, &motion));
  assert(motion.wheel == -1 && state.buttons == (8 | 16));
  decoder.set_device_id(0);
  assert(!decoder.feed(0xc8, true, state, &motion));
  assert(!decoder.feed(0, false, state, &motion));
  assert(decoder.feed(0, false, state, &motion));
  assert(motion.dx == 255 && motion.dy == -255);
}

int main()
{
  focus_and_clicks(); overflow_and_saturation(); raw_deltas_and_relative_position();
  std::cout << "Mouse input tests passed: focus, click queue, coalescing, overflow/resync, independent raw motion, relative mode, wheel.\n";
}
