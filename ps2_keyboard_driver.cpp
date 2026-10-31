#include "globals.h"
#include "ps2_keyboard_driver.h"
#include "mms.h"
#include "native_runtime.h"
#include "keyboard_runtime.h"
#include "hardware/gpio.h"
#include "pico/time.h"

static volatile uint16_t event_packet = 0;
static volatile int bitcount = 0;
static volatile int databitcount = 0;
static volatile uint8_t key_byte = 0;

static volatile bool event_packetReady = false;

static uint8_t key_bytes[10] = {0 , 0 , 0 , 0 , 0 , 0 , 0 , 0};
static uint8_t volatile key_byteCount = 0;
static uint8_t volatile make_break_size = 0;

uint16_t non_ext_make_LUT[256][2];

static volatile bool ext_key = false;
static volatile bool releasing = false;
static volatile bool is_printable = true;

static volatile bool shift = false;
static volatile bool caps = false;
static volatile bool numLock = true;

// System shortcuts are decoded directly from the PS/2 Set-2 stream.  They
// must not depend on the focused application's event queue or on a function
// key being classified as a fresh normalized PRESS (a lost break byte can
// otherwise turn the next make into REPEAT and make the shortcut disappear).
static uint8_t ps2_system_alt_mask = 0u;
static uint8_t ps2_system_shortcut_latch = 0;
static volatile uint32_t ps2_received_bytes = 0u;
static uint32_t ps2_recoveries = 0u;
static uint32_t ps2_last_transition = 0u;
static uint32_t ps2_shortcut_presses[3] = {};
static constexpr uint8_t PS2_SHORTCUT_F2 = 1u << 0;
static constexpr uint8_t PS2_SHORTCUT_F3 = 1u << 1;
static constexpr uint8_t PS2_SHORTCUT_F4 = 1u << 2;

// The keyboard ISR and ps2_keyboard_poll() form a single-producer/single-consumer queue
// on Core 1.  Keeping complete bytes here prevents a new PS/2 frame from
// overwriting an older frame while the foreground code is busy.
static constexpr uint8_t PS2_RX_QUEUE_SIZE = 32;
static constexpr uint8_t PS2_RX_QUEUE_MASK = PS2_RX_QUEUE_SIZE - 1;
static constexpr uint32_t PS2_FRAME_TIMEOUT_US = 250;
static_assert((PS2_RX_QUEUE_SIZE & PS2_RX_QUEUE_MASK) == 0,
              "PS/2 receive queue must have a power-of-two size");

static volatile uint8_t ps2_rx_queue[PS2_RX_QUEUE_SIZE];
static volatile uint8_t ps2_rx_head = 0;
static volatile uint8_t ps2_rx_tail = 0;
static volatile uint8_t ps2_frame_data = 0;
static volatile uint8_t ps2_parity_bit = 0;
static volatile uint8_t ps2_resync_requested = 0;
static volatile uint32_t ps2_last_edge_us = 0;

static inline __attribute__((always_inline)) void ps2_reset_isr_frame()
{
  bitcount = 0;
  databitcount = 0;
  ps2_frame_data = 0;
  event_packet = 0;
  ps2_parity_bit = 0;
}

void __not_in_flash_func(ps2_keyboard_clock_isr)()
{
  const uint32_t now = time_us_32();
  const uint8_t data = (uint8_t)gpio_get(PS2_DATA);

  // A missing clock edge must not shift every subsequent frame forever.
  if (bitcount != 0 && (uint32_t)(now - ps2_last_edge_us) > PS2_FRAME_TIMEOUT_US)
  {
    ps2_reset_isr_frame();
    ps2_resync_requested = 1;
  }
  ps2_last_edge_us = now;

  // A PS/2 frame always starts with a zero start bit.  Idle-line noise is
  // ignored rather than being allowed to become bit zero of a fake byte.
  if (bitcount == 0)
  {
    if (data != 0)
    {
      return;
    }
    event_packet = 0;
    ps2_frame_data = 0;
    databitcount = 0;
    bitcount = 1;
    return;
  }

  event_packet |= (uint16_t)data << bitcount;

  if (bitcount >= 1 && bitcount <= 8)
  {
    ps2_frame_data |= data << databitcount;
    databitcount++;
  }
  else if (bitcount == 9)
  {
    ps2_parity_bit = data;
  }

  bitcount++;

  if(bitcount == 11)
  {
    // Set 2 uses odd parity across the eight data bits and parity bit.
    const uint8_t parity_valid = (uint8_t)
      ((__builtin_parity((unsigned int)ps2_frame_data) ^ ps2_parity_bit) != 0);
    const uint8_t stop_valid = data;
    if (parity_valid && stop_valid)
    {
      ++ps2_received_bytes;
      const uint8_t head = ps2_rx_head;
      const uint8_t next = (uint8_t)((head + 1u) & PS2_RX_QUEUE_MASK);
      if (next != ps2_rx_tail)
      {
        ps2_rx_queue[head] = ps2_frame_data;
        __dmb();
        ps2_rx_head = next;
        event_packetReady = true;
      }
      else
      {
        // Dropping a prefix or break byte can leave the make/break parser in
        // the wrong state, so queue overflow explicitly requests recovery.
        ps2_resync_requested = 1;
      }
    }
    else
    {
      ps2_resync_requested = 1;
    }
    ps2_reset_isr_frame();
  }
}

static void ps2_update_legacy_translation_lut()
{
  non_ext_make_LUT[0x1C][0] = 'a';
  non_ext_make_LUT[0x32][0] = 'b';
  non_ext_make_LUT[0x21][0] = 'c';
  non_ext_make_LUT[0x23][0] = 'd';
  non_ext_make_LUT[0x24][0] = 'e';
  non_ext_make_LUT[0x2b][0] = 'f';
  non_ext_make_LUT[0x34][0] = 'g';
  non_ext_make_LUT[0x33][0] = 'h';
  non_ext_make_LUT[0x43][0] = 'i';
  non_ext_make_LUT[0x3b][0] = 'j';
  non_ext_make_LUT[0x42][0] = 'k';
  non_ext_make_LUT[0x4b][0] = 'l';
  non_ext_make_LUT[0x3a][0] = 'm';
  non_ext_make_LUT[0x31][0] = 'n';
  non_ext_make_LUT[0x44][0] = 'o';
  non_ext_make_LUT[0x4d][0] = 'p';
  non_ext_make_LUT[0x15][0] = 'q';
  non_ext_make_LUT[0x2d][0] = 'r';
  non_ext_make_LUT[0x1b][0] = 's';
  non_ext_make_LUT[0x2c][0] = 't';
  non_ext_make_LUT[0x3c][0] = 'u';
  non_ext_make_LUT[0x2a][0] = 'v';
  non_ext_make_LUT[0x1d][0] = 'w';
  non_ext_make_LUT[0x22][0] = 'x';
  non_ext_make_LUT[0x35][0] = 'y';
  non_ext_make_LUT[0x1a][0] = 'z';

  non_ext_make_LUT[0x0D][0] = '\t'; // tab
  non_ext_make_LUT[0x29][0] = ' '; //space

  //CapsLock                                 NUMLOCK                                   SHIFT                                             ESC                       BACKSPACE                                     ALT
  non_ext_make_LUT[0x58][0] = 20000;       non_ext_make_LUT[0x77][0] = 20011;    non_ext_make_LUT[0x12][0] = 20012;        non_ext_make_LUT[0x76][0] = 20014;      non_ext_make_LUT[0x66][0] = 20013;         non_ext_make_LUT[0x11][0] = 20027; 

  non_ext_make_LUT[0x05][0] = 20015; //F1   
  non_ext_make_LUT[0x06][0] = 20016; //F2
  non_ext_make_LUT[0x04][0] = 20017; //F3
  non_ext_make_LUT[0x0c][0] = 20018; //F4
  non_ext_make_LUT[0x03][0] = 20019; //F5
  non_ext_make_LUT[0x0b][0] = 20020; //F6
  non_ext_make_LUT[0x83][0] = 20021; //F7
  non_ext_make_LUT[0x0a][0] = 20022; //F8
  non_ext_make_LUT[0x01][0] = 20023; //F9
  non_ext_make_LUT[0x09][0] = 20024; //F10
  non_ext_make_LUT[0x78][0] = 20025; //F11
  non_ext_make_LUT[0x07][0] = 20026; //F12

  if(shift == false)
  {
    non_ext_make_LUT[0x45][0] = '0';
    non_ext_make_LUT[0x16][0] = '1';
    non_ext_make_LUT[0x1e][0] = '2';
    non_ext_make_LUT[0x26][0] = '3';
    non_ext_make_LUT[0x25][0] = '4';
    non_ext_make_LUT[0x2e][0] = '5';
    non_ext_make_LUT[0x36][0] = '6';
    non_ext_make_LUT[0x3d][0] = '7';
    non_ext_make_LUT[0x3e][0] = '8';
    non_ext_make_LUT[0x46][0] = '9';

    non_ext_make_LUT[0x4E][0] = '-';
    non_ext_make_LUT[0x55][0] = '=';
    non_ext_make_LUT[0x0E][0] = '\'';
    non_ext_make_LUT[0x61][0] = '\\';
    non_ext_make_LUT[0x5B][0] = '[';
    non_ext_make_LUT[0x5D][0] = ']';
    non_ext_make_LUT[0x4A][0] = ';';
    non_ext_make_LUT[0x51][0] = '/';
    non_ext_make_LUT[0x41][0] = ',';
    non_ext_make_LUT[0x49][0] = '.';
    non_ext_make_LUT[0x52][0] = '~';
  }else
  {
    non_ext_make_LUT[0x45][0] = ')';
    non_ext_make_LUT[0x16][0] = '!';
    non_ext_make_LUT[0x1e][0] = '@';
    non_ext_make_LUT[0x26][0] = '#';
    non_ext_make_LUT[0x25][0] = '$';
    non_ext_make_LUT[0x2e][0] = '%';
    non_ext_make_LUT[0x36][0] = '^';
    non_ext_make_LUT[0x3d][0] = '&';
    non_ext_make_LUT[0x3e][0] = '*';
    non_ext_make_LUT[0x46][0] = '(';

    non_ext_make_LUT[0x4E][0] = '_';
    non_ext_make_LUT[0x55][0] = '+';
    non_ext_make_LUT[0x0E][0] = '"';
    non_ext_make_LUT[0x61][0] = '|';
    non_ext_make_LUT[0x5B][0] = '{';
    non_ext_make_LUT[0x5D][0] = '}';
    non_ext_make_LUT[0x4A][0] = ':';
    non_ext_make_LUT[0x51][0] = '?';
    non_ext_make_LUT[0x41][0] = '<';
    non_ext_make_LUT[0x49][0] = '>';
    non_ext_make_LUT[0x52][0] = '^';
  }

  if(numLock == false)
  {
    //U ARROW                            D ARROW                                L ARROW                             R ARROW           
    non_ext_make_LUT[0x75][0] = 20001;    non_ext_make_LUT[0x72][0] = 20002;    non_ext_make_LUT[0x6b][0] = 20003;   non_ext_make_LUT[0x74][0] = 20004; 

    //INSERT                //END                   //PG DN
    non_ext_make_LUT[0x70][0] = 20005;    non_ext_make_LUT[0x69][0] = 20006;    non_ext_make_LUT[0x7a][0] = 20007;  non_ext_make_LUT[0x73][0] = '5';  
    
    //HOME                       //PG UP                 
    non_ext_make_LUT[0x6c][0] = 20008;    non_ext_make_LUT[0x7d][0] = 20009;     non_ext_make_LUT[0x7c][0] = '*';   non_ext_make_LUT[0x7b][0] = '-';      
    //                                                                                 //ENTER
    non_ext_make_LUT[0x79][0] = '+';    non_ext_make_LUT[0x6d][0] = '.';   non_ext_make_LUT[0x5a][0] = 13;

    if(ext_key == true)
    {
      non_ext_make_LUT[0x4a][0] = '/';
    }else if(shift == false)
    {
      non_ext_make_LUT[0x4a][0] = ';';
    }else if(shift == true)
    {
      non_ext_make_LUT[0x4a][0] = ':';
    }

    //DEL
    non_ext_make_LUT[0x71][0] = 20010;
  }else
  {
    non_ext_make_LUT[0x70][0] = '0';    non_ext_make_LUT[0x69][0] = '1';    non_ext_make_LUT[0x72][0] = '2';   non_ext_make_LUT[0x7a][0] = '3';
    non_ext_make_LUT[0x6b][0] = '4';    non_ext_make_LUT[0x73][0] = '5';    non_ext_make_LUT[0x74][0] = '6';   non_ext_make_LUT[0x6c][0] = '7';
    non_ext_make_LUT[0x75][0] = '8';    non_ext_make_LUT[0x7d][0] = '9';    non_ext_make_LUT[0x7c][0] = '*';   non_ext_make_LUT[0x7b][0] = '-';    
    non_ext_make_LUT[0x79][0] = '+';    non_ext_make_LUT[0x6d][0] = '.';    non_ext_make_LUT[0x5a][0] = 13;    non_ext_make_LUT[0x71][0] = ',';

    if(ext_key == true)
    {
      non_ext_make_LUT[0x4a][0] = '/';
    }else if(shift == false)
    {
      non_ext_make_LUT[0x4a][0] = ';';
    }else if(shift == true)
    {
      non_ext_make_LUT[0x4a][0] = ':';
    }
  }
  
}

static bool ps2_handle_system_shortcut(uint8_t scan_code,
                                       bool extended,
                                       bool released)
{
  // Both left Alt (11) and right Alt (E0 11) qualify as the system modifier.
  if (scan_code == 0x11u)
  {
    const uint8_t side = extended ? 2u : 1u;
    if (released) ps2_system_alt_mask &= (uint8_t)~side;
    else ps2_system_alt_mask |= side;
    return false;
  }

  // Set-2 F2/F3/F4 make codes. They are normally non-extended, but accepting
  // an extended prefix as well costs nothing and tolerates keyboard adapters
  // which incorrectly add E0 to function keys.
  uint8_t latch = 0u;
  if (scan_code == 0x06u) latch = PS2_SHORTCUT_F2;
  else if (scan_code == 0x04u) latch = PS2_SHORTCUT_F3;
  else if (scan_code == 0x0cu) latch = PS2_SHORTCUT_F4;
  else return false;

  if (released)
  {
    const bool consumed = (ps2_system_shortcut_latch & latch) != 0u;
    ps2_system_shortcut_latch &= (uint8_t)~latch;
    return consumed;
  }

  if (ps2_system_alt_mask == 0u) return false;
  if ((ps2_system_shortcut_latch & latch) != 0u) return true;
  ps2_system_shortcut_latch |= latch;

  const uint32_t index = latch == PS2_SHORTCUT_F2 ? 0u :
                          latch == PS2_SHORTCUT_F3 ? 1u : 2u;
  __atomic_fetch_add(&ps2_shortcut_presses[index], 1u, __ATOMIC_RELAXED);
  if (latch == PS2_SHORTCUT_F2) native_request_focus_next();
  else if (latch == PS2_SHORTCUT_F3) native_request_toggle_mode();
  else native_request_stop();
  return true;
}

static void ps2_publish_decoded_event()
{

  uint8_t keybyte;
  if(ext_key == true && releasing == true)
  { 
    keybyte = 2;
    is_printable = false;
  }else if(ext_key == true)
  {
    keybyte = 1;
    is_printable = false;
  }else if(releasing == true)
  {
    keybyte = 1;
  }else
  {
    keybyte = 0;
  }

  /*if(key_bytes[keybyte] == 0x12 && releasing == false)
  {
    shift = true;
  }else if(key_bytes[keybyte] == 0x12 && releasing == true)
  {
    shift = false;
  }

  if(key_bytes[keybyte] == 0x58 && releasing == false)
  {
    caps = !caps;
  }

  if(key_bytes[keybyte] == 0x77 && releasing == false)
  {
    numLock = !numLock;
  }*/

  const uint8_t scan_code = key_bytes[keybyte];
  __atomic_store_n(&ps2_last_transition,
                   (uint32_t)scan_code | (ext_key ? 0x100u : 0u) |
                   (releasing ? 0x200u : 0u), __ATOMIC_RELEASE);
  const KeyboardTransition transition = keyboard_update_ps2_state(scan_code,
                                                                   ext_key,
                                                                   releasing);
  const bool system_shortcut = ps2_handle_system_shortcut(scan_code,
                                                          ext_key,
                                                          releasing);
  const uint8_t modifiers = transition.modifiers;
  shift = (modifiers & KEYBOARD_MOD_SHIFT) != 0;
  caps = (modifiers & KEYBOARD_MOD_CAPS_LOCK) != 0;
  numLock = (modifiers & KEYBOARD_MOD_NUM_LOCK) != 0;
  ps2_update_legacy_translation_lut();

  
  if(key_bytes[keybyte] == 0x66 || key_bytes[keybyte] == 0x58 || key_bytes[keybyte] == 0x12 || key_bytes[keybyte] == 0x14 || key_bytes[keybyte] == 0x11 || key_bytes[keybyte] == 0x59 ||
     key_bytes[keybyte] == 0x5a || key_bytes[keybyte] == 0x76 || key_bytes[keybyte] == 0x7e || key_bytes[keybyte] == 0x77 || key_bytes[keybyte] == 0x05 || key_bytes[keybyte] == 0x06 ||
     key_bytes[keybyte] == 0x04 || key_bytes[keybyte] == 0x0c || key_bytes[keybyte] == 0x03 || key_bytes[keybyte] == 0x0b || key_bytes[keybyte] == 0x83 || key_bytes[keybyte] == 0x0a ||
     key_bytes[keybyte] == 0x01 || key_bytes[keybyte] == 0x09 || key_bytes[keybyte] == 0x78 || key_bytes[keybyte] == 0x07)
     {
      is_printable = false;
     }
  

  /*if(1)
  {

    if(releasing == true)
    {
      Serial.print("\nRELEASING");
    }else
    {
      Serial.print("\nPRESSING");
    }

    if(ext_key == true)
    {
      Serial.print(" SPECIAL");
    }

    Serial.print(" KEY ");

    Serial.print(key_bytes[keybyte], HEX);

    if(is_printable == true)
    {
      Serial.print(" or ");

      if(isAlpha(non_ext_make_LUT[key_bytes[keybyte]]))
      {
        if(shift == true || caps == true)
        {
          Serial.print((char)toupper(non_ext_make_LUT[key_bytes[keybyte]]));
        }else
        {
          Serial.print((char)non_ext_make_LUT[key_bytes[keybyte]]);
        }
      }else
      {
        Serial.print((char)non_ext_make_LUT[key_bytes[keybyte]]);
      }
    }
  }*/

  uint32_t character;
  if(isAlpha((char)non_ext_make_LUT[scan_code][0]))
  {
    if(shift != caps)
    {
      character = (uint8_t)toupper(non_ext_make_LUT[scan_code][0]);
      
    }else
    {
      character = non_ext_make_LUT[scan_code][0];
    }

  }else
  {
    character = non_ext_make_LUT[scan_code][0];
  }

  if (!system_shortcut)
  {
    keyboard_publish_event(transition, character, is_printable);
  }
  
  is_printable = true;
}

static uint8_t ps2_ignore_bytes = 0;

static void ps2_reset_make_break_state(bool clear_pressed_keys)
{
  key_byte = 0;
  key_byteCount = 0;
  make_break_size = 0;
  ext_key = false;
  releasing = false;
  is_printable = true;
  ps2_ignore_bytes = 0;
  for (uint8_t i = 0; i < sizeof(key_bytes); i++)
  {
    key_bytes[i] = 0;
  }

  if (clear_pressed_keys)
  {
    // A lost break byte must not leave Shift/Alt or a key latched.
    keyboard_release_all_keys();
    const uint8_t modifiers = keyboard_modifiers();
    shift = false;
    caps = (modifiers & KEYBOARD_MOD_CAPS_LOCK) != 0;
    numLock = (modifiers & KEYBOARD_MOD_NUM_LOCK) != 0;
    ps2_system_alt_mask = 0u;
    ps2_system_shortcut_latch = 0u;
  }
}

static void make_break_assembler(uint8_t scan_byte)
{
  key_byte = scan_byte;

  // Pause/Break is the only Set 2 sequence beginning with E1.  This firmware
  // does not expose it, but consuming the whole sequence prevents its bytes
  // from being mistaken for Ctrl, NumLock, and break prefixes.
  if (ps2_ignore_bytes != 0)
  {
    ps2_ignore_bytes--;
    key_byte = 0;
    return;
  }
  if (scan_byte == 0xE1)
  {
    ps2_reset_make_break_state(false);
    ps2_ignore_bytes = 7;
    return;
  }

  if(key_byteCount == 0)
  {
    if(scan_byte == 0xF0)
    {
      releasing = true;
      make_break_size = 2;
    }else if(scan_byte == 0xE0)
    {
      ext_key = true;
    }else
    {
      make_break_size = 1;
    }
  }

  if(ext_key == true && key_byteCount == 1)
  {
    if(scan_byte == 0xF0)
    {
      releasing = true;
      make_break_size = 3;
    }else
    {
      make_break_size = 2;
    }
  }

  if (key_byteCount >= sizeof(key_bytes))
  {
    ps2_reset_make_break_state(true);
    return;
  }
  key_bytes[key_byteCount] = scan_byte;

  key_byteCount++;

  if(key_byteCount == make_break_size)
  {
    ps2_publish_decoded_event();
    make_break_size = 0;
    key_byteCount = 0;

    ext_key = false;
    releasing = false;
  }

  key_byte = 0;
  event_packet = 0;
}

void ps2_keyboard_poll()
{
  // An invalid frame, timeout, or full receive queue means a prefix or break
  // byte may have been lost.  Flush queued bytes and release all latched keys
  // instead of allowing a permanent shifted stream or phantom keypresses.
  bool resync = false;
  noInterrupts();
  if (ps2_resync_requested)
  {
    ps2_resync_requested = 0;
    ps2_rx_tail = ps2_rx_head;
    event_packetReady = false;
    resync = true;
  }
  interrupts();
  if (resync)
  {
    __atomic_fetch_add(&ps2_recoveries, 1u, __ATOMIC_RELAXED);
    ps2_reset_make_break_state(true);
  }

  while (true)
  {
    uint8_t scan_byte;
    noInterrupts();
    const uint8_t tail = ps2_rx_tail;
    if (tail == ps2_rx_head)
    {
      event_packetReady = false;
      interrupts();
      break;
    }
    scan_byte = ps2_rx_queue[tail];
    ps2_rx_tail = (uint8_t)((tail + 1u) & PS2_RX_QUEUE_MASK);
    event_packetReady = (ps2_rx_tail != ps2_rx_head);
    interrupts();

    // Device responses and error bytes are not keyboard scan codes.
    if (scan_byte == 0x00 || scan_byte == 0xAA || scan_byte == 0xEE ||
        scan_byte == 0xFA || scan_byte == 0xFC || scan_byte == 0xFE ||
        scan_byte == 0xFF)
    {
      ps2_reset_make_break_state(true);
      continue;
    }

    // Parsing and event publication happen with interrupts enabled.  The old
    // implementation disabled them across this entire call, long enough to
    // lose PS/2 clock edges and permanently shift the receiver.
    make_break_assembler(scan_byte);
  }
}

void ps2_keyboard_get_diagnostics(Ps2KeyboardDiagnostics &diagnostics)
{
  diagnostics.received_bytes = __atomic_load_n(&ps2_received_bytes, __ATOMIC_ACQUIRE);
  diagnostics.recoveries = __atomic_load_n(&ps2_recoveries, __ATOMIC_ACQUIRE);
  diagnostics.last_transition = __atomic_load_n(&ps2_last_transition, __ATOMIC_ACQUIRE);
  for (uint32_t i = 0; i < 3u; ++i)
    diagnostics.shortcut_presses[i] =
        __atomic_load_n(&ps2_shortcut_presses[i], __ATOMIC_ACQUIRE);
}

