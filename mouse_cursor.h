#ifndef MOUSE_CURSOR_H
#define MOUSE_CURSOR_H

#include <stdint.h>

// Call on Core 1 once VGA scanout is ready. Claims one hardware alarm there;
// returns false if unavailable, and safely permits a later retry.
bool mouse_cursor_init();
// One atomic target; safe for mouse packet/connection updates on either core.
void mouse_cursor_set_target(int32_t x, int32_t y, bool visible);
// Bounded deferred update, also called automatically after a presentation.
void mouse_cursor_poll();

// All scene writes/readbacks of scanout must be bracketed. These hooks never
// disable interrupts or hold a lock across a frame copy. While bracketed the
// cursor stays at its latched position and writes update its saved background.
void mouse_cursor_begin_present();
void mouse_cursor_end_present();
bool mouse_cursor_row_bounds(int y, int &left, int &right);
// One latched, clipped rectangle per presentation; unaffected rows use the
// original bulk conversion loops with no cursor queries.
bool mouse_cursor_bounds(int &left, int &top, int &right, int &bottom);
void mouse_cursor_write_pair(int x, int y, uint16_t colors);
void mouse_cursor_write_row(int y, int x, const uint8_t *source, uint32_t count);
void mouse_cursor_read_row(int y, int x, uint8_t *destination, uint32_t count);

#endif
