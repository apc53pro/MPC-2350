#include "draw_funcs.h"
#include "globals.h"
#include "mms.h"
#include "graphics_runtime.h"
#include "vga4_inline.h"
#include "usb_storage_mode.h"

bool graphics_mode;

const uint8_t vga_4bit_color[16] = {255, 0, 0x02, 0x10, 0xE0, 0x80, 0x61, 0xEC, 0xFC, 0x1C, 0x09, 0x9F, 0x07, 0xE3, 0x49, 0x92};
uint16_t vga_4bit_expanded_pairs[256];

namespace
{
uint8_t palette_index_for_rgb332[256];
bool palette_lookup_initialized = false;

static inline uint8_t expand3To8(uint8_t value)
{
  return (value << 5) | (value << 2) | (value >> 1);
}

static inline uint8_t expand2To8(uint8_t value)
{
  return (value << 6) | (value << 4) | (value << 2) | value;
}

void initializePaletteLookup()
{
  if(palette_lookup_initialized)
  {
    return;
  }

  for(int color = 0; color < 256; color++)
  {
    const int red = expand3To8(color >> 5);
    const int green = expand3To8((color >> 2) & 0x07);
    const int blue = expand2To8(color & 0x03);
    uint32_t best_distance = 0xFFFFFFFF;
    uint8_t best_index = 0;

    for(uint8_t index = 0; index < 16; index++)
    {
      const uint8_t palette_color = vga_4bit_color[index];
      const int delta_red = red - expand3To8(palette_color >> 5);
      const int delta_green = green - expand3To8((palette_color >> 2) & 0x07);
      const int delta_blue = blue - expand2To8(palette_color & 0x03);
      const uint32_t distance = (delta_red * delta_red) + (delta_green * delta_green) + (delta_blue * delta_blue);

      if(distance < best_distance)
      {
        best_distance = distance;
        best_index = index;
      }
    }

    palette_index_for_rgb332[color] = best_index;
    // Packed pixels store the left pixel in the high nibble. The RP2350 is
    // little-endian, so this table emits both DMA bytes with one 16-bit store.
    vga_4bit_expanded_pairs[color] =
      (uint16_t)vga_4bit_color[color >> 4] |
      ((uint16_t)vga_4bit_color[color & 0x0F] << 8);
  }

  palette_lookup_initialized = true;
}

static inline uint8_t paletteIndexForRgb332(uint8_t color)
{
  initializePaletteLookup();
  return palette_index_for_rgb332[color];
}

static inline void drawPackedGlyphRow(uint8_t* destination, uint16_t glyph_bits, const uint8_t opaque_pairs[4], uint8_t foreground_index, bool use_back_color)
{
  for(int pair = 0; pair < 8; pair++)
  {
    const uint8_t glyph_pair = (glyph_bits >> (14 - (pair * 2))) & 0x03;

    if(use_back_color)
    {
      destination[pair] = opaque_pairs[glyph_pair];
    }else if(glyph_pair == 1)
    {
      destination[pair] = (destination[pair] & 0xF0) | foreground_index;
    }else if(glyph_pair == 2)
    {
      destination[pair] = (destination[pair] & 0x0F) | (foreground_index << 4);
    }else if(glyph_pair == 3)
    {
      destination[pair] = (foreground_index << 4) | foreground_index;
    }
  }
}
}

void drawString_grid(char arr[40][60], uint8_t column, uint8_t line, String str, uint8_t color, uint8_t backColor, bool useBackColor)
{
  if(column < 40 && line < 60)
  {
    int counter = 0;

    while(counter < str.length()) 
    {
      if(16*column > 639)
      {
        if(line < 59)
        {
          column = 0;
          line++;
        }else
        {
          column = 0;
          line = 0;
        }
      }

      arr[column][line] = str.charAt(counter);
      gridBuffer_charColor[column][line] = color;

      if(useBackColor == false)
      {
        gridBuffer_backColor[column][line] = 300;
      }else
      {
        gridBuffer_backColor[column][line] = backColor;
      }

      counter++;
      column++;
    }
  }
}

void drawCharGridBuffer()
{
  const bool service_usb = usb_storage_active();
  for(int i = 0; i < 30; i++)
  {
    if (service_usb) usb_storage_poll();
    const int source_line = i + char_grid_buffer_V_draw_offset;

    for(int j = 0; j < 40; j++)
    {
      const uint16_t back_color = gridBuffer_backColor[j][source_line];
      drawLetter(16*j, i*16, gridBuffer_char[j][source_line], gridBuffer_charColor[j][source_line], back_color, back_color != 300);
    }
  }
}

void drawLetter(int16_t baseX, int16_t baseY, char letter, uint8_t color, uint8_t backColor, bool useBackColor)
{
  uint8_t glyph = (uint8_t)letter;

  if(glyph < 32 || glyph > 159)
  {
    glyph = '?';
  }

  const uint8_t foreground_index = paletteIndexForRgb332(color);
  const uint8_t background_index = paletteIndexForRgb332(backColor);
  const uint8_t opaque_pairs[4] =
  {
    (uint8_t)((background_index << 4) | background_index),
    (uint8_t)((background_index << 4) | foreground_index),
    (uint8_t)((foreground_index << 4) | background_index),
    (uint8_t)((foreground_index << 4) | foreground_index)
  };
  const uint32_t glyph_address = partitions[SYS_FONT_PARTITION_INDEX].start_addr + ((glyph - 32) * 8);

  if(baseX >= 0 && baseX + 15 < VGA_WIDTH && baseY >= 0 && baseY + 15 < VGA_HEIGHT && (baseX & 1) == 0)
  {
    for(int font_pair = 0; font_pair < 8; font_pair++)
    {
      const uint32_t font_value = memory[glyph_address + font_pair].value;
      uint8_t* top_row = vga_640_480_4_01 + ((((uint32_t)baseY + (font_pair * 2)) * VGA_WIDTH + baseX) >> 1);
      uint8_t* bottom_row = top_row + (VGA_WIDTH >> 1);

      drawPackedGlyphRow(top_row, font_value >> 16, opaque_pairs, foreground_index, useBackColor);
      drawPackedGlyphRow(bottom_row, font_value, opaque_pairs, foreground_index, useBackColor);
    }
    return;
  }

  for(int font_pair = 0; font_pair < 8; font_pair++)
  {
    const uint32_t font_value = memory[glyph_address + font_pair].value;
    const uint16_t glyph_rows[2] = {(uint16_t)(font_value >> 16), (uint16_t)font_value};

    for(int row = 0; row < 2; row++)
    {
      for(int bit = 0; bit < 16; bit++)
      {
        if((glyph_rows[row] << bit) & 0x8000)
        {
          vga4PutPixelInline(baseX + bit, baseY + (font_pair * 2) + row, foreground_index);
        }else if(useBackColor)
        {
          vga4PutPixelInline(baseX + bit, baseY + (font_pair * 2) + row, background_index);
        }
      }
    }
  }
}

void drawString_free(uint16_t baseX, uint16_t baseY, int letterSpacing, String str, uint8_t color, uint8_t backColor, bool useBackColor)
{
  for(int i = 0; i < str.length(); i++)
  {
    drawLetter(baseX+(letterSpacing*i), baseY, str.charAt(i), color, backColor, useBackColor);
  }
}

void drawPixel(int16_t pixelX, int16_t pixelY, uint8_t pixelColor)
{
  vga4PutPixelInline(pixelX, pixelY, paletteIndexForRgb332(pixelColor));
}

void clearVga4BitBuffer(uint8_t color)
{
  clearVga4BitBufferIndex(paletteIndexForRgb332(color));
}

void clearVga4BitBufferIndex(uint8_t colorIndex)
{
  graphics_vga4_clear(colorIndex);
}

void drawVga4BitPixel(int16_t pixelX, int16_t pixelY, uint8_t colorIndex)
{
  graphics_vga4_pixel(pixelX, pixelY, colorIndex);
}

void fillVga4BitRect(int x0, int y0, int x1, int y1, uint8_t colorIndex)
{
  graphics_vga4_fill_rect(x0, y0, x1, y1, colorIndex);
}

void drawVga4BitLine(int x0, int y0, int x1, int y1, uint8_t colorIndex)
{
  graphics_vga4_line(x0, y0, x1, y1, colorIndex);
}

void drawVga4BitGlyph(int x0, int y0, int glyphWidth, int glyphHeight, const var* glyphWords, uint8_t colorIndex)
{
  graphics_vga4_glyph(x0, y0, glyphWidth, glyphHeight, glyphWords, colorIndex);
}

void presentVga4BitBuffer()
{
  initializePaletteLookup();
  if (usb_storage_active())
  {
    // Bound time away from EP0 while copying the PSRAM framebuffer. USB
    // control requests must be serviced within a frame, not once per frame.
    uint16_t *destination = (uint16_t *)vga_data_array;
    constexpr uint32_t chunk_pairs = VGA_WIDTH * 2u;
    for (uint32_t base = 0; base < VGA_4BIT_BUFFER_SIZE; base += chunk_pairs)
    {
      usb_storage_poll();
      const uint32_t end = base + chunk_pairs < VGA_4BIT_BUFFER_SIZE
                         ? base + chunk_pairs : VGA_4BIT_BUFFER_SIZE;
      for (uint32_t pair = base; pair < end; ++pair)
        destination[pair] = vga_4bit_expanded_pairs[vga_640_480_4_01[pair]];
    }
    graphics_note_present();
    return;
  }
  graphics_vga4_present();
}

void draw_half_res_buffer()
{
  // Four 8-bit pixels are stored per word, matching RP2350 byte order.
  const uint8_t* source = (const uint8_t*)&memory[partitions[SYS_HALF_RES_BUFFER_PARTITION_INDEX].start_addr].value;

  for(uint16_t y = 0; y < 240; y++)
  {
    uint16_t* top_row = (uint16_t*)(vga_data_array + ((uint32_t)y * 2 * VGA_WIDTH));
    uint16_t* bottom_row = top_row + (VGA_WIDTH >> 1);

    for(uint16_t x = 0; x < 320; x++)
    {
      const uint8_t color = *source++;
      const uint16_t doubled_pixel = color | ((uint16_t)color << 8);
      top_row[x] = doubled_pixel;
      bottom_row[x] = doubled_pixel;
    }
  }
  graphics_note_present();
}

void draw_pixel_half_res_buffer(int x, int y, uint8_t color)
{
  if (x >= 0 && x < 320 && y >= 0 && y < 240) 
  {
    // Calculate which memory slot and which pixel within that slot
    uint32_t pixel_index = (y * 320) + x;
    uint32_t slot_index = pixel_index >> 2;  // Divide by 4
    uint32_t pixelInSlot = pixel_index & 3;  // Modulo 4
    
    // Lookup table for bit shift amounts
    uint8_t shift = shift_lookup[pixelInSlot];
    
    // Create mask and insert color at correct position
    uint32_t mask = 0xFF << shift;
    uint32_t new_color = (uint32_t)color << shift;
    
    // Read-modify-write: clear the old pixel, set new pixel
    const uint32_t buffer_address = partitions[SYS_HALF_RES_BUFFER_PARTITION_INDEX].start_addr + slot_index;
    memory[buffer_address].value = (memory[buffer_address].value & ~mask) | new_color;
  }
}

void drawThiccPixel(int x, int y, int thiccness, int color) 
{
  int r = thiccness / 2; // radius
  for (int dy = -r; dy <= r; dy++) 
  {
    for (int dx = -r; dx <= r; dx++) 
    {
      
      draw_pixel_half_res_buffer(x + dx, y + dy, color);
      
    }
  }
}

void draw_line_half_res_buffer(int x0, int y0, int x1, int y1, uint8_t color, int thiccness) 
{
  int dx = abs(x1 - x0);
  int sx = (x0 < x1) ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy; // error term

  // Fast path for thickness == 1 (most common case)
  if (thiccness <= 1) {
    while (true) 
    {
      // Inline bounds check and direct write to packed memory
      if (x0 >= 0 && x0 < 320 && y0 >= 0 && y0 < 240) 
      {
        uint32_t pixel_index = (y0 * 320) + x0;
        uint32_t slot_index = pixel_index >> 2;
        uint32_t pixelInSlot = pixel_index & 3;
        

        uint8_t shift = shift_lookup[pixelInSlot];
        
        uint32_t mask = 0xFF << shift;
        uint32_t new_color = (uint32_t)color << shift;
        
        const uint32_t buffer_address = partitions[SYS_HALF_RES_BUFFER_PARTITION_INDEX].start_addr + slot_index;
        memory[buffer_address].value = (memory[buffer_address].value & ~mask) | new_color;
        
      }

      if (x0 == x1 && y0 == y1) break;

      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  } else {
    // Thick line path
    while (true) 
    {
      if (x0 >= 0 && x0 < 320 && y0 >= 0 && y0 < 240) 
      {
        drawThiccPixel(x0, y0, thiccness, color);
      }

      if (x0 == x1 && y0 == y1) break;

      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
}

