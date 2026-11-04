#include "mouse_cursor_compositor.h"

#include <array>
#include <cassert>
#include <iostream>
#include <limits>

namespace
{
constexpr int kPixels = 640 * 480;
using Screen = std::array<uint8_t, kPixels>;

Screen scene(uint8_t salt)
{
  Screen result;
  for (int i = 0; i < kPixels; ++i) result[i] = (uint8_t)((i * 17 + i / 640 + salt) % 251 + 1);
  return result;
}

void expect_asset(const Screen &screen, int x, int y, const Screen &background)
{
  const char *asset[9] = {"B.....", "BB....", "BWB...", "BWWB..", "BWWWB.",
                         "BWWWWB", "BWWBBB", "BBBBB.", "BB...."};
  for (int row = 0; row < 9; ++row)
    for (int col = 0; col < 6; ++col)
      if (x + col >= 0 && x + col < 640 && y + row >= 0 && y + row < 480)
      {
        const int offset = (y + row) * 640 + x + col;
        const char pixel = asset[row][col];
        assert(screen[offset] == (pixel == 'B' ? 0 : pixel == 'W' ? 255 : background[offset]));
      }
}

void moving_and_clipping()
{
  const Screen original = scene(3);
  Screen scanout = original;
  MouseCursorCompositor cursor;
  const int positions[][2] = {{0,0},{1,1},{3,4},{638,478},{639,479},{-3,-4},{640,480},{320,240}};
  for (const auto &p : positions)
  {
    cursor.refresh(scanout.data(), p[0], p[1], true);
    expect_asset(scanout, p[0], p[1], original);
  }
  cursor.refresh(scanout.data(), 0, 0, false);
  assert(scanout == original);

  // Guards around the scanout catch clipping writes beyond either edge.
  std::array<uint8_t, kPixels + 128> guarded;
  guarded.fill(0x6d);
  MouseCursorCompositor clipped;
  clipped.refresh(guarded.data() + 64, -5, -8, true);
  clipped.refresh(guarded.data() + 64, 639, 479, true);
  clipped.refresh(guarded.data() + 64, 0, 0, false);
  for (uint8_t pixel : guarded) assert(pixel == 0x6d);
}

void present_and_overlay()
{
  Screen scanout = scene(1);
  const Screen replacement = scene(90);
  MouseCursorCompositor cursor;
  cursor.refresh(scanout.data(), 101, 15, true);
  for (int y = 0; y < 480; ++y)
    cursor.write_row(scanout.data(), y, 0, replacement.data() + y * 640, 640);
  expect_asset(scanout, 101, 15, replacement);

  // Overlay capture must see the scene, with no cursor rectangle baked in.
  std::array<uint8_t, 38 * 640> overlay_backing;
  for (int y = 0; y < 38; ++y)
    cursor.read_row(scanout.data(), y, 0, overlay_backing.data() + y * 640, 640);
  for (int i = 0; i < (int)overlay_backing.size(); ++i)
    assert(overlay_backing[i] == replacement[i]);

  std::array<uint8_t, 640> overlay_row;
  overlay_row.fill(66);
  for (int y = 0; y < 38; ++y)
    cursor.write_row(scanout.data(), y, 0, overlay_row.data(), 640);
  Screen overlay = replacement;
  for(int i = 0; i < 38 * 640; ++i) overlay[i] = 66;
  expect_asset(scanout, 101, 15, overlay);
  cursor.refresh(scanout.data(), 103, 17, true); // overlapping move within overlay
  for (int y = 0; y < 38; ++y)
    cursor.write_row(scanout.data(), y, 0, overlay_backing.data() + y * 640, 640);
  cursor.refresh(scanout.data(), 0, 0, false);
  assert(scanout == replacement);
}

void packed_pair_presentation()
{
  Screen scanout = scene(11);
  Screen replacement = scene(17);
  MouseCursorCompositor cursor;
  for (int x : {0,1,317,638,639})
  {
    cursor.refresh(scanout.data(), x, 479, true);
    for (int col = 0; col < 640; col += 2)
    {
      cursor.write_pixel(scanout.data(), col, 479, replacement[479 * 640 + col]);
      cursor.write_pixel(scanout.data(), col + 1, 479, replacement[479 * 640 + col + 1]);
    }
    expect_asset(scanout, x, 479, replacement);
    cursor.refresh(scanout.data(), 0, 0, false);
    for (int col = 0; col < 640; ++col)
      assert(scanout[479 * 640 + col] == replacement[479 * 640 + col]);
  }
}

void cadence_and_wrap()
{
  MouseCursorCadence cadence;
  int updates = 0;
  for (uint32_t now = 0; now < 1000000u; ++now)
    if (cadence.take(now)) ++updates;
  assert(updates == 60);
  assert(!cadence.take(1000000u));
  assert(cadence.take(2000000u)); // delayed update never replays missed ticks
  assert(!cadence.take(2000001u));

  MouseCursorCadence wrapping;
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 5000u;
  assert(wrapping.take(start)); // first use is also valid after 35 minutes
  assert(!wrapping.take(start + 16666u));
  assert(wrapping.take(start + 16667u));
  assert(!wrapping.take(start + 16668u));
}

void contrasting_backgrounds()
{
  for(uint8_t color : {0, 255})
  {
    Screen original; original.fill(color);
    Screen scanout = original;
    MouseCursorCompositor cursor;
    cursor.refresh(scanout.data(), 100, 100, true);
    // No buffer access is allowed when visibility and position are unchanged.
    cursor.refresh(nullptr, 100, 100, true);
    expect_asset(scanout, 100, 100, original);
    // The enclosed white fill remains visible on black, the outline on white.
    assert(scanout[102 * 640 + 101] == 255);
    assert(scanout[102 * 640 + 102] == 0);
    assert(scanout[100 * 640 + 105] == color); // Exterior white is transparent.
    cursor.refresh(scanout.data(), 0, 0, false);
    cursor.refresh(nullptr, 200, 200, false);
    assert(scanout == original);
  }
}
}

int main()
{
  moving_and_clipping();
  present_and_overlay();
  packed_pair_presentation();
  cadence_and_wrap();
  contrasting_backgrounds();
  std::cout << "Cursor tests passed: exact asset, clipping, restoration, presentation, overlay, cadence, wrap.\n";
}
