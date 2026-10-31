#include "graphics_runtime.h"

#include "half_res_inline.h"
#include "vga4_inline.h"

namespace
{
volatile uint32_t present_generation = 0u;
}

void graphics_note_present()
{
  __atomic_fetch_add(&present_generation, 1u, __ATOMIC_RELEASE);
}

uint32_t graphics_present_generation()
{
  return __atomic_load_n(&present_generation, __ATOMIC_ACQUIRE);
}

void graphics_half_present(var *framebuffer)
{
  if (framebuffer)
  {
    halfResPresentInline(framebuffer);
    graphics_note_present();
  }
}

void graphics_half_put_linear_pixel(var *framebuffer,
                                    uint32_t pixel_index,
                                    uint8_t color)
{
  if (framebuffer && pixel_index < 320u * 240u)
  {
    halfResPutLinearPixelInline(framebuffer, pixel_index, color);
  }
}

void graphics_half_draw_line(var *framebuffer,
                             int32_t x0,
                             int32_t y0,
                             int32_t x1,
                             int32_t y1,
                             uint8_t color)
{
  if (framebuffer)
  {
    halfResDrawLineInline(framebuffer, x0, y0, x1, y1, color);
  }
}

void graphics_half_clear(var *framebuffer, uint8_t color)
{
  if (!framebuffer)
  {
    return;
  }
  const uint32_t packed_color = (uint32_t)color * 0x01010101u;
  for (uint32_t i = 0; i < 19200u; ++i)
  {
    framebuffer[i].value = packed_color;
  }
}

void graphics_half_fill_rect(var *framebuffer,
                             int32_t x0,
                             int32_t y0,
                             int32_t x1,
                             int32_t y1,
                             uint8_t color)
{
  if (!framebuffer)
  {
    return;
  }
  if (x0 > x1) { int32_t t = x0; x0 = x1; x1 = t; }
  if (y0 > y1) { int32_t t = y0; y0 = y1; y1 = t; }
  if (x1 < 0 || y1 < 0 || x0 >= 320 || y0 >= 240)
  {
    return;
  }
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > 319) x1 = 319;
  if (y1 > 239) y1 = 239;

  for (int32_t y = y0; y <= y1; ++y)
  {
    halfResDrawHorizontalSpanInline(framebuffer, x0, x1, y, color);
  }
}

void graphics_half_fill_triangle(var *framebuffer,
                                 int32_t x0,
                                 int32_t y0,
                                 int32_t x1,
                                 int32_t y1,
                                 int32_t x2,
                                 int32_t y2,
                                 uint8_t color)
{
  if (!framebuffer)
  {
    return;
  }
  if (y0 > y1) { int32_t t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y1 > y2) { int32_t t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
  if (y0 > y1) { int32_t t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

  if (y0 == y2)
  {
    int32_t left = x0 < x1 ? x0 : x1;
    if (x2 < left) left = x2;
    int32_t right = x0 > x1 ? x0 : x1;
    if (x2 > right) right = x2;
    halfResDrawHorizontalSpanInline(framebuffer, left, right, y0, color);
    return;
  }

  const int64_t long_step = ((int64_t)(x2 - x0) << 16) / (y2 - y0);
  const int64_t top_step = y1 == y0 ? 0 : ((int64_t)(x1 - x0) << 16) / (y1 - y0);
  const int64_t bottom_step = y2 == y1 ? 0 : ((int64_t)(x2 - x1) << 16) / (y2 - y1);
  int64_t long_x = (int64_t)x0 << 16;
  int64_t short_x = long_x;

  for (int32_t y = y0; y < y1; ++y)
  {
    halfResDrawHorizontalSpanInline(framebuffer,
                                    (int32_t)(long_x >> 16),
                                    (int32_t)(short_x >> 16),
                                    y,
                                    color);
    long_x += long_step;
    short_x += top_step;
  }

  short_x = (int64_t)x1 << 16;
  for (int32_t y = y1; y <= y2; ++y)
  {
    halfResDrawHorizontalSpanInline(framebuffer,
                                    (int32_t)(long_x >> 16),
                                    (int32_t)(short_x >> 16),
                                    y,
                                    color);
    long_x += long_step;
    short_x += bottom_step;
  }
}

namespace
{
bool checked_bitmap_size(int32_t width,
                         int32_t height,
                         uint32_t pixels_per_word,
                         uint32_t available_words,
                         uint32_t &required_words)
{
  if (width <= 0 || height <= 0 || pixels_per_word == 0)
  {
    return false;
  }

  const uint64_t pixel_count = (uint64_t)(uint32_t)width * (uint32_t)height;
  const uint64_t words = (pixel_count + pixels_per_word - 1u) / pixels_per_word;
  if (words > UINT32_MAX || words > available_words)
  {
    return false;
  }

  required_words = (uint32_t)words;
  return true;
}

bool checked_texture_size(int32_t width,
                          int32_t height,
                          uint32_t available_words,
                          uint32_t &row_words)
{
  if (width <= 0 || height <= 0)
  {
    return false;
  }

  const uint64_t row_word_count = ((uint64_t)(uint32_t)width + 3u) / 4u;
  const uint64_t total_words = row_word_count * (uint32_t)height;
  if (row_word_count > UINT32_MAX || total_words > available_words)
  {
    return false;
  }

  row_words = (uint32_t)row_word_count;
  return true;
}

inline uint8_t packed_texture_pixel(const var *texture_words,
                                    uint32_t row_words,
                                    int32_t source_x,
                                    int32_t source_y)
{
  const uint32_t word = texture_words[(uint32_t)source_y * row_words +
                                      ((uint32_t)source_x >> 2)].value;
  return (uint8_t)(word >> (((uint32_t)source_x & 3u) << 3));
}

inline bool packed_mask_pixel(const var *mask_words, uint32_t pixel_index)
{
  return ((mask_words[pixel_index >> 5].value >> (31u - (pixel_index & 31u))) & 1u) != 0;
}

bool clip_bitmap(int32_t x,
                 int32_t y,
                 int32_t width,
                 int32_t height,
                 int32_t &source_x0,
                 int32_t &source_y0,
                 int32_t &source_x1,
                 int32_t &source_y1)
{
  source_x0 = x < 0 ? -x : 0;
  source_y0 = y < 0 ? -y : 0;
  source_x1 = width;
  source_y1 = height;

  if (x >= HALF_RES_WIDTH || y >= HALF_RES_HEIGHT)
  {
    return false;
  }
  if ((int64_t)x + source_x1 > HALF_RES_WIDTH)
  {
    source_x1 = HALF_RES_WIDTH - x;
  }
  if ((int64_t)y + source_y1 > HALF_RES_HEIGHT)
  {
    source_y1 = HALF_RES_HEIGHT - y;
  }

  return source_x0 < source_x1 && source_y0 < source_y1;
}
} // namespace

bool graphics_half_blit(var *framebuffer,
                        int32_t x,
                        int32_t y,
                        int32_t width,
                        int32_t height,
                        const var *texture_words,
                        uint32_t texture_word_count)
{
  uint32_t row_words = 0;
  if (!framebuffer || !texture_words ||
      !checked_texture_size(width, height, texture_word_count, row_words))
  {
    return false;
  }

  int32_t source_x0, source_y0, source_x1, source_y1;
  if (!clip_bitmap(x, y, width, height, source_x0, source_y0, source_x1, source_y1))
  {
    return true;
  }

  for (int32_t source_y = source_y0; source_y < source_y1; ++source_y)
  {
    for (int32_t source_x = source_x0; source_x < source_x1; ++source_x)
    {
      halfResPutPixelInline(framebuffer,
                            x + source_x,
                            y + source_y,
                            packed_texture_pixel(texture_words, row_words, source_x, source_y));
    }
  }
  return true;
}

bool graphics_half_blit_masked(var *framebuffer,
                               int32_t x,
                               int32_t y,
                               int32_t width,
                               int32_t height,
                               const var *texture_words,
                               uint32_t texture_word_count,
                               const var *mask_words,
                               uint32_t mask_word_count)
{
  uint32_t row_words = 0;
  uint32_t required_mask_words = 0;
  if (!framebuffer || !texture_words || !mask_words ||
      !checked_texture_size(width, height, texture_word_count, row_words) ||
      !checked_bitmap_size(width, height, 32u, mask_word_count, required_mask_words))
  {
    return false;
  }

  int32_t source_x0, source_y0, source_x1, source_y1;
  if (!clip_bitmap(x, y, width, height, source_x0, source_y0, source_x1, source_y1))
  {
    return true;
  }

  for (int32_t source_y = source_y0; source_y < source_y1; ++source_y)
  {
    for (int32_t source_x = source_x0; source_x < source_x1; ++source_x)
    {
      const uint32_t pixel_index = (uint32_t)source_y * (uint32_t)width + (uint32_t)source_x;
      if (packed_mask_pixel(mask_words, pixel_index))
      {
        halfResPutPixelInline(framebuffer,
                              x + source_x,
                              y + source_y,
                              packed_texture_pixel(texture_words, row_words, source_x, source_y));
      }
    }
  }
  return true;
}

bool graphics_half_glyph(var *framebuffer,
                         int32_t x,
                         int32_t y,
                         int32_t width,
                         int32_t height,
                         const var *glyph_words,
                         uint32_t glyph_word_count,
                         uint8_t color)
{
  uint32_t required_glyph_words = 0;
  if (!framebuffer || !glyph_words ||
      !checked_bitmap_size(width, height, 32u, glyph_word_count, required_glyph_words))
  {
    return false;
  }

  int32_t source_x0, source_y0, source_x1, source_y1;
  if (!clip_bitmap(x, y, width, height, source_x0, source_y0, source_x1, source_y1))
  {
    return true;
  }

  for (int32_t source_y = source_y0; source_y < source_y1; ++source_y)
  {
    for (int32_t source_x = source_x0; source_x < source_x1; ++source_x)
    {
      const uint32_t pixel_index = (uint32_t)source_y * (uint32_t)width + (uint32_t)source_x;
      if (packed_mask_pixel(glyph_words, pixel_index))
      {
        halfResPutPixelInline(framebuffer, x + source_x, y + source_y, color);
      }
    }
  }
  return true;
}

void graphics_vga4_present()
{
  vga4PresentInline();
  graphics_note_present();
}

void graphics_vga4_clear(uint8_t color_index)
{
  vga4ClearInline(color_index);
}

void graphics_vga4_pixel(int32_t x, int32_t y, uint8_t color_index)
{
  vga4PutPixelInline(x, y, color_index);
}

void graphics_vga4_fill_rect(int32_t x0,
                             int32_t y0,
                             int32_t x1,
                             int32_t y1,
                             uint8_t color_index)
{
  vga4FillRectInline(x0, y0, x1, y1, color_index);
}

void graphics_vga4_line(int32_t x0,
                        int32_t y0,
                        int32_t x1,
                        int32_t y1,
                        uint8_t color_index)
{
  vga4DrawLineInline(x0, y0, x1, y1, color_index);
}

void graphics_vga4_glyph(int32_t x,
                         int32_t y,
                         int32_t width,
                         int32_t height,
                         const var *glyph_words,
                         uint8_t color_index)
{
  vga4DrawGlyphInline(x, y, width, height, glyph_words, color_index);
}
