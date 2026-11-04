// Exercise the production renderer with a counter at the compositor boundary.
#include "cursor_host_globals.h"
#include "mouse_cursor_compositor.h"
#include "hardware/timer.h"
#include <cassert>
#include <iostream>

static unsigned refresh_calls;
class CountedCompositor : public MouseCursorCompositor
{
public:
  void refresh(uint8_t *screen, int x, int y, bool visible)
  {
    ++refresh_calls;
    MouseCursorCompositor::refresh(screen, x, y, visible);
  }
};
#define MouseCursorCompositor CountedCompositor
#include "mouse_cursor.cpp"
#undef MouseCursorCompositor

uint8_t vga_data_array[VGA_WIDTH * VGA_HEIGHT] = {};

int main()
{
  assert(mouse_cursor_init());
  mouse_cursor_set_target(100, 100, true);
  mouse_cursor_poll();
  assert(refresh_calls == 1);
  for(uint32_t tick = 1; tick <= 600; ++tick)
  {
    cursor_test_now.store(tick * 16667u);
    cursor_test_alarm_callback(0);
    mouse_cursor_begin_present();
    mouse_cursor_end_present();
  }
  assert(refresh_calls == 1); // Ten idle seconds, including 600 presentations.
  mouse_cursor_set_target(101, 100, true);
  mouse_cursor_poll();
  assert(refresh_calls == 2);
  mouse_cursor_set_target(102, 100, true);
  mouse_cursor_poll();
  assert(refresh_calls == 2); // New targets still respect the 60 Hz limit.
  cursor_test_now.fetch_add(16667u);
  mouse_cursor_begin_present();
  cursor_test_alarm_callback(0);
  assert(refresh_calls == 2); // Never alter scanout during presentation.
  mouse_cursor_end_present();
  assert(refresh_calls == 3);
  mouse_cursor_set_target(102, 100, false);
  cursor_test_now.fetch_add(16667u);
  mouse_cursor_poll();
  assert(refresh_calls == 4);
  cursor_test_now.fetch_add(16667u);
  mouse_cursor_poll();
  assert(refresh_calls == 4);
  std::cout << "Idle renderer tests passed: zero stationary redraws, changed targets only, 60 Hz cap, presentation deferral.\n";
}
