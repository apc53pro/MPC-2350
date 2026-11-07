#ifndef NATIVE_GPU_RECORDS_H
#define NATIVE_GPU_RECORDS_H

#include <stdint.h>
#include <string.h>

// ARM argument layout shared with sdk/narm_gpu.tch. Wire bytes are emitted
// explicitly, including the reserved words.
struct NativeGpuPrimitiveInput {
  uint32_t kind, flags, shade, depth_function, addressing, rgb888, key_rgb565;
  int32_t scissor[4], bounds[4];
  uint32_t texture_base, texture_width, texture_height, texture_pitch, depth;
  int32_t origin[2], line_end[2], vertices_q4[6], edges[12];
  uint32_t planes[42];
};
static_assert(sizeof(NativeGpuPrimitiveInput) == 336, "NARM GPU input layout changed");

inline void native_gpu_put_word(uint8_t *out, unsigned index, uint32_t value)
{
  for (unsigned i = 0; i < 4; ++i) out[index * 4 + i] = (uint8_t)(value >> (8 * i));
}

inline bool native_gpu_heap_range(uint32_t address, uint64_t bytes)
{
  return (address & 3u) == 0 && address >= 0x280000u && address < 0x800000u &&
         bytes != 0 && bytes <= 0x800000u - address;
}

inline bool native_gpu_encode_tile(uint8_t *out, uint32_t list, uint32_t count)
{
  if (count > 1048576u || (!count ? list != 0 : !native_gpu_heap_range(list, count * 4u)))
    return false;
  native_gpu_put_word(out, 0, list);
  native_gpu_put_word(out, 1, count);
  return true;
}

inline bool native_gpu_encode_pass(uint8_t *out, uint32_t target, uint32_t flags,
                                  uint32_t color, uint32_t depth, uint32_t table)
{
  if (target > 1 || flags > 7 || color > 0xffffu || depth > 0xffffffu ||
      !native_gpu_heap_range(table, 2400)) return false;
  const uint32_t words[] = {0x3153504eu, target, flags, color, depth, table, 300, 0};
  for (unsigned i = 0; i < 8; ++i) native_gpu_put_word(out, i, words[i]);
  return true;
}

inline bool native_gpu_encode_primitive(uint8_t *out, const NativeGpuPrimitiveInput &p)
{
  const auto output = reinterpret_cast<uintptr_t>(out);
  const auto input = reinterpret_cast<uintptr_t>(&p);
  if (output <= input ? input - output < 320 : output - input < sizeof(p))
    return false;
  constexpr uint32_t color_write = 0x10000u, key_enable = 0x80000u;
  if (p.kind > 3 || (p.flags & ~0xf0000u) || p.shade > 4 || p.depth_function > 2 ||
      p.addressing > 3 || p.rgb888 > 0xffffffu || p.key_rgb565 > 0xffffu ||
      (!(p.flags & key_enable) && p.key_rgb565) || p.depth > 0xffffffu) return false;
  const bool triangle = p.kind == 0, line = p.kind == 2, sprite = p.kind == 3;
  const bool textured = p.shade >= 2;
  const bool has_texture = p.texture_base != 0;
  if ((!triangle && !sprite && (p.shade || (p.flags & key_enable))) ||
      (sprite && p.shade != 2) || (triangle && !textured && (p.flags & key_enable)) ||
      ((sprite || (textured && (p.flags & (color_write | key_enable)))) && !has_texture))
    return false;
  if (has_texture) {
    const uint32_t w = p.texture_width, h = p.texture_height, pitch = p.texture_pitch;
    if (!w || !h || w > 1024 || h > 1024 || (w & (w - 1)) || (h & (h - 1)) ||
        pitch < w * 2 || (pitch & 3u) ||
        !native_gpu_heap_range(p.texture_base, (uint64_t)pitch * h)) return false;
  }
  if (triangle) {
    if (p.bounds[0] < 0 || p.bounds[1] < 0 || (uint32_t)p.bounds[2] > 640 ||
        (uint32_t)p.bounds[3] > 480) return false;
    for (unsigned i = 0; i < 12; i += 4)
      if (p.edges[i] < -131072 || p.edges[i] > 131071 ||
          p.edges[i + 1] < -131072 || p.edges[i + 1] > 131071 ||
          (uint32_t)p.edges[i + 3] > 1) return false;
  }
  if (line && ((uint32_t)p.origin[0] >= 640 || (uint32_t)p.origin[1] >= 480 ||
               (uint32_t)p.line_end[0] >= 640 || (uint32_t)p.line_end[1] >= 480)) return false;
  if (sprite) {
    const int64_t w = (int64_t)p.bounds[2] - p.bounds[0];
    const int64_t h = (int64_t)p.bounds[3] - p.bounds[1];
    if (p.origin[0] < 0 || p.origin[1] < 0 ||
        (w > 0 && h > 0 && (w + p.origin[0] > p.texture_width ||
                            h + p.origin[1] > p.texture_height))) return false;
  }

  memset(out, 0, 320);
  native_gpu_put_word(out, 0, p.kind | 0x100u | p.flags);
  native_gpu_put_word(out, 1, p.shade | (p.depth_function << 8) | (p.addressing << 16) |
                            (has_texture ? 0x40000u : 0u));
  native_gpu_put_word(out, 2, p.rgb888);
  native_gpu_put_word(out, 3, p.key_rgb565);
  for (unsigned i = 0; i < 4; ++i) {
    native_gpu_put_word(out, 4 + i, (uint32_t)p.scissor[i]);
    if (!line) native_gpu_put_word(out, 8 + i, (uint32_t)p.bounds[i]);
  }
  if (has_texture) {
    native_gpu_put_word(out, 12, p.texture_base);
    native_gpu_put_word(out, 13, p.texture_width);
    native_gpu_put_word(out, 14, p.texture_height);
    native_gpu_put_word(out, 15, p.texture_pitch);
  }
  if (!triangle) native_gpu_put_word(out, 16, p.depth);
  if (line || sprite) for (unsigned i = 0; i < 2; ++i)
    native_gpu_put_word(out, 17 + i, (uint32_t)p.origin[i]);
  if (line) for (unsigned i = 0; i < 2; ++i)
    native_gpu_put_word(out, 20 + i, (uint32_t)p.line_end[i]);
  if (triangle) {
    for (unsigned i = 0; i < 6; ++i) native_gpu_put_word(out, 20 + i, (uint32_t)p.vertices_q4[i]);
    for (unsigned i = 0; i < 12; ++i) native_gpu_put_word(out, 26 + i, (uint32_t)p.edges[i]);
    for (unsigned i = 0; i < 42; ++i) native_gpu_put_word(out, 38 + i, p.planes[i]);
  }
  return true;
}

#endif
