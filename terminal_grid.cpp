#include "terminal_grid.h"

void terminal_write_grid(char grid[40][60], uint8_t column, uint8_t row, const String &text) {
  if (column >= 40 || row >= 60) return;
  for (unsigned i = 0; i < text.length(); ++i) {
    grid[column][row] = text.charAt(i);
    if (++column == 40) { column = 0; if (++row == 60) break; }
  }
}
