#include "keyboard_runtime.h"

#include <string.h>

namespace
{
static constexpr uint8_t EVENT_QUEUE_SIZE = 32;
static constexpr uint8_t EVENT_QUEUE_MASK = EVENT_QUEUE_SIZE - 1;
static_assert((EVENT_QUEUE_SIZE & EVENT_QUEUE_MASK) == 0,
              "keyboard event queue must have a power-of-two size");

// The reserved ABI field carries the queue owner internally. It is cleared
// before delivery, avoiding per-slot padding solely for an ownership byte.
KeyboardEvent event_queue[EVENT_QUEUE_SIZE];
// Monotonic counters keep all 32 slots usable and let focus/reset flushes race
// safely with a consumer by using compare-and-exchange on the tail.
volatile uint32_t event_head = 0;
volatile uint32_t event_tail = 0;
volatile uint8_t focused_consumer = KEYBOARD_FOCUS_TERMINAL;
volatile uint32_t dropped_events = 0;
volatile uint32_t key_state[8] = {};
volatile uint32_t lock_modifiers = KEYBOARD_MOD_NUM_LOCK;

bool modifier_key_down(uint16_t left, uint16_t right)
{
  return keyboard_key_down(left) || keyboard_key_down(right);
}

void set_key_state(uint16_t key, bool down)
{
  if (key == KEYBOARD_KEY_NONE || key > 0xffu) return;
  volatile uint32_t *word = &key_state[key >> 5];
  uint32_t mask = 1u << (key & 31u);
  if (down)
    __atomic_fetch_or(word, mask, __ATOMIC_RELEASE);
  else
    __atomic_fetch_and(word, ~mask, __ATOMIC_RELEASE);
}
} // namespace

uint16_t keyboard_key_from_ps2(uint8_t scan_code, bool extended)
{
  if (extended)
  {
    switch (scan_code)
    {
      case 0x11: return KEYBOARD_KEY_RIGHT_ALT;
      case 0x14: return KEYBOARD_KEY_RIGHT_CONTROL;
      case 0x1f: return KEYBOARD_KEY_LEFT_GUI;
      case 0x27: return KEYBOARD_KEY_RIGHT_GUI;
      case 0x2f: return KEYBOARD_KEY_APPLICATION;
      case 0x4a: return KEYBOARD_KEY_KEYPAD_DIVIDE;
      case 0x5a: return KEYBOARD_KEY_KEYPAD_ENTER;
      case 0x69: return KEYBOARD_KEY_END;
      case 0x6b: return KEYBOARD_KEY_LEFT;
      case 0x6c: return KEYBOARD_KEY_HOME;
      case 0x70: return KEYBOARD_KEY_INSERT;
      case 0x71: return KEYBOARD_KEY_DELETE;
      case 0x72: return KEYBOARD_KEY_DOWN;
      case 0x74: return KEYBOARD_KEY_RIGHT;
      case 0x75: return KEYBOARD_KEY_UP;
      case 0x7a: return KEYBOARD_KEY_PAGE_DOWN;
      case 0x7c: return KEYBOARD_KEY_PRINT_SCREEN;
      case 0x7d: return KEYBOARD_KEY_PAGE_UP;
      default: return KEYBOARD_KEY_NONE;
    }
  }

  switch (scan_code)
  {
    case 0x01: return KEYBOARD_KEY_F9;
    case 0x03: return KEYBOARD_KEY_F5;
    case 0x04: return KEYBOARD_KEY_F3;
    case 0x05: return KEYBOARD_KEY_F1;
    case 0x06: return KEYBOARD_KEY_F2;
    case 0x07: return KEYBOARD_KEY_F12;
    case 0x09: return KEYBOARD_KEY_F10;
    case 0x0a: return KEYBOARD_KEY_F8;
    case 0x0b: return KEYBOARD_KEY_F6;
    case 0x0c: return KEYBOARD_KEY_F4;
    case 0x0d: return KEYBOARD_KEY_TAB;
    case 0x0e: return KEYBOARD_KEY_GRAVE;
    case 0x11: return KEYBOARD_KEY_LEFT_ALT;
    case 0x12: return KEYBOARD_KEY_LEFT_SHIFT;
    case 0x14: return KEYBOARD_KEY_LEFT_CONTROL;
    case 0x15: return KEYBOARD_KEY_Q;
    case 0x16: return KEYBOARD_KEY_1;
    case 0x1a: return KEYBOARD_KEY_Z;
    case 0x1b: return KEYBOARD_KEY_S;
    case 0x1c: return KEYBOARD_KEY_A;
    case 0x1d: return KEYBOARD_KEY_W;
    case 0x1e: return KEYBOARD_KEY_2;
    case 0x21: return KEYBOARD_KEY_C;
    case 0x22: return KEYBOARD_KEY_X;
    case 0x23: return KEYBOARD_KEY_D;
    case 0x24: return KEYBOARD_KEY_E;
    case 0x25: return KEYBOARD_KEY_4;
    case 0x26: return KEYBOARD_KEY_3;
    case 0x29: return KEYBOARD_KEY_SPACE;
    case 0x2a: return KEYBOARD_KEY_V;
    case 0x2b: return KEYBOARD_KEY_F;
    case 0x2c: return KEYBOARD_KEY_T;
    case 0x2d: return KEYBOARD_KEY_R;
    case 0x2e: return KEYBOARD_KEY_5;
    case 0x31: return KEYBOARD_KEY_N;
    case 0x32: return KEYBOARD_KEY_B;
    case 0x33: return KEYBOARD_KEY_H;
    case 0x34: return KEYBOARD_KEY_G;
    case 0x35: return KEYBOARD_KEY_Y;
    case 0x36: return KEYBOARD_KEY_6;
    case 0x3a: return KEYBOARD_KEY_M;
    case 0x3b: return KEYBOARD_KEY_J;
    case 0x3c: return KEYBOARD_KEY_U;
    case 0x3d: return KEYBOARD_KEY_7;
    case 0x3e: return KEYBOARD_KEY_8;
    case 0x41: return KEYBOARD_KEY_COMMA;
    case 0x42: return KEYBOARD_KEY_K;
    case 0x43: return KEYBOARD_KEY_I;
    case 0x44: return KEYBOARD_KEY_O;
    case 0x45: return KEYBOARD_KEY_0;
    case 0x46: return KEYBOARD_KEY_9;
    case 0x49: return KEYBOARD_KEY_PERIOD;
    case 0x4a: return KEYBOARD_KEY_SLASH;
    case 0x4b: return KEYBOARD_KEY_L;
    case 0x4c: return KEYBOARD_KEY_SEMICOLON;
    case 0x4d: return KEYBOARD_KEY_P;
    case 0x4e: return KEYBOARD_KEY_MINUS;
    case 0x52: return KEYBOARD_KEY_APOSTROPHE;
    case 0x54: return KEYBOARD_KEY_LEFT_BRACKET;
    case 0x55: return KEYBOARD_KEY_EQUAL;
    case 0x58: return KEYBOARD_KEY_CAPS_LOCK;
    case 0x59: return KEYBOARD_KEY_RIGHT_SHIFT;
    case 0x5a: return KEYBOARD_KEY_ENTER;
    case 0x5b: return KEYBOARD_KEY_RIGHT_BRACKET;
    case 0x5d: return KEYBOARD_KEY_BACKSLASH;
    case 0x66: return KEYBOARD_KEY_BACKSPACE;
    case 0x69: return KEYBOARD_KEY_KEYPAD_1;
    case 0x6b: return KEYBOARD_KEY_KEYPAD_4;
    case 0x6c: return KEYBOARD_KEY_KEYPAD_7;
    case 0x70: return KEYBOARD_KEY_KEYPAD_0;
    case 0x71: return KEYBOARD_KEY_KEYPAD_PERIOD;
    case 0x72: return KEYBOARD_KEY_KEYPAD_2;
    case 0x73: return KEYBOARD_KEY_KEYPAD_5;
    case 0x74: return KEYBOARD_KEY_KEYPAD_6;
    case 0x75: return KEYBOARD_KEY_KEYPAD_8;
    case 0x76: return KEYBOARD_KEY_ESCAPE;
    case 0x77: return KEYBOARD_KEY_NUM_LOCK;
    case 0x78: return KEYBOARD_KEY_F11;
    case 0x79: return KEYBOARD_KEY_KEYPAD_PLUS;
    case 0x7a: return KEYBOARD_KEY_KEYPAD_3;
    case 0x7b: return KEYBOARD_KEY_KEYPAD_MINUS;
    case 0x7c: return KEYBOARD_KEY_KEYPAD_MULTIPLY;
    case 0x7d: return KEYBOARD_KEY_KEYPAD_9;
    case 0x7e: return KEYBOARD_KEY_SCROLL_LOCK;
    case 0x83: return KEYBOARD_KEY_F7;
    default: return KEYBOARD_KEY_NONE;
  }
}

void keyboard_runtime_reset()
{
  memset((void *)key_state, 0, sizeof(key_state));
  __atomic_store_n(&lock_modifiers, KEYBOARD_MOD_NUM_LOCK, __ATOMIC_RELEASE);
  __atomic_store_n(&focused_consumer, KEYBOARD_FOCUS_TERMINAL, __ATOMIC_RELEASE);
  __atomic_store_n(&event_head, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&event_tail, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&dropped_events, 0u, __ATOMIC_RELEASE);
}

void keyboard_release_all_keys()
{
  memset((void *)key_state, 0, sizeof(key_state));
  keyboard_clear_events();
}

void keyboard_set_focus(KeyboardFocus focus)
{
  uint8_t old = __atomic_exchange_n(&focused_consumer, (uint8_t)focus, __ATOMIC_ACQ_REL);
  if (old != (uint8_t)focus) keyboard_clear_events();
}

KeyboardFocus keyboard_get_focus()
{
  return (KeyboardFocus)__atomic_load_n(&focused_consumer, __ATOMIC_ACQUIRE);
}

void keyboard_clear_events()
{
  uint32_t head = __atomic_load_n(&event_head, __ATOMIC_ACQUIRE);
  __atomic_store_n(&event_tail, head, __ATOMIC_RELEASE);
}

KeyboardTransition keyboard_update_ps2_state(uint8_t scan_code,
                                             bool extended,
                                             bool released)
{
  KeyboardTransition transition = {};
  transition.key = keyboard_key_from_ps2(scan_code, extended);
  transition.physical_code = (uint16_t)scan_code | (extended ? 0x0100u : 0u);
  bool was_down = keyboard_key_down(transition.key);
  transition.action = released ? KEYBOARD_ACTION_RELEASE
                               : (was_down ? KEYBOARD_ACTION_REPEAT : KEYBOARD_ACTION_PRESS);
  set_key_state(transition.key, !released);

  if (transition.action == KEYBOARD_ACTION_PRESS)
  {
    if (transition.key == KEYBOARD_KEY_CAPS_LOCK)
      __atomic_fetch_xor(&lock_modifiers, KEYBOARD_MOD_CAPS_LOCK, __ATOMIC_ACQ_REL);
    else if (transition.key == KEYBOARD_KEY_NUM_LOCK)
      __atomic_fetch_xor(&lock_modifiers, KEYBOARD_MOD_NUM_LOCK, __ATOMIC_ACQ_REL);
  }
  transition.modifiers = keyboard_modifiers();
  return transition;
}

bool keyboard_publish_event(const KeyboardTransition &transition,
                            uint32_t character,
                            bool printable)
{
  if (transition.key == KEYBOARD_KEY_NONE && !printable) return false;
  uint32_t head = __atomic_load_n(&event_head, __ATOMIC_RELAXED);
  uint32_t tail = __atomic_load_n(&event_tail, __ATOMIC_ACQUIRE);
  if ((uint32_t)(head - tail) >= EVENT_QUEUE_SIZE)
  {
    __atomic_fetch_add(&dropped_events, 1u, __ATOMIC_RELAXED);
    return false;
  }

  KeyboardEvent &slot = event_queue[head & EVENT_QUEUE_MASK];
  slot.key = transition.key;
  slot.action = transition.action;
  slot.modifiers = transition.modifiers;
  slot.character = printable && transition.action != KEYBOARD_ACTION_RELEASE
                     ? character : 0u;
  slot.physical_code = transition.physical_code;
  slot.reserved = __atomic_load_n(&focused_consumer, __ATOMIC_ACQUIRE);
  __atomic_store_n(&event_head, head + 1u, __ATOMIC_RELEASE);
  return true;
}

bool keyboard_poll_event(KeyboardFocus consumer, KeyboardEvent &event)
{
  if (__atomic_load_n(&focused_consumer, __ATOMIC_ACQUIRE) != (uint8_t)consumer)
    return false;

  while (true)
  {
    uint32_t tail = __atomic_load_n(&event_tail, __ATOMIC_ACQUIRE);
    if (tail == __atomic_load_n(&event_head, __ATOMIC_ACQUIRE)) return false;
    KeyboardEvent slot = event_queue[tail & EVENT_QUEUE_MASK];
    uint32_t expected = tail;
    if (!__atomic_compare_exchange_n(&event_tail, &expected, tail + 1u,
                                     false, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE))
      continue;
    if (slot.reserved == (uint16_t)consumer)
    {
      slot.reserved = 0;
      event = slot;
      return true;
    }
  }
}

bool keyboard_key_down(uint16_t key)
{
  if (key == KEYBOARD_KEY_NONE || key > 0xffu) return false;
  uint32_t word = __atomic_load_n(&key_state[key >> 5], __ATOMIC_ACQUIRE);
  return (word & (1u << (key & 31u))) != 0;
}

bool keyboard_ps2_key_down(uint8_t scan_code, bool extended)
{
  return keyboard_key_down(keyboard_key_from_ps2(scan_code, extended));
}

uint8_t keyboard_modifiers()
{
  uint8_t modifiers = (uint8_t)__atomic_load_n(&lock_modifiers, __ATOMIC_ACQUIRE);
  if (modifier_key_down(KEYBOARD_KEY_LEFT_SHIFT, KEYBOARD_KEY_RIGHT_SHIFT))
    modifiers |= KEYBOARD_MOD_SHIFT;
  if (modifier_key_down(KEYBOARD_KEY_LEFT_CONTROL, KEYBOARD_KEY_RIGHT_CONTROL))
    modifiers |= KEYBOARD_MOD_CONTROL;
  if (modifier_key_down(KEYBOARD_KEY_LEFT_ALT, KEYBOARD_KEY_RIGHT_ALT))
    modifiers |= KEYBOARD_MOD_ALT;
  if (modifier_key_down(KEYBOARD_KEY_LEFT_GUI, KEYBOARD_KEY_RIGHT_GUI))
    modifiers |= KEYBOARD_MOD_GUI;
  return modifiers;
}

uint32_t keyboard_dropped_events()
{
  return __atomic_load_n(&dropped_events, __ATOMIC_ACQUIRE);
}
