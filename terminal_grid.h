#ifndef TERMINAL_GRID_H
#define TERMINAL_GRID_H
#include <Arduino.h>
void terminal_write_grid(char grid[40][60], uint8_t column, uint8_t row, const String &text);
#endif
