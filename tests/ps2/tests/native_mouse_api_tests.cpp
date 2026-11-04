#include "mouse_input_state.h"
#include <cassert>
#include <cstdint>
#include <iostream>

struct NativeProcess { uint32_t pid; };
static NativeProcess process = {42};
static NativeContextV1 context = {};
static MouseInputState input;
alignas(4) static uint8_t output[128];
static bool valid_native_context(NativeContextV1 *ctx) { return ctx == &context; }
static void abort_if_stop_requested(NativeContextV1 *) {}
static bool has_capability(NativeContextV1 *ctx, uint64_t cap)
{ return (ctx->granted_capabilities_low & cap) != 0; }
static bool native_pointer_valid(NativeContextV1 *ctx, const void *ptr, uint32_t bytes)
{
  const uintptr_t start = (uintptr_t)ptr, base = (uintptr_t)output;
  return valid_native_context(ctx) && !(start & 3u) && start >= base &&
         start - base <= sizeof(output) && bytes <= sizeof(output) - (start - base);
}
static void mouse_input_get_state(uint32_t pid, NativeMouseStateV1 &state) { input.state(pid, state); }
static bool mouse_input_poll_event(uint32_t pid, NativeMouseEventV1 &event) { return input.poll(pid, event); }
static void mouse_input_read_motion(uint32_t pid, NativeMouseMotionV1 &motion) { input.read_motion(pid, motion); }
static bool mouse_input_set_cursor_visible(uint32_t pid, bool value) { return input.set_cursor(pid, value); }
static bool mouse_input_set_relative_mode(uint32_t pid, bool value) { return input.set_relative(pid, value); }
static uint32_t mouse_input_dropped_events(uint32_t pid) { return input.dropped(pid); }
#include "native_mouse_api_impl.h"

int main()
{
  context.process_handle = &process;
  context.granted_capabilities_low = NARM_CAP_MOUSE;
  input.report(100, 200, 0, true, {}, 0); input.focus(42);
  auto *state = (NativeMouseStateV1 *)output;
  auto *event = (NativeMouseEventV1 *)output;
  auto *motion = (NativeMouseMotionV1 *)output;
  assert(native_mouse_api.struct_bytes == sizeof(NativeMouseApiV1));
  assert(native_mouse_api.interface_major == 1 && native_mouse_api.interface_minor == 0);
  assert(native_mouse_api.get_state(&context, state) == 0 && state->x == 100 && state->y == 200);
  input.report(103, 204, 1, true, {3, 4, 0}, 55);
  assert(native_mouse_api.poll_event(&context, nullptr) == NARM_SERVICE_ERR_ARGUMENTS);
  assert(native_mouse_api.poll_event(&context, (NativeMouseEventV1 *)(output + 1)) == NARM_SERVICE_ERR_ARGUMENTS);
  assert(native_mouse_api.poll_event(&context, (NativeMouseEventV1 *)(output + 124)) == NARM_SERVICE_ERR_ARGUMENTS);
  context.granted_capabilities_low = 0;
  assert(native_mouse_api.poll_event(&context, event) == NARM_SERVICE_ERR_CAPABILITY);
  context.granted_capabilities_low = NARM_CAP_MOUSE;
  assert(native_mouse_api.poll_event(&context, event) == 1);
  assert(event->changed_buttons == 1 && event->dx == 3 && event->dy == 4 && event->timestamp_us == 55);
  assert(native_mouse_api.poll_event(&context, event) == 0);
  assert(native_mouse_api.read_motion(&context, nullptr) == NARM_SERVICE_ERR_ARGUMENTS);
  assert(native_mouse_api.read_motion(&context, motion) == 0 && motion->dx == 3 && motion->dy == 4);
  assert(native_mouse_api.read_motion(&context, motion) == 0 && motion->dx == 0);
  assert(native_mouse_api.set_cursor_visible(&context, 2) == NARM_SERVICE_ERR_ARGUMENTS);
  assert(native_mouse_api.set_relative_mode(nullptr, 1) == NARM_SERVICE_ERR_ARGUMENTS);
  assert(native_mouse_api.set_relative_mode(&context, 1) == 0);
  assert(native_mouse_api.get_state(&context, state) == 0 && (state->flags & NARM_MOUSE_RELATIVE));
  input.focus(99);
  assert(native_mouse_api.get_state(&context, state) == 0 && state->buttons == 0 && state->x == 0);
  assert(native_mouse_api.set_relative_mode(&context, 1) == NARM_SERVICE_ERR_BUSY);
  assert(native_mouse_api.set_cursor_visible(&context, 0) == NARM_SERVICE_ERR_BUSY);
  assert(native_mouse_api.poll_event(&context, event) == 0);
  assert(native_mouse_api.read_motion(&context, motion) == 0 && motion->dx == 0);
  std::cout << "Native mouse API tests passed: production wrappers, output bounds, capabilities, queue preservation, focus.\n";
}
