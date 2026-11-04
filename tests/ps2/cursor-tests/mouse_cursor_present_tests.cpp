// Compile the actual optimized presentation headers with host scanout buffers.
#define GLOBALS_H
#include <stdint.h>
#define VGA_WIDTH 640
#define VGA_HEIGHT 480
#define VGA_4BIT_BUFFER_SIZE (640 * 480 / 2)
struct var { uint32_t value; };
alignas(4) uint8_t vga_data_array[640 * 480];
alignas(4) uint8_t vga_640_480_4_01[VGA_4BIT_BUFFER_SIZE];
uint16_t vga_4bit_expanded_pairs[256];

#include "mouse_cursor_compositor.h"
#include "half_res_inline.h"
#include "vga4_inline.h"
#include <array>
#include <cassert>
#include <iostream>

MouseCursorCompositor cursor;
int depth = 0;
int polls = 0;
int bounds_queries = 0;
int row_queries = 0;
int pair_writes = 0;
void mouse_cursor_begin_present() { ++depth; }
void mouse_cursor_end_present() { assert(depth > 0); --depth; }
bool mouse_cursor_row_bounds(int y, int &left, int &right)
{ assert(depth > 0); ++row_queries; return cursor.row_bounds(y, left, right); }
bool mouse_cursor_bounds(int &left, int &top, int &right, int &bottom)
{ assert(depth > 0); ++bounds_queries; return cursor.bounds(left, top, right, bottom); }
void mouse_cursor_write_pair(int x, int y, uint16_t colors)
{
  assert(depth > 0);
  ++pair_writes;
  cursor.write_pixel(vga_data_array, x, y, (uint8_t)colors);
  cursor.write_pixel(vga_data_array, x + 1, y, (uint8_t)(colors >> 8));
}

namespace
{
void poll() { ++polls; }

void reset_counts() { bounds_queries = row_queries = pair_writes = polls = 0; }
void check_cost(bool visible)
{
  assert(bounds_queries == 1 && row_queries == 0);
  assert(pair_writes <= (visible ? 36 : 0));
}

void check_without_cursor(const std::array<uint8_t, 640 * 480> &expected)
{
  cursor.refresh(vga_data_array, 0, 0, false);
  for (uint32_t i = 0; i < expected.size(); ++i)
    assert(vga_data_array[i] == expected[i]);
  assert(depth == 0);
}

void check_asset(int x, int y, const std::array<uint8_t, 640 * 480> &background)
{
  const char *asset[9] = {"B.....", "BB....", "BWB...", "BWWB..", "BWWWB.",
                         "BWWWWB", "BWWBBB", "BBBBB.", "BB...."};
  for (int row = 0; row < 9 && y + row < 480; ++row)
    for (int col = 0; col < 6 && x + col < 640; ++col)
    {
      if(y + row < 0 || x + col < 0) continue;
      const int offset = (y + row) * 640 + x + col;
      const char pixel = asset[row][col];
      assert(vga_data_array[offset] == (pixel == 'B' ? 0 : pixel == 'W' ? 255 : background[offset]));
    }
}

void check_half_present()
{
  std::array<var, 320 * 240 / 4> framebuffer;
  uint8_t *source = (uint8_t*)framebuffer.data();
  for (int i = 0; i < 320 * 240; ++i) source[i] = (uint8_t)((i * 13 + i / 320) % 253 + 1);
  std::array<uint8_t, 640 * 480> expected;
  for (int y = 0; y < 480; ++y)
    for (int x = 0; x < 640; ++x)
      expected[y * 640 + x] = source[(y / 2) * 320 + x / 2];

  reset_counts();
  halfResPresentInline(framebuffer.data());
  check_cost(false);
  check_without_cursor(expected);
  for (const auto &p : {std::array<int,2>{0,0}, {1,1}, {638,478}, {639,479}, {317,239}, {-3,-4}, {0,479}, {639,0}, {640,480}})
  {
    cursor.refresh(vga_data_array, p[0], p[1], true);
    reset_counts();
    halfResPresentInline(framebuffer.data());
    check_cost(true);
    check_asset(p[0], p[1], expected);
    check_without_cursor(expected);
  }
}

void check_vga4_present()
{
  for (int value = 0; value < 256; ++value)
    vga_4bit_expanded_pairs[value] = (uint16_t)((value >> 4) * 17) |
                                   (uint16_t)(((value & 15) * 17) << 8);
  std::array<uint8_t, 640 * 480> expected;
  for (int pair = 0; pair < VGA_4BIT_BUFFER_SIZE; ++pair)
  {
    const uint8_t packed = (uint8_t)(pair * 7 + pair / 320);
    vga_640_480_4_01[pair] = packed;
    expected[pair * 2] = (packed >> 4) * 17;
    expected[pair * 2 + 1] = (packed & 15) * 17;
  }
  reset_counts();
  vga4PresentInline();
  check_cost(false);
  check_without_cursor(expected);
  for (const auto &p : {std::array<int,2>{0,0}, {1,1}, {638,478}, {639,479}, {317,239}, {-3,-4}, {0,479}, {639,0}, {640,480}})
  {
    for(bool usb : {false, true})
    {
      cursor.refresh(vga_data_array, p[0], p[1], true);
      reset_counts();
      vga4PresentInline(usb ? poll : nullptr);
      assert(polls == (usb ? 120 : 0)); // USB service frequency unchanged.
      check_cost(true);
      check_asset(p[0], p[1], expected);
      check_without_cursor(expected);
    }
  }
}
}

int main()
{
  check_half_present();
  check_vga4_present();
  std::cout << "Presentation tests passed: half-res/VGA4 output, clipping, restoration, USB service; one bounds query/frame, zero row queries, <=36 cursor pairs.\n";
}
