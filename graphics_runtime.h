#ifndef GRAPHICS_RUNTIME_H
#define GRAPHICS_RUNTIME_H

#include "globals.h"

// Runtime-neutral drawing entry points used by the native interface.
void graphics_half_present(var *framebuffer);
void graphics_half_put_linear_pixel(var *framebuffer,
                                    uint32_t pixel_index,
                                    uint8_t color);
void graphics_half_draw_line(var *framebuffer,
                             int32_t x0,
                             int32_t y0,
                             int32_t x1,
                             int32_t y1,
                             uint8_t color);
void graphics_half_clear(var *framebuffer, uint8_t color);
void graphics_half_fill_rect(var *framebuffer,
                             int32_t x0,
                             int32_t y0,
                             int32_t x1,
                             int32_t y1,
                             uint8_t color);
void graphics_half_fill_triangle(var *framebuffer,
                                 int32_t x0,
                                 int32_t y0,
                                 int32_t x1,
                                 int32_t y1,
                                 int32_t x2,
                                 int32_t y2,
                                 uint8_t color);
bool graphics_half_blit(var *framebuffer,
                        int32_t x,
                        int32_t y,
                        int32_t width,
                        int32_t height,
                        const var *texture_words,
                        uint32_t texture_word_count);
bool graphics_half_blit_masked(var *framebuffer,
                               int32_t x,
                               int32_t y,
                               int32_t width,
                               int32_t height,
                               const var *texture_words,
                               uint32_t texture_word_count,
                               const var *mask_words,
                               uint32_t mask_word_count);
bool graphics_half_glyph(var *framebuffer,
                         int32_t x,
                         int32_t y,
                         int32_t width,
                         int32_t height,
                         const var *glyph_words,
                         uint32_t glyph_word_count,
                         uint8_t color);

void graphics_vga4_present();
void graphics_vga4_clear(uint8_t color_index);
void graphics_vga4_pixel(int32_t x, int32_t y, uint8_t color_index);
void graphics_vga4_fill_rect(int32_t x0,
                             int32_t y0,
                             int32_t x1,
                             int32_t y1,
                             uint8_t color_index);
void graphics_vga4_line(int32_t x0,
                        int32_t y0,
                        int32_t x1,
                        int32_t y1,
                        uint8_t color_index);
void graphics_vga4_glyph(int32_t x,
                         int32_t y,
                         int32_t width,
                         int32_t height,
                         const var *glyph_words,
                         uint8_t color_index);

// Monotonic scanout generation used by the firmware-owned transient overlay.
// Present paths increment it after replacing vga_data_array.
void graphics_note_present();
uint32_t graphics_present_generation();

#endif
