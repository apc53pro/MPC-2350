#ifndef KEYBOARD_RUNTIME_H
#define KEYBOARD_RUNTIME_H

#include <stdint.h>

// Stable key identifiers use the USB HID Keyboard/Keypad usage values.  They
// are independent of the active PS/2 transport and can be reused by a future
// USB keyboard driver without changing NARM programs.
enum KeyboardKey : uint16_t
{
  KEYBOARD_KEY_NONE = 0x00,
  KEYBOARD_KEY_A = 0x04,
  KEYBOARD_KEY_B = 0x05,
  KEYBOARD_KEY_C = 0x06,
  KEYBOARD_KEY_D = 0x07,
  KEYBOARD_KEY_E = 0x08,
  KEYBOARD_KEY_F = 0x09,
  KEYBOARD_KEY_G = 0x0a,
  KEYBOARD_KEY_H = 0x0b,
  KEYBOARD_KEY_I = 0x0c,
  KEYBOARD_KEY_J = 0x0d,
  KEYBOARD_KEY_K = 0x0e,
  KEYBOARD_KEY_L = 0x0f,
  KEYBOARD_KEY_M = 0x10,
  KEYBOARD_KEY_N = 0x11,
  KEYBOARD_KEY_O = 0x12,
  KEYBOARD_KEY_P = 0x13,
  KEYBOARD_KEY_Q = 0x14,
  KEYBOARD_KEY_R = 0x15,
  KEYBOARD_KEY_S = 0x16,
  KEYBOARD_KEY_T = 0x17,
  KEYBOARD_KEY_U = 0x18,
  KEYBOARD_KEY_V = 0x19,
  KEYBOARD_KEY_W = 0x1a,
  KEYBOARD_KEY_X = 0x1b,
  KEYBOARD_KEY_Y = 0x1c,
  KEYBOARD_KEY_Z = 0x1d,
  KEYBOARD_KEY_1 = 0x1e,
  KEYBOARD_KEY_2 = 0x1f,
  KEYBOARD_KEY_3 = 0x20,
  KEYBOARD_KEY_4 = 0x21,
  KEYBOARD_KEY_5 = 0x22,
  KEYBOARD_KEY_6 = 0x23,
  KEYBOARD_KEY_7 = 0x24,
  KEYBOARD_KEY_8 = 0x25,
  KEYBOARD_KEY_9 = 0x26,
  KEYBOARD_KEY_0 = 0x27,
  KEYBOARD_KEY_ENTER = 0x28,
  KEYBOARD_KEY_ESCAPE = 0x29,
  KEYBOARD_KEY_BACKSPACE = 0x2a,
  KEYBOARD_KEY_TAB = 0x2b,
  KEYBOARD_KEY_SPACE = 0x2c,
  KEYBOARD_KEY_MINUS = 0x2d,
  KEYBOARD_KEY_EQUAL = 0x2e,
  KEYBOARD_KEY_LEFT_BRACKET = 0x2f,
  KEYBOARD_KEY_RIGHT_BRACKET = 0x30,
  KEYBOARD_KEY_BACKSLASH = 0x31,
  KEYBOARD_KEY_SEMICOLON = 0x33,
  KEYBOARD_KEY_APOSTROPHE = 0x34,
  KEYBOARD_KEY_GRAVE = 0x35,
  KEYBOARD_KEY_COMMA = 0x36,
  KEYBOARD_KEY_PERIOD = 0x37,
  KEYBOARD_KEY_SLASH = 0x38,
  KEYBOARD_KEY_CAPS_LOCK = 0x39,
  KEYBOARD_KEY_F1 = 0x3a,
  KEYBOARD_KEY_F2 = 0x3b,
  KEYBOARD_KEY_F3 = 0x3c,
  KEYBOARD_KEY_F4 = 0x3d,
  KEYBOARD_KEY_F5 = 0x3e,
  KEYBOARD_KEY_F6 = 0x3f,
  KEYBOARD_KEY_F7 = 0x40,
  KEYBOARD_KEY_F8 = 0x41,
  KEYBOARD_KEY_F9 = 0x42,
  KEYBOARD_KEY_F10 = 0x43,
  KEYBOARD_KEY_F11 = 0x44,
  KEYBOARD_KEY_F12 = 0x45,
  KEYBOARD_KEY_PRINT_SCREEN = 0x46,
  KEYBOARD_KEY_SCROLL_LOCK = 0x47,
  KEYBOARD_KEY_INSERT = 0x49,
  KEYBOARD_KEY_HOME = 0x4a,
  KEYBOARD_KEY_PAGE_UP = 0x4b,
  KEYBOARD_KEY_DELETE = 0x4c,
  KEYBOARD_KEY_END = 0x4d,
  KEYBOARD_KEY_PAGE_DOWN = 0x4e,
  KEYBOARD_KEY_RIGHT = 0x4f,
  KEYBOARD_KEY_LEFT = 0x50,
  KEYBOARD_KEY_DOWN = 0x51,
  KEYBOARD_KEY_UP = 0x52,
  KEYBOARD_KEY_NUM_LOCK = 0x53,
  KEYBOARD_KEY_KEYPAD_DIVIDE = 0x54,
  KEYBOARD_KEY_KEYPAD_MULTIPLY = 0x55,
  KEYBOARD_KEY_KEYPAD_MINUS = 0x56,
  KEYBOARD_KEY_KEYPAD_PLUS = 0x57,
  KEYBOARD_KEY_KEYPAD_ENTER = 0x58,
  KEYBOARD_KEY_KEYPAD_1 = 0x59,
  KEYBOARD_KEY_KEYPAD_2 = 0x5a,
  KEYBOARD_KEY_KEYPAD_3 = 0x5b,
  KEYBOARD_KEY_KEYPAD_4 = 0x5c,
  KEYBOARD_KEY_KEYPAD_5 = 0x5d,
  KEYBOARD_KEY_KEYPAD_6 = 0x5e,
  KEYBOARD_KEY_KEYPAD_7 = 0x5f,
  KEYBOARD_KEY_KEYPAD_8 = 0x60,
  KEYBOARD_KEY_KEYPAD_9 = 0x61,
  KEYBOARD_KEY_KEYPAD_0 = 0x62,
  KEYBOARD_KEY_KEYPAD_PERIOD = 0x63,
  KEYBOARD_KEY_APPLICATION = 0x65,
  KEYBOARD_KEY_LEFT_CONTROL = 0xe0,
  KEYBOARD_KEY_LEFT_SHIFT = 0xe1,
  KEYBOARD_KEY_LEFT_ALT = 0xe2,
  KEYBOARD_KEY_LEFT_GUI = 0xe3,
  KEYBOARD_KEY_RIGHT_CONTROL = 0xe4,
  KEYBOARD_KEY_RIGHT_SHIFT = 0xe5,
  KEYBOARD_KEY_RIGHT_ALT = 0xe6,
  KEYBOARD_KEY_RIGHT_GUI = 0xe7
};

enum KeyboardAction : uint8_t
{
  KEYBOARD_ACTION_PRESS = 1,
  KEYBOARD_ACTION_RELEASE = 2,
  KEYBOARD_ACTION_REPEAT = 3
};

enum KeyboardModifier : uint8_t
{
  KEYBOARD_MOD_SHIFT = 1u << 0,
  KEYBOARD_MOD_CONTROL = 1u << 1,
  KEYBOARD_MOD_ALT = 1u << 2,
  KEYBOARD_MOD_GUI = 1u << 3,
  KEYBOARD_MOD_CAPS_LOCK = 1u << 4,
  KEYBOARD_MOD_NUM_LOCK = 1u << 5
};

enum KeyboardFocus : uint8_t
{
  KEYBOARD_FOCUS_TERMINAL = 0,
  KEYBOARD_FOCUS_NARM = 1
};

struct KeyboardEvent
{
  uint16_t key;
  uint8_t action;
  uint8_t modifiers;
  uint32_t character;
  uint16_t physical_code;
  uint16_t reserved;
};

struct KeyboardTransition
{
  uint16_t key;
  uint16_t physical_code;
  uint8_t action;
  uint8_t modifiers;
};

void keyboard_runtime_reset();
void keyboard_release_all_keys();
void keyboard_set_focus(KeyboardFocus focus);
KeyboardFocus keyboard_get_focus();
void keyboard_clear_events();

KeyboardTransition keyboard_update_ps2_state(uint8_t scan_code,
                                             bool extended,
                                             bool released);
bool keyboard_publish_event(const KeyboardTransition &transition,
                            uint32_t character,
                            bool printable);
bool keyboard_poll_event(KeyboardFocus consumer, KeyboardEvent &event);

uint16_t keyboard_key_from_ps2(uint8_t scan_code, bool extended);
bool keyboard_key_down(uint16_t key);
bool keyboard_ps2_key_down(uint8_t scan_code, bool extended = false);
uint8_t keyboard_modifiers();
uint32_t keyboard_dropped_events();

#endif
