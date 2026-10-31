#include "globals.h"

#ifndef DRAW_FUNCS_H
#define DRAW_FUNCS_H

void drawString_grid(char arr[40][60], uint8_t column, uint8_t line, String str, uint8_t color, uint8_t backColor, bool useBackColor);

void drawCharGridBuffer();

void drawLetter(int16_t baseX, int16_t baseY, char letter, uint8_t color, uint8_t backColor, bool useBackColor);

void drawString_free(uint16_t baseX, uint16_t baseY, int letterSpacing, String str, uint8_t color, uint8_t backColor, bool useBackColor);

void drawPixel(int16_t pixelX, int16_t pixelY, uint8_t pixelColor);

void draw_pixel_half_res_buffer(int x, int y, uint8_t color);

void drawThiccPixel(int x, int y, int thiccness, int color);

void draw_line_half_res_buffer(int x0, int y0, int x1, int y1, uint8_t color, int thiccness);

void clearVga4BitBuffer(uint8_t color);

void clearVga4BitBufferIndex(uint8_t colorIndex);

void drawVga4BitPixel(int16_t pixelX, int16_t pixelY, uint8_t colorIndex);

void fillVga4BitRect(int x0, int y0, int x1, int y1, uint8_t colorIndex);

void drawVga4BitLine(int x0, int y0, int x1, int y1, uint8_t colorIndex);

void drawVga4BitGlyph(int x0, int y0, int glyphWidth, int glyphHeight, const var* glyphWords, uint8_t colorIndex);

void presentVga4BitBuffer();

void draw_half_res_buffer();

#endif
