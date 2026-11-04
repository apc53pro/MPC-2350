#ifndef MOUSE_CURSOR_COMPOSITOR_H
#define MOUSE_CURSOR_COMPOSITOR_H

#include <stdint.h>
#include <string.h>

#if defined(__GNUC__)
#define CURSOR_INLINE inline __attribute__((always_inline))
#else
#define CURSOR_INLINE inline
#endif

// Pure scanout compositor, also exercised by the host tests. Its 54 saved
// pixels are the scene beneath the cursor, never an application framebuffer.
class MouseCursorCompositor
{
public:
  static constexpr int kWidth = 6;
  static constexpr int kHeight = 9;
  static constexpr int kScreenWidth = 640;
  static constexpr int kScreenHeight = 480;

  CURSOR_INLINE bool bounds(int &left, int &top, int &right, int &bottom) const
  {
    if(!shown_) return false;
    left = x_ < 0 ? 0 : x_;
    top = y_ < 0 ? 0 : y_;
    right = x_ + kWidth > kScreenWidth ? kScreenWidth : x_ + kWidth;
    bottom = y_ + kHeight > kScreenHeight ? kScreenHeight : y_ + kHeight;
    return left < right && top < bottom;
  }

  CURSOR_INLINE bool row_bounds(int y, int &left, int &right) const
  {
    if (!shown_ || y < y_ || y >= y_ + kHeight || y < 0 || y >= kScreenHeight)
      return false;
    left = x_ < 0 ? 0 : x_;
    right = x_ + kWidth > kScreenWidth ? kScreenWidth : x_ + kWidth;
    return left < right;
  }

  CURSOR_INLINE void refresh(uint8_t *screen, int x, int y, bool visible)
  {
    if(shown_ == visible && (!visible || (x_ == x && y_ == y))) return;
    if (shown_)
    {
      for (int row = 0; row < kHeight; ++row)
        for (int col = 0; col < kWidth; ++col)
          if (opaque(row, col) && in_screen(x_ + col, y_ + row))
            screen[(y_ + row) * kScreenWidth + x_ + col] = backing_[row * kWidth + col];
    }
    x_ = x;
    y_ = y;
    shown_ = visible;
    if (!shown_) return;
    for (int row = 0; row < kHeight; ++row)
      for (int col = 0; col < kWidth; ++col)
        if (opaque(row, col) && in_screen(x_ + col, y_ + row))
        {
          const int index = row * kWidth + col;
          uint8_t &pixel = screen[(y_ + row) * kScreenWidth + x_ + col];
          backing_[index] = pixel;
          pixel = bitmap_[index];
        }
  }

  CURSOR_INLINE void write_pixel(uint8_t *screen, int x, int y, uint8_t color)
  {
    if (!in_screen(x, y)) return;
    if (shown_ && x >= x_ && x < x_ + kWidth && y >= y_ && y < y_ + kHeight &&
        opaque(y - y_, x - x_))
      backing_[(y - y_) * kWidth + x - x_] = color;
    else
      screen[y * kScreenWidth + x] = color;
  }

  CURSOR_INLINE void write_row(uint8_t *screen, int y, int x,
                               const uint8_t *source, uint32_t count)
  {
    if (y < 0 || y >= kScreenHeight || x < 0 || x >= kScreenWidth || !count) return;
    if (count > (uint32_t)(kScreenWidth - x)) count = kScreenWidth - x;
    int left, right;
    if (!row_bounds(y, left, right) || right <= x || left >= x + (int)count)
    {
      memcpy(screen + y * kScreenWidth + x, source, count);
      return;
    }
    if (left < x) left = x;
    if (right > x + (int)count) right = x + (int)count;
    const uint32_t prefix = (uint32_t)(left - x);
    memcpy(screen + y * kScreenWidth + x, source, prefix);
    for (int col = left; col < right; ++col)
      write_pixel(screen, col, y, source[col - x]);
    memcpy(screen + y * kScreenWidth + right, source + right - x,
           count - (uint32_t)(right - x));
  }

  CURSOR_INLINE void read_row(const uint8_t *screen, int y, int x,
                              uint8_t *destination, uint32_t count) const
  {
    if (y < 0 || y >= kScreenHeight || x < 0 || x >= kScreenWidth || !count) return;
    if (count > (uint32_t)(kScreenWidth - x)) count = kScreenWidth - x;
    memcpy(destination, screen + y * kScreenWidth + x, count);
    int left, right;
    if (!row_bounds(y, left, right)) return;
    if (left < x) left = x;
    if (right > x + (int)count) right = x + (int)count;
    for (int col = left; col < right; ++col)
      if(opaque(y - y_, col - x_))
        destination[col - x] = backing_[(y - y_) * kWidth + col - x_];
  }

private:
  CURSOR_INLINE static bool in_screen(int x, int y)
  {
    return x >= 0 && x < kScreenWidth && y >= 0 && y < kScreenHeight;
  }

  CURSOR_INLINE bool opaque(int row, int col) const
  { return (mask_[row] & (1u << col)) != 0; }

  // Updated MPC-2350_mouse.png: preserve the black outline and its enclosed
  // white fill. Only 4-connected white pixels reachable from the image edge
  // are transparent; diagonal black edges must not leak into the white fill.
  // Mutable member storage keeps bitmap/mask reads in SRAM in the timer IRQ.
  uint8_t mask_[kHeight] = {1, 3, 7, 15, 31, 63, 63, 31, 3};
  uint8_t bitmap_[kWidth * kHeight] = {
      0,255,255,255,255,255,
      0,  0,255,255,255,255,
      0,255,  0,255,255,255,
      0,255,255,  0,255,255,
      0,255,255,255,  0,255,
      0,255,255,255,255,  0,
      0,255,255,  0,  0,  0,
      0,  0,  0,  0,  0,255,
      0,  0,255,255,255,255};
  uint8_t backing_[kWidth * kHeight] = {};
  int x_ = 0;
  int y_ = 0;
  bool shown_ = false;
};

struct MouseCursorCadence
{
  static constexpr uint32_t kPeriodUs = 16667u;
  uint32_t next_due = 0u;
  bool primed = false;

  CURSOR_INLINE bool take(uint32_t now)
  {
    if (primed && (int32_t)(now - next_due) < 0) return false;
    // Schedule from the actual update, so delayed work never causes a burst.
    next_due = now + kPeriodUs;
    primed = true;
    return true;
  }
};

#undef CURSOR_INLINE
#endif
