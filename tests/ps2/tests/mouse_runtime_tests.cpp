#include "mouse_runtime.h"
#include "ps2_pio_transport.h"
#include <assert.h>
#include <stdio.h>
#include <vector>

static uint32_t fake_now;
static uint32_t irq_depth;
static bool wire_ok = true;
static std::vector<uint8_t> sent;

uint32_t time_us_32() { return fake_now; }
uint32_t save_and_disable_interrupts() { return irq_depth++; }
void restore_interrupts(uint32_t state) { assert(irq_depth == state + 1); irq_depth = state; }
bool ps2_pio_send_byte(Ps2Port port, uint8_t byte)
{
  assert(port == Ps2Port::Mouse);
  assert(irq_depth == 0); // Command waits must never mask Core 1's interrupts.
  sent.push_back(byte);
  return wire_ok;
}
static MouseState snapshot()
{
  MouseState state;
  mouse_runtime_get_state(state);
  return state;
}
static void poll_at(uint32_t timestamp)
{
  fake_now = timestamp;
  mouse_runtime_poll();
  assert(irq_depth == 0);
}
static void receive(uint8_t byte, bool after_idle = false)
{
  fake_now += 700;
  mouse_runtime_receive(byte, true, after_idle);
}
static void reset_runtime(uint32_t now)
{
  fake_now = now;
  sent.clear();
  wire_ok = true;
  mouse_runtime_init();
  assert(!snapshot().connected);
  assert(!snapshot().connected && snapshot().x == 320 && snapshot().y == 240);
}
static void ack_expect(uint8_t next)
{
  const auto count = sent.size();
  receive(0xfa); mouse_runtime_poll();
  assert(sent.size() == count + 1 && sent.back() == next);
}
static void initialize_to_read_id()
{
  reset_runtime(0);
  poll_at(499999);
  assert(sent.empty());
  poll_at(500000);
  assert(sent.size() == 1 && sent.back() == 0xff);
  receive(0xfa); receive(0xaa); receive(0x00);
  mouse_runtime_poll();
  assert(sent.size() == 2 && sent.back() == 0xf6);
  for(uint8_t command : {0xf3, 200, 0xf3, 100, 0xf3, 80, 0xf2}) ack_expect(command);
}
static void initialize_to_enable(uint8_t id = 0)
{
  initialize_to_read_id();
  const auto count = sent.size();
  receive(0xfa); receive(id); mouse_runtime_poll();
  assert(sent.size() == count + 1 && sent.back() == 0xf3);
  ack_expect(100); ack_expect(0xea); ack_expect(0xf4);
}
static void finish_basic_fallback()
{
  assert(!snapshot().connected);
  wire_ok = true;
  poll_at(fake_now + 2000000u);
  assert(sent.back() == 0xff);
  receive(0xfa); receive(0xaa); receive(0); mouse_runtime_poll();
  assert(sent.back() == 0xf6);
  ack_expect(0xea); ack_expect(0xf4); // No more optional commands this attempt.
  receive(0xfa); mouse_runtime_poll();
  assert(snapshot().connected);
  const int32_t before_x = snapshot().x;
  receive(0x08); receive(1); receive(0);
  assert(snapshot().x == (before_x < 639 ? before_x + 1 : 639));
}

int main()
{
  initialize_to_enable();
  // A complete movement report can follow F4's ACK before loop1 runs again.
  receive(0xfa); receive(0x09); receive(200); receive(5);
  assert(!snapshot().connected);
  mouse_runtime_poll();
  MouseState state = snapshot();
  assert(state.connected && state.x == 520 && state.y == 235);
  assert(state.buttons == 1 && state.reports == 1);
  assert(snapshot().connected && snapshot().x == 520 && snapshot().y == 235);
  // During streaming the receive handler updates state without poll/render.
  receive(0x38); receive(56); receive(251);
  state = snapshot();
  assert(state.x == 320 && state.y == 240 && state.reports == 2);
  assert(state.buttons == 0 && state.x == 320 && state.y == 240);
  receive(0x0f); receive(0); receive(0);
  assert(snapshot().buttons == 7);
  mouse_runtime_receive(0, false, false);
  assert(snapshot().buttons == 0 && snapshot().connected);
  fake_now += 6000; // Fresh report after the damaged packet's remainder.
  receive(0x08, true); receive(1); receive(1);
  assert(snapshot().x == 321 && snapshot().y == 239);

  // Exercise the public NARM-facing API against the actual IRQ receiver.
  mouse_input_set_focus(7);
  receive(0x09); receive(200); receive(0);
  receive(0x08); receive(200); receive(0);
  assert(snapshot().x == 639); // Raw movement must still retain all 400 counts.
  NativeMouseStateV1 app_state;
  NativeMouseMotionV1 motion;
  NativeMouseEventV1 event;
  mouse_input_get_state(7, app_state);
  assert(app_state.x == 639 && app_state.buttons == 0);
  mouse_input_read_motion(8, motion);
  assert(motion.dx == 0);
  mouse_input_read_motion(7, motion);
  assert(motion.dx == 400 && motion.dy == 0);
  assert(mouse_input_poll_event(7, event) && (event.type & NARM_MOUSE_EVENT_PRESS));
  assert(mouse_input_poll_event(7, event) && (event.type & NARM_MOUSE_EVENT_RELEASE));
  assert(!mouse_input_poll_event(7, event));
  assert(mouse_input_set_relative_mode(7, true));
  receive(0x38); receive(156); receive(251); // -100, +5 screen delta.
  assert(snapshot().x == 639 && snapshot().y == 239);
  mouse_input_read_motion(7, motion);
  assert(motion.dx == -100 && motion.dy == 5);
  assert(!mouse_input_set_cursor_visible(8, false));
  mouse_input_set_focus(8);
  assert(snapshot().connected && snapshot().x == 639 && snapshot().y == 239);
  mouse_input_get_state(7, app_state);
  assert(app_state.x == 0 && app_state.buttons == 0 && app_state.flags == NARM_MOUSE_CONNECTED);
  assert(mouse_input_set_cursor_visible(8, false));
  mouse_input_release(8);

  // The reset ID is zero, but negotiation selects four-byte wheel packets.
  initialize_to_enable(3);
  mouse_input_set_focus(7);
  receive(0xfa); receive(0x09); receive(1); receive(2); receive(0xff);
  mouse_runtime_poll(); // Includes F4 ACK and the first report in one batch.
  state = snapshot();
  assert(state.connected && state.x == 321 && state.y == 238 && state.wheel == -1);
  assert(state.buttons == 1 && state.reports == 1);
  mouse_input_read_motion(7, motion);
  assert(motion.dx == 1 && motion.dy == -2 && motion.wheel == -1);
  bool wheel_event = false;
  while(mouse_input_poll_event(7, event))
    if(event.type & NARM_MOUSE_EVENT_WHEEL)
    { assert(event.wheel == -1 && event.x == 321 && event.y == 238); wheel_event = true; }
  assert(wheel_event);
  receive(0x39); receive(0); receive(0); receive(1);
  state = snapshot();
  assert(state.x == 321 && state.y == 238 && state.wheel == 0); // Negative-zero axes stay still.
  mouse_input_read_motion(7, motion);
  assert(motion.dx == 0 && motion.dy == 0 && motion.wheel == 1);
  assert(mouse_input_poll_event(7, event) && event.type == NARM_MOUSE_EVENT_WHEEL && event.wheel == 1);
  MouseTraceSnapshot trace = {};
  assert(mouse_runtime_get_trace(trace) && trace.reports == 2 && trace.invalid == 0);
  const MousePacketTrace &last = trace.packets[trace.packet_count - 1];
  assert(last.raw == 0x01000039u && last.device_id == 3 && last.dx == 0 && last.dy == 0);
  assert(last.wheel == 1 && last.x == 321 && last.y == 238);
  const MouseByteTrace &wire = trace.bytes[trace.byte_count - 1];
  assert((wire.value & 511u) == 257u && (wire.status & 255u) == 3u);
  assert(((wire.status >> 8) & 255u) == 3u && (wire.status & (1u << 17)));
  // A lost frame used to let 0x08/0xff payload bytes become false headers.
  receive(0x08); receive(120);
  mouse_runtime_receive(0, false, false);
  receive(0x08); receive(0xff); // Remainder/noise: discard until a report gap.
  assert(snapshot().reports == 2 && snapshot().x == 321 && snapshot().y == 238);
  fake_now += 6000;
  receive(0x08, true); receive(3); receive(4); receive(0xff);
  assert(snapshot().x == 324 && snapshot().y == 234 && snapshot().wheel == -1);
  assert(mouse_runtime_get_trace(trace) && trace.reports == 3 && trace.invalid == 1);
  bool saw_error = false;
  for(uint32_t i = 0; i < trace.byte_count; ++i) if(!(trace.bytes[i].value & 256u)) saw_error = true;
  assert(saw_error);

  // A new wire report can follow an incomplete one in the same F4 batch.
  initialize_to_enable();
  receive(0xfa); receive(0x38, true); receive(0xff);
  receive(0x08, true); receive(1); receive(2);
  mouse_runtime_poll();
  state = snapshot();
  assert(state.connected && state.reports == 1 && state.x == 321 && state.y == 238);

  initialize_to_enable(4);
  receive(0xfa); mouse_runtime_poll();
  receive(0x08); receive(0); receive(0); receive(0x3f);
  assert(snapshot().wheel == -1 && snapshot().buttons == (8u | 16u));

  // Missing or unknown ID must reset before falling back to basic packets.
  initialize_to_read_id();
  receive(0xfa); mouse_runtime_poll();
  poll_at(fake_now + 250000u);
  finish_basic_fallback();
  initialize_to_read_id();
  receive(0xfa); receive(0x7f); mouse_runtime_poll();
  finish_basic_fallback();

  // RESEND repeats the parameter itself, with bounded retries and fallback.
  initialize_to_read_id();
  receive(0xfe); mouse_runtime_poll(); assert(sent.back() == 0xf2);
  receive(0xfe); mouse_runtime_poll(); assert(sent.back() == 0xf2);
  receive(0xfe); mouse_runtime_poll();
  finish_basic_fallback();
  // Reconnect rearms wheel detection for a newly attached wheel mouse.
  mouse_runtime_reconnect(); mouse_runtime_poll();
  receive(0xfa); receive(0xaa); receive(0); mouse_runtime_poll();
  ack_expect(0xf3); ack_expect(200);
  receive(0xfe); mouse_runtime_poll(); assert(sent.back() == 200);
  ack_expect(0xf3); // The rate index advances only after ACK.
  receive(0xfc); mouse_runtime_poll();
  finish_basic_fallback();

  // A wire failure during optional setup also takes the known-format fallback.
  initialize_to_read_id();
  receive(0xfa); receive(3); wire_ok = false; mouse_runtime_poll();
  assert(sent.back() == 0xf3);
  finish_basic_fallback();

  // Optional setup on the last reset attempt still gets one basic fallback.
  reset_runtime(0);
  wire_ok = false; poll_at(500000); poll_at(2500000);
  wire_ok = true; poll_at(4500000);
  assert(sent.size() == 3 && sent.back() == 0xff);
  receive(0xfa); receive(0xaa); receive(0); mouse_runtime_poll();
  ack_expect(0xf3);
  receive(0xfc); mouse_runtime_poll();
  finish_basic_fallback();

  // The extended ID timeout also remains bounded across micros() wrap.
  initialize_to_read_id();
  fake_now = 0xffffff00u;
  receive(0xfa); mouse_runtime_poll();
  const auto before_timeout = sent.size();
  poll_at(fake_now + 249999u); assert(sent.size() == before_timeout);
  poll_at(fake_now + 1u); finish_basic_fallback();

  // Absent mouse: one bounded TX attempt, followed by a two-second retry gap.
  reset_runtime(0);
  wire_ok = false;
  poll_at(500000);
  assert(sent.size() == 1);
  poll_at(2499999);
  assert(sent.size() == 1);
  poll_at(2500000);
  assert(sent.size() == 2 && !snapshot().connected);
  poll_at(4500000);
  assert(sent.size() == 3);
  poll_at(6500000);
  poll_at(100000000);
  assert(sent.size() == 3); // Quiet after the bounded absent-device budget.
  receive(0x08); receive(1); receive(2);
  mouse_runtime_poll();
  assert(sent.size() == 3); // Arbitrary input cannot continually rearm probes.
  receive(0xaa); receive(0x00);
  wire_ok = true;
  mouse_runtime_poll();
  assert(sent.size() == 4 && sent.back() == 0xff);

  // Manual reconnect is available for devices without a power-on announcement.
  mouse_runtime_reconnect();
  mouse_runtime_poll();
  assert(sent.size() == 5 && sent.back() == 0xff);

  // Mouse RESEND is bounded to two immediate retries per command.
  reset_runtime(10000000);
  poll_at(10500000);
  receive(0xfe); mouse_runtime_poll();
  receive(0xfe); mouse_runtime_poll();
  assert(sent.size() == 3);
  receive(0xfe); mouse_runtime_poll();
  assert(sent.size() == 3 && !snapshot().connected);

  // Missing ACK and invalid response frames also recover without spinning.
  reset_runtime(20000000);
  poll_at(20500000);
  poll_at(20750000);
  assert(sent.size() == 1);
  poll_at(22750000);
  assert(sent.size() == 2);
  mouse_runtime_receive(0, false, false);
  mouse_runtime_poll();
  assert(!snapshot().connected);

  // Deadlines continue to work when micros() wraps.
  reset_runtime(0xfff90000u);
  poll_at((uint32_t)(0xfff90000u + 499999u));
  assert(sent.empty());
  poll_at((uint32_t)(0xfff90000u + 500000u));
  assert(sent.size() == 1 && sent.back() == 0xff);
  puts("mouse_runtime_tests: all tests passed");
  return 0;
}
