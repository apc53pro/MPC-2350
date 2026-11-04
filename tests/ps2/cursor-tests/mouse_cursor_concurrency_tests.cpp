#include "cursor_host_globals.h"
#include "hardware/timer.h"
#include "mouse_cursor.h"

#include <array>
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>

uint8_t vga_data_array[VGA_WIDTH * VGA_HEIGHT] = {};

int main()
{
  assert(mouse_cursor_init());
  assert(mouse_cursor_init()); // idempotent initialization
  assert(cursor_test_alarm_callback != nullptr);
  std::atomic<bool> running{true};
  std::thread timer([&]() {
    uint32_t report = 0;
    while (running.load(std::memory_order_relaxed))
    {
      mouse_cursor_set_target((report * 17) % VGA_WIDTH,
                              (report * 13) % VGA_HEIGHT, true);
      cursor_test_now.fetch_add(16667u, std::memory_order_relaxed);
      cursor_test_alarm_callback(0);
      ++report;
      std::this_thread::yield();
    }
  });

  std::array<uint8_t, 640> row;
  std::array<uint8_t, 640> captured;
  uint8_t last = 0;
  for (uint32_t frame = 0; frame < 100; ++frame)
  {
    last = (uint8_t)(frame + 1u);
    row.fill(last);
    mouse_cursor_begin_present();
    // Nested scene operations must not release the cursor early.
    mouse_cursor_begin_present();
    for (int y = 0; y < VGA_HEIGHT; ++y)
      mouse_cursor_write_row(y, 0, row.data(), row.size());
    mouse_cursor_end_present();
    for (int y = 0; y < VGA_HEIGHT; ++y)
    {
      mouse_cursor_read_row(y, 0, captured.data(), captured.size());
      assert(captured == row); // cursor-free snapshot under concurrent timer requests
    }
    mouse_cursor_end_present();
  }
  running.store(false, std::memory_order_relaxed);
  timer.join();
  mouse_cursor_set_target(0, 0, false);
  cursor_test_now.fetch_add(16667u, std::memory_order_relaxed);
  mouse_cursor_poll();
  for (uint8_t pixel : vga_data_array) assert(pixel == last);
  std::cout << "Concurrency tests passed: actual renderer, timer/presenter handshake, nested presents, cursor-free capture, no trails.\n";
}
