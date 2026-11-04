#include "mouse_cursor.h"

#include "mouse_cursor_compositor.h"
#include "globals.h"
#include "hardware/timer.h"
#include "pico/platform.h"

namespace
{
MouseCursorCompositor compositor;
MouseCursorCadence cadence;
volatile uint32_t target_word = 0u;
volatile uint32_t applied_word = 0xffffffffu;
volatile uint32_t present_depth = 0u;
volatile uint32_t drawing = 0u;
volatile uint32_t initialized = 0u;

void __not_in_flash_func(service_cursor)()
{
  if (!__atomic_load_n(&initialized, __ATOMIC_ACQUIRE)) return;
  const uint32_t target = __atomic_load_n(&target_word, __ATOMIC_ACQUIRE);
  // A stationary cursor is already preserved by presentation. Do not acquire
  // the drawing handshake or touch scanout again just because a timer fired.
  if(target == __atomic_load_n(&applied_word, __ATOMIC_ACQUIRE)) return;
  uint32_t expected = 0u;
  // IRQ paths must never spin on code that they may have interrupted.
  if (!__atomic_compare_exchange_n(&drawing, &expected, 1u, false,
                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return;
  const uint32_t latest = __atomic_load_n(&target_word, __ATOMIC_ACQUIRE);
  if (latest != __atomic_load_n(&applied_word, __ATOMIC_ACQUIRE) &&
      !__atomic_load_n(&present_depth, __ATOMIC_SEQ_CST) && cadence.take(time_us_32()))
  {
    compositor.refresh(vga_data_array, (int)(latest & 1023u),
                       (int)((latest >> 10) & 511u), (latest & (1u << 19)) != 0u);
    __atomic_store_n(&applied_word, latest, __ATOMIC_RELEASE);
  }
  __atomic_store_n(&drawing, 0u, __ATOMIC_SEQ_CST);
}

void __not_in_flash_func(cursor_alarm_callback)(uint alarm)
{
  service_cursor();
  // Fixed maximum cadence. An end-present refresh uses the same deadline,
  // so it cannot add an extra redraw between timer updates.
  hardware_alarm_set_target(alarm, delayed_by_us(get_absolute_time(), MouseCursorCadence::kPeriodUs));
}
}

bool mouse_cursor_init()
{
  if (__atomic_load_n(&initialized, __ATOMIC_ACQUIRE)) return true;
  if (get_core_num() != 1u) return false;
  const int alarm = hardware_alarm_claim_unused(false);
  if (alarm < 0) return false;
  hardware_alarm_set_callback((uint)alarm, cursor_alarm_callback);
  __atomic_store_n(&initialized, 1u, __ATOMIC_RELEASE);
  hardware_alarm_set_target((uint)alarm,
                           delayed_by_us(get_absolute_time(), MouseCursorCadence::kPeriodUs));
  return true;
}

void mouse_cursor_set_target(int32_t x, int32_t y, bool visible)
{
  if (x < 0) x = 0;
  if (x >= VGA_WIDTH) x = VGA_WIDTH - 1;
  if (y < 0) y = 0;
  if (y >= VGA_HEIGHT) y = VGA_HEIGHT - 1;
  const uint32_t packed = (uint32_t)x | ((uint32_t)y << 10) |
                          (visible ? (1u << 19) : 0u);
  __atomic_store_n(&target_word, packed, __ATOMIC_RELEASE);
}

void mouse_cursor_poll()
{
  service_cursor();
}

void mouse_cursor_begin_present()
{
  // The sequential handshake guarantees either the refresh sees busy or the
  // presenter waits for its <=54-pixel restore/capture/blit to finish.
  __atomic_fetch_add(&present_depth, 1u, __ATOMIC_SEQ_CST);
  while (__atomic_load_n(&drawing, __ATOMIC_SEQ_CST)) tight_loop_contents();
}

void mouse_cursor_end_present()
{
  __atomic_fetch_sub(&present_depth, 1u, __ATOMIC_SEQ_CST);
  service_cursor();
}

bool mouse_cursor_row_bounds(int y, int &left, int &right)
{
  return compositor.row_bounds(y, left, right);
}

bool mouse_cursor_bounds(int &left, int &top, int &right, int &bottom)
{
  return compositor.bounds(left, top, right, bottom);
}

void mouse_cursor_write_pair(int x, int y, uint16_t colors)
{
  compositor.write_pixel(vga_data_array, x, y, (uint8_t)colors);
  compositor.write_pixel(vga_data_array, x + 1, y, (uint8_t)(colors >> 8));
}

void mouse_cursor_write_row(int y, int x, const uint8_t *source, uint32_t count)
{
  compositor.write_row(vga_data_array, y, x, source, count);
}

void mouse_cursor_read_row(int y, int x, uint8_t *destination, uint32_t count)
{
  compositor.read_row(vga_data_array, y, x, destination, count);
}
