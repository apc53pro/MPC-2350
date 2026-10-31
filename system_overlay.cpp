#include "system_overlay.h"

#include "globals.h"
#include "graphics_runtime.h"

#include <stdio.h>
#include <string.h>

namespace
{
constexpr uint32_t kOverlayMilliseconds = 3000u;
constexpr int kOverlayWidth = VGA_WIDTH;
constexpr int kOverlayHeight = 38;
constexpr int kOverlayRowBytes = kOverlayWidth;

PSRAM uint8_t overlay_backing[kOverlayRowBytes * kOverlayHeight];
char overlay_line1[48] = {};
char overlay_line2[48] = {};
uint32_t overlay_deadline = 0;
uint32_t backing_generation = 0;
bool overlay_active = false;

void sanitize_name(const char *source, char *destination, size_t bytes)
{
  if (!bytes) return;
  size_t out = 0;
  while (source && *source && out + 1u < bytes)
  {
    const uint8_t c = (uint8_t)*source++;
    destination[out++] = c >= 32u && c <= 126u ? (char)c : '?';
  }
  destination[out] = '\0';
}

void capture_background()
{
  for (int y = 0; y < kOverlayHeight; ++y)
    memcpy(overlay_backing + y * kOverlayRowBytes,
           vga_data_array + y * VGA_WIDTH,
           kOverlayRowBytes);
}

void restore_background()
{
  for (int y = 0; y < kOverlayHeight; ++y)
    memcpy(vga_data_array + y * VGA_WIDTH,
           overlay_backing + y * kOverlayRowBytes,
           kOverlayRowBytes);
}

void draw_overlay_text(int x, int y, const char *text)
{
  for (uint32_t character = 0; text && text[character]; ++character)
  {
    const int base_x = x + (int)character * 16;
    if (base_x + 15 >= kOverlayWidth) break;
    uint8_t glyph = (uint8_t)text[character];
    if (glyph < 32u || glyph > 159u) glyph = '?';
    const uint32_t address = partitions[SYS_FONT_PARTITION_INDEX].start_addr +
                             ((uint32_t)(glyph - 32u) * 8u);
    for (int pair = 0; pair < 8; ++pair)
    {
      const uint32_t rows = memory[address + (uint32_t)pair].value;
      for (int row = 0; row < 2; ++row)
      {
        const uint16_t bits = row == 0 ? (uint16_t)(rows >> 16)
                                       : (uint16_t)rows;
        uint8_t *destination = vga_data_array +
                               (y + pair * 2 + row) * VGA_WIDTH + base_x;
        for (int bit = 0; bit < 16; ++bit)
          destination[bit] = (bits & (uint16_t)(0x8000u >> bit)) ? 0xffu : 0u;
      }
    }
  }
}

void render_overlay()
{
  for (int y = 0; y < kOverlayHeight; ++y)
    memset(vga_data_array + y * VGA_WIDTH, 0, kOverlayWidth);
  draw_overlay_text(4, 3, overlay_line1);
  if (overlay_line2[0]) draw_overlay_text(4, 20, overlay_line2);
}

void begin_overlay()
{
  const uint32_t generation = graphics_present_generation();
  if (overlay_active && generation == backing_generation) restore_background();
  capture_background();
  backing_generation = generation;
  overlay_active = true;
  overlay_deadline = millis() + kOverlayMilliseconds;
  render_overlay();
}
}

void system_overlay_show_focus(uint32_t pid, const char *name)
{
  overlay_line2[0] = '\0';
  if (pid == 0u)
  {
    snprintf(overlay_line1, sizeof(overlay_line1), "SYSTEM - TERMINAL");
  }
  else
  {
    char clean[28];
    sanitize_name(name, clean, sizeof(clean));
    snprintf(overlay_line1, sizeof(overlay_line1), "PID %lu - %s",
             (unsigned long)pid, clean[0] ? clean : "<unnamed>");
  }
  begin_overlay();
}

void system_overlay_show_mode(uint32_t pid,
                              const char *name,
                              NativeRunMode selected,
                              NativeRunMode effective)
{
  char clean[28];
  sanitize_name(name, clean, sizeof(clean));
  snprintf(overlay_line1, sizeof(overlay_line1), "PID %lu - %s",
           (unsigned long)pid, clean[0] ? clean : "<unnamed>");
  const char *effective_name = effective == NARM_RUN_MODE_BACKGROUND
                                 ? "BACKGROUND" : "FOREGROUND";
  if (selected == NARM_RUN_MODE_AUTO)
    snprintf(overlay_line2, sizeof(overlay_line2), "MODE: AUTO (%s)", effective_name);
  else
    snprintf(overlay_line2, sizeof(overlay_line2), "MODE: %s", effective_name);
  begin_overlay();
}

void system_overlay_poll()
{
  if (!overlay_active) return;
  if ((int32_t)(millis() - overlay_deadline) >= 0)
  {
    restore_background();
    overlay_active = false;
    return;
  }
  const uint32_t generation = graphics_present_generation();
  if (generation != backing_generation)
  {
    capture_background();
    backing_generation = generation;
  }
  render_overlay();
}
