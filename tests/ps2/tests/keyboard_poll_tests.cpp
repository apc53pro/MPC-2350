// Production keyboard parser + queue, with hardware/Arduino boundaries stubbed.
#define GLOBALS_H
#define MY_FUNCTIONS_H
#define NATIVE_RUNTIME_H
#define __not_in_flash_func(name) name
#include "ps2_keyboard_driver.h"
#include "keyboard_runtime.h"
#include <cassert>
#include <cctype>
#include <cstdint>
#include <iostream>

static uint32_t now_us, arduino_irq_calls, transport_polls, mouse_polls;
static unsigned shortcut_calls;
static bool replenish_shortcuts;
bool isAlpha(char c) { return std::isalpha((unsigned char)c) != 0; }
void noInterrupts() { ++arduino_irq_calls; }
void interrupts() { ++arduino_irq_calls; }
uint32_t time_us_32() { return now_us; }
void ps2_pio_poll() { ++transport_polls; }
void mouse_runtime_poll() { ++mouse_polls; }
bool native_request_focus_next()
{
  ++shortcut_calls;
  if(replenish_shortcuts)
  {
    // A producer that replenishes input during every decoded shortcut must
    // not trap the foreground in a never-empty receive queue.
    ps2_keyboard_receive_byte(0xf0, true);
    ps2_keyboard_receive_byte(0x06, true);
    ps2_keyboard_receive_byte(0x06, true);
  }
  return true;
}
bool native_request_toggle_mode() { ++shortcut_calls; return true; }
bool native_request_stop() { ++shortcut_calls; return true; }

#include "ps2_keyboard_driver.cpp"

static Ps2KeyboardDiagnostics diagnostics()
{ Ps2KeyboardDiagnostics result = {}; ps2_keyboard_get_diagnostics(result); return result; }
static void fresh()
{
  keyboard_runtime_reset();
  ps2_reset_make_break_state(true);
  ps2_rx_head = ps2_rx_tail = ps2_resync_requested = 0;
  ps2_received_bytes = ps2_clock_edges = ps2_invalid_frames = 0;
  ps2_queue_overflows = ps2_poll_count = ps2_parsed_bytes = ps2_recoveries = 0;
  ps2_poll_stage = PS2_POLL_IDLE;
  now_us = arduino_irq_calls = transport_polls = mouse_polls = 0;
  shortcut_calls = 0; replenish_shortcuts = false;
}
static void receive(uint8_t byte)
{ now_us += 700; ps2_keyboard_receive_byte(byte, true); }
static void poll()
{
  ps2_keyboard_poll();
  assert(arduino_irq_calls == 0);
  assert(diagnostics().poll_stage == PS2_POLL_IDLE);
}

static void idle_then_typing()
{
  fresh();
  now_us = 0xffff0000u;
  for(unsigned i = 0; i < 20000000u; ++i) { now_us += 1000u; ps2_keyboard_poll(); }
  assert(diagnostics().polls == 20000000u && diagnostics().parsed_bytes == 0);
  assert(transport_polls == 20000000u && mouse_polls == 20000000u);
  assert(arduino_irq_calls == 0);
  receive(0x12); receive(0x1c); receive(0xf0); receive(0x1c); receive(0xf0); receive(0x12);
  poll();
  KeyboardEvent event;
  bool capital = false, release = false;
  while(keyboard_poll_event(KEYBOARD_FOCUS_TERMINAL, event))
  {
    if(event.key == KEYBOARD_KEY_A && event.action == KEYBOARD_ACTION_PRESS)
      capital = event.character == 'A' && (event.modifiers & KEYBOARD_MOD_SHIFT);
    if(event.key == KEYBOARD_KEY_A && event.action == KEYBOARD_ACTION_RELEASE) release = true;
  }
  assert(capital && release && !keyboard_key_down(KEYBOARD_KEY_LEFT_SHIFT));
  receive(0xe0); receive(0x75); receive(0xe0); receive(0xf0); receive(0x75);
  poll();
  assert(keyboard_poll_event(KEYBOARD_FOCUS_TERMINAL, event) && event.key == KEYBOARD_KEY_UP);
  assert(keyboard_poll_event(KEYBOARD_FOCUS_TERMINAL, event) && event.action == KEYBOARD_ACTION_RELEASE);
}

static void full_queue_and_recovery()
{
  fresh();
  // Reproduce the reported RX=153, OVERRUN=122, QUEUED=31 snapshot.
  for(unsigned i = 0; i < 153; ++i) receive(0x1c);
  auto before = diagnostics();
  assert(before.received_bytes == 153 && before.queue_overflows == 122 && before.queue_pending == 31);
  poll();
  assert(diagnostics().recoveries == 1 && diagnostics().queue_pending == 0);
  receive(0x1c); poll();
  KeyboardEvent event;
  assert(keyboard_poll_event(KEYBOARD_FOCUS_TERMINAL, event) && event.character == 'a');
  assert(keyboard_key_down(KEYBOARD_KEY_A));
  ps2_keyboard_receive_byte(0, false); poll();
  assert(!keyboard_key_down(KEYBOARD_KEY_A) && diagnostics().recoveries == 2);
  receive(0xe0); poll(); // Aborted prefix must not affect the next normal key.
  ps2_keyboard_receive_byte(0, false); poll();
  receive(0x1c); poll();
  assert(keyboard_poll_event(KEYBOARD_FOCUS_TERMINAL, event) && event.character == 'a');
}

static void bounded_replenished_queue()
{
  fresh();
  replenish_shortcuts = true;
  receive(0x11); receive(0x06); poll();
  assert(diagnostics().parsed_bytes == 31 && diagnostics().queue_pending > 0);
  assert(shortcut_calls > 1 && diagnostics().queue_overflows == 0);
  replenish_shortcuts = false;
  poll();
  assert(diagnostics().queue_pending == 0);
  receive(0xf0); receive(0x11); poll();
  assert(!keyboard_key_down(KEYBOARD_KEY_LEFT_ALT));
  keyboard_set_focus(KEYBOARD_FOCUS_NARM);
  receive(0x1c); poll();
  KeyboardEvent event;
  assert(!keyboard_poll_event(KEYBOARD_FOCUS_TERMINAL, event));
  assert(keyboard_poll_event(KEYBOARD_FOCUS_NARM, event) && event.character == 'a');
}

int main()
{
  idle_then_typing(); full_queue_and_recovery(); bounded_replenished_queue();
  std::cout << "Keyboard poll tests passed: 20 million idle polls, make/break/modifiers, extended keys, observed full queue/recovery, bounded live refill, focus; zero Arduino IRQ-wrapper calls.\n";
}
