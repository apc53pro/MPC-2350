#include "globals.h"

#ifndef TERMINAL_H
#define TERMINAL_H

void echo_message(String message);

void userInput();

void scroll_terminal_up(int times);

void draw_line_pointer(int x, int y, bool show_line_pointer);

void blinkCursor(int x, int y, bool active);

void controls();

void terminal(bool run, bool show_line_pointer, bool show_cursor);

#endif
