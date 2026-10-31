#ifndef HALF_RES_INLINE_H
#define HALF_RES_INLINE_H

#include "globals.h"

#define HALF_RES_WIDTH 320
#define HALF_RES_HEIGHT 240
#define HALF_RES_PIXEL_COUNT (HALF_RES_WIDTH * HALF_RES_HEIGHT)

static inline __attribute__((always_inline)) void halfResPutLinearPixelUncheckedInline(var* buffer, uint32_t pixelAddress, uint8_t color)
{
  const uint32_t slotIndex = pixelAddress >> 2;
  const uint32_t shift = (pixelAddress & 3) << 3;
  const uint32_t mask = 0xFFUL << shift;
  buffer[slotIndex].value = (buffer[slotIndex].value & ~mask) | ((uint32_t)color << shift);
}

static inline __attribute__((always_inline)) void halfResPutLinearPixelInline(var* buffer, uint32_t pixelAddress, uint8_t color)
{
  if(pixelAddress < HALF_RES_PIXEL_COUNT)
  {
    halfResPutLinearPixelUncheckedInline(buffer, pixelAddress, color);
  }
}

static inline __attribute__((always_inline)) void halfResPutPixelInline(var* buffer, int x, int y, uint8_t color)
{
  if(x >= 0 && x < HALF_RES_WIDTH && y >= 0 && y < HALF_RES_HEIGHT)
  {
    halfResPutLinearPixelUncheckedInline(buffer, ((uint32_t)y * HALF_RES_WIDTH) + x, color);
  }
}

static inline __attribute__((always_inline)) void halfResDrawHorizontalSpanInline(var* buffer, int x0, int x1, int y, uint8_t color)
{
  if(x0 > x1)
  {
    const int swap = x0;
    x0 = x1;
    x1 = swap;
  }

  if(y < 0 || y >= HALF_RES_HEIGHT || x1 < 0 || x0 >= HALF_RES_WIDTH)
  {
    return;
  }

  if(x0 < 0) x0 = 0;
  if(x1 >= HALF_RES_WIDTH) x1 = HALF_RES_WIDTH - 1;

  uint32_t pixelAddress = ((uint32_t)y * HALF_RES_WIDTH) + x0;
  while(x0 <= x1 && (pixelAddress & 3))
  {
    halfResPutLinearPixelUncheckedInline(buffer, pixelAddress++, color);
    x0++;
  }

  const uint32_t packedColor = ((uint32_t)color) * 0x01010101UL;
  while(x0 + 3 <= x1)
  {
    buffer[pixelAddress >> 2].value = packedColor;
    pixelAddress += 4;
    x0 += 4;
  }

  while(x0++ <= x1)
  {
    halfResPutLinearPixelUncheckedInline(buffer, pixelAddress++, color);
  }
}

static inline __attribute__((always_inline)) void halfResDrawLineInline(var* buffer, int x0, int y0, int x1, int y1, uint8_t color)
{
  const int deltaX = x1 - x0;
  const int deltaY = y1 - y0;
  const int dx = deltaX < 0 ? -deltaX : deltaX;
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = deltaY < 0 ? deltaY : -deltaY;
  const int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;

  while(true)
  {
    halfResPutPixelInline(buffer, x0, y0, color);
    if(x0 == x1 && y0 == y1)
    {
      break;
    }

    const int doubledError = error << 1;
    if(doubledError >= dy)
    {
      error += dy;
      x0 += sx;
    }
    if(doubledError <= dx)
    {
      error += dx;
      y0 += sy;
    }
  }
}

static inline __attribute__((always_inline)) void halfResPresentInline(const var* buffer)
{
  const uint8_t* source = (const uint8_t*)&buffer[0].value;

  for(uint16_t y = 0; y < HALF_RES_HEIGHT; y++)
  {
    uint16_t* topRow = (uint16_t*)(vga_data_array + ((uint32_t)y * 2 * VGA_WIDTH));
    uint16_t* bottomRow = topRow + (VGA_WIDTH >> 1);

    for(uint16_t x = 0; x < HALF_RES_WIDTH; x++)
    {
      const uint8_t color = *source++;
      const uint16_t doubledPixel = color | ((uint16_t)color << 8);
      topRow[x] = doubledPixel;
      bottomRow[x] = doubledPixel;
    }
  }
}

#endif
