#ifndef VGA4_INLINE_H
#define VGA4_INLINE_H

#include "globals.h"

static inline __attribute__((always_inline)) void vga4PutPixelUncheckedInline(int32_t pixelX, int32_t pixelY, uint8_t colorIndex)
{
  const uint32_t pixelAddress = ((uint32_t)pixelY * VGA_WIDTH) + pixelX;
  uint8_t& packedPixels = vga_640_480_4_01[pixelAddress >> 1];

  colorIndex &= 0x0F;
  if(pixelAddress & 1)
  {
    packedPixels = (packedPixels & 0xF0) | colorIndex;
  }else
  {
    packedPixels = (packedPixels & 0x0F) | (colorIndex << 4);
  }
}

static inline __attribute__((always_inline)) void vga4PutPixelInline(int32_t pixelX, int32_t pixelY, uint8_t colorIndex)
{
  if(pixelX >= 0 && pixelX < VGA_WIDTH && pixelY >= 0 && pixelY < VGA_HEIGHT)
  {
    vga4PutPixelUncheckedInline(pixelX, pixelY, colorIndex);
  }
}

static inline __attribute__((always_inline)) void vga4ClearInline(uint8_t colorIndex)
{
  colorIndex &= 0x0F;
  const uint32_t packedColor = ((uint32_t)((colorIndex << 4) | colorIndex)) * 0x01010101UL;
  uint32_t* destination = (uint32_t*)vga_640_480_4_01;

  for(uint32_t word = 0; word < VGA_4BIT_BUFFER_SIZE / sizeof(uint32_t); word++)
  {
    destination[word] = packedColor;
  }
}

static inline __attribute__((always_inline)) void vga4FillPackedBytesInline(uint8_t* destination, int byteCount, uint8_t packedColor)
{
  while(byteCount > 0 && ((uintptr_t)destination & 3))
  {
    *destination++ = packedColor;
    byteCount--;
  }

  const uint32_t packedWord = ((uint32_t)packedColor) * 0x01010101UL;
  uint32_t* wordDestination = (uint32_t*)destination;
  while(byteCount >= 4)
  {
    *wordDestination++ = packedWord;
    byteCount -= 4;
  }

  destination = (uint8_t*)wordDestination;
  while(byteCount-- > 0)
  {
    *destination++ = packedColor;
  }
}

static inline __attribute__((always_inline)) void vga4FillRectInline(int x0, int y0, int x1, int y1, uint8_t colorIndex)
{
  if(x0 > x1)
  {
    const int swap = x0;
    x0 = x1;
    x1 = swap;
  }

  if(y0 > y1)
  {
    const int swap = y0;
    y0 = y1;
    y1 = swap;
  }

  if(x1 < 0 || x0 >= VGA_WIDTH || y1 < 0 || y0 >= VGA_HEIGHT)
  {
    return;
  }

  if(x0 < 0) x0 = 0;
  if(y0 < 0) y0 = 0;
  if(x1 >= VGA_WIDTH) x1 = VGA_WIDTH - 1;
  if(y1 >= VGA_HEIGHT) y1 = VGA_HEIGHT - 1;

  colorIndex &= 0x0F;
  const uint8_t packedColor = (colorIndex << 4) | colorIndex;

  for(int y = y0; y <= y1; y++)
  {
    int x = x0;

    if(x & 1)
    {
      vga4PutPixelUncheckedInline(x++, y, colorIndex);
    }

    const int packedByteCount = (x1 - x + 1) >> 1;
    if(packedByteCount > 0)
    {
      vga4FillPackedBytesInline(vga_640_480_4_01 + (((uint32_t)y * VGA_WIDTH + x) >> 1), packedByteCount, packedColor);
      x += packedByteCount << 1;
    }

    if(x <= x1)
    {
      vga4PutPixelUncheckedInline(x, y, colorIndex);
    }
  }
}

static inline __attribute__((always_inline)) void vga4DrawLineInline(int x0, int y0, int x1, int y1, uint8_t colorIndex)
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
    vga4PutPixelInline(x0, y0, colorIndex);

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

static inline __attribute__((always_inline)) void vga4DrawGlyphInline(int x0, int y0, int glyphWidth, int glyphHeight, const var* glyphWords, uint8_t colorIndex)
{
  if(glyphWidth <= 0 || glyphHeight <= 0 || glyphWords == nullptr)
  {
    return;
  }

  const int drawX0 = x0 < 0 ? 0 : x0;
  const int drawY0 = y0 < 0 ? 0 : y0;
  const int64_t glyphX1 = (int64_t)x0 + glyphWidth;
  const int64_t glyphY1 = (int64_t)y0 + glyphHeight;
  const int drawX1 = glyphX1 > VGA_WIDTH ? VGA_WIDTH : (int)glyphX1;
  const int drawY1 = glyphY1 > VGA_HEIGHT ? VGA_HEIGHT : (int)glyphY1;

  for(int y = drawY0; y < drawY1; y++)
  {
    const uint32_t glyphRowBitBase = (uint32_t)(y - y0) * glyphWidth;

    for(int x = drawX0; x < drawX1; x++)
    {
      const uint32_t glyphBitIndex = glyphRowBitBase + (x - x0);
      const uint32_t glyphWord = glyphWords[glyphBitIndex >> 5].value;

      if((glyphWord >> (31 - (glyphBitIndex & 31))) & 1)
      {
        vga4PutPixelUncheckedInline(x, y, colorIndex);
      }
    }
  }
}

static inline __attribute__((always_inline)) void vga4PresentInline()
{
  uint16_t* destination = (uint16_t*)vga_data_array;
  for(uint32_t pair = 0; pair < VGA_4BIT_BUFFER_SIZE; pair++)
  {
    destination[pair] = vga_4bit_expanded_pairs[vga_640_480_4_01[pair]];
  }
}

#endif
