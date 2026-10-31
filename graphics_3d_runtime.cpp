#include "graphics_3d_runtime.h"

#include "ps2_keyboard_driver.h"
#include "audio_runtime.h"
#include <string.h>

enum
{
  TRI3D_STAT_TRIANGLES_SUBMITTED = 0,
  TRI3D_STAT_TRIANGLES_CULLED = 1,
  TRI3D_STAT_TRIANGLES_RASTERIZED = 2,
  TRI3D_STAT_PIXELS_TESTED = 3,
  TRI3D_STAT_PIXELS_WRITTEN = 4,
  TRI3D_STAT_Z_REJECTED = 5,
  TRI3D_STAT_FAST_UNTOUCHED_WORDS = 6,
  TRI3D_STAT_FAST_CURRENT_WORDS = 7,
  TRI3D_STAT_QUADS_SUBMITTED = 8,
  TRI3D_STAT_COUNT = 16
};

#ifndef TRI3D_RUNTIME_STATS
// Expensive legacy per-pixel statistics remain compiled out by default.
#define TRI3D_RUNTIME_STATS 0
#endif

static uint8_t tri3d_stats_enabled = 0;
static uint8_t tri3d_force_general_sampler = 0;
static uint8_t tri3d_pixel_stats = 0;
static uint8_t tri3d_stage_profile = 0;
static uint8_t tri3d_force_legacy_geometry = 0;
static uint32_t tri3d_stage_stats[30]; // read IDs 35..64; frame cumulative
// Lightweight, opt-in span counts [9..12], covered-pixel candidates [13..16],
// and eligible clamped spans/pixel candidates [17..18]. No per-pixel hooks.
static uint32_t tri3d_sampler_stats[10];
static uint32_t tri3d_tile_stats[6]; // jobs, triangles, candidates, rejects, mask tests, writes
static void tri3d_occlusion_invalidate();

void tri3d_set_stats_enabled(uint32_t flags)
{
  tri3d_stats_enabled = flags ? 1u : 0u;
  tri3d_force_general_sampler = (flags & 0x100u) ? 1u : 0u;
  tri3d_pixel_stats = (flags & 0x200u) ? 1u : 0u;
  tri3d_stage_profile = (flags & 0x400u) ? 1u : 0u;
  tri3d_force_legacy_geometry = (flags & 0x800u) ? 1u : 0u;
}

bool tri3d_geometry_optimized_enabled()
{
  return tri3d_force_legacy_geometry == 0u;
}

bool tri3d_stage_profile_enabled()
{
  return tri3d_stage_profile != 0u;
}

void tri3d_stage_profile_add(uint32_t stat, uint32_t amount)
{
  if (tri3d_stage_profile && stat >= 35u && stat <= 64u)
    tri3d_stage_stats[stat - 35u] += amount;
}

static inline __attribute__((always_inline)) void tri3d_sampler_span(uint32_t path, int32_t x0, int32_t x1)
{
  if (tri3d_stats_enabled && x1 >= x0)
  {
    ++tri3d_sampler_stats[path];
    tri3d_sampler_stats[path + 4u] += (uint32_t)x1 - (uint32_t)x0 + 1u;
  }
}

#if TRI3D_RUNTIME_STATS
static uint32_t tri3d_stats[TRI3D_STAT_COUNT];
#endif

static inline __attribute__((always_inline)) void tri3d_stat_add(uint8_t stat, uint32_t amount)
{
#if TRI3D_RUNTIME_STATS
  if (tri3d_stats_enabled && stat < TRI3D_STAT_COUNT)
  {
    tri3d_stats[stat] += amount;
  }
#else
  (void)stat;
  (void)amount;
#endif
}

void tri3d_stats_reset()
{
  for (uint8_t i = 0; i < 10u; ++i) tri3d_sampler_stats[i] = 0;
  for (uint8_t i = 0; i < 6u; ++i) tri3d_tile_stats[i] = 0;
  for (uint8_t i = 0; i < 30u; ++i) tri3d_stage_stats[i] = 0;
#if TRI3D_RUNTIME_STATS
  for (uint8_t i = 0; i < TRI3D_STAT_COUNT; i++)
  {
    tri3d_stats[i] = 0;
  }
#endif
}

uint32_t tri3d_stats_read(uint32_t stat)
{
  if (stat == 70u) return 0x43550001u;
  if (stat >= 71u && stat <= 75u) return tri3d_stage_stats[25u + stat - 71u];
  if (stat == 66u) return 0x50500001u; // Inlined setup/local command preparation.
  if (stat == 65u) return 0x47500001u;
  if (stat == 34u) return 0x44530001u;
  if (stat >= 35u && stat <= 59u) return tri3d_stage_stats[stat - 35u];
  if (stat >= 60u && stat <= 64u) return 0u;
  if (stat == 24u) return 0x54420001u;
  if (stat >= 25u && stat <= 30u) return tri3d_tile_stats[stat - 25u];
  if (stat == 31u) return 15360u; // two 320 x 4-row SRAM pixel workspaces
  if (stat >= 9u && stat <= 18u) return tri3d_sampler_stats[stat - 9u];
  if (stat == 19u) return 0x53500001u; // Sampler diagnostics protocol v1.
  if (stat == 20u) return (tri3d_stats_enabled ? 1u : 0u) |
                          (tri3d_force_general_sampler ? 0x100u : 0u) |
                          (tri3d_pixel_stats ? 0x200u : 0u) |
                          (tri3d_stage_profile ? 0x400u : 0u) |
                          (tri3d_force_legacy_geometry ? 0x800u : 0u);
#if TRI3D_RUNTIME_STATS
  if (stat >= TRI3D_STAT_COUNT)
  {
    return 0;
  }
  return tri3d_stats[stat];
#else
  (void)stat;
  return 0;
#endif
}

static inline __attribute__((always_inline)) uint32_t tri3d_z_from_float(float z)
{
  if (!(z == z) || z <= 0.0f)
  {
    return 0u;
  }
  if (z >= 4294967040.0f)
  {
    return 0xFFFFFFFFu;
  }
  return (uint32_t)z;
}

static inline __attribute__((always_inline)) uint8_t tri3d_z_nearer(uint32_t z, uint32_t old_z)
{
  return (uint8_t)(z < old_z);
}

static inline __attribute__((always_inline)) int32_t tri3d_round_i32(float v)
{
  return (int32_t)((v >= 0.0f) ? (v + 0.5f) : (v - 0.5f));
}

static inline __attribute__((always_inline)) int32_t tri3d_interp_i32(int32_t a, int32_t b, int32_t num, int32_t den)
{
  if (den == 0)
  {
    return a;
  }
  return a + (((b - a) * num) / den);
}

static inline __attribute__((always_inline)) float tri3d_interp_f(float a, float b, int32_t num, int32_t den)
{
  if (den == 0)
  {
    return a;
  }
  return a + ((b - a) * ((float)num / (float)den));
}

static inline __attribute__((always_inline)) float tri3d_color_r_f(uint32_t color)
{
  return (float)((color >> 5) & 7u);
}

static inline __attribute__((always_inline)) float tri3d_color_g_f(uint32_t color)
{
  return (float)((color >> 2) & 7u);
}

static inline __attribute__((always_inline)) float tri3d_color_b_f(uint32_t color)
{
  return (float)(color & 3u);
}

static inline __attribute__((always_inline)) uint8_t tri3d_pack_rgb332_f(float r, float g, float b)
{
  int32_t ri = tri3d_round_i32(r);
  int32_t gi = tri3d_round_i32(g);
  int32_t bi = tri3d_round_i32(b);
  if (ri < 0) ri = 0; else if (ri > 7) ri = 7;
  if (gi < 0) gi = 0; else if (gi > 7) gi = 7;
  if (bi < 0) bi = 0; else if (bi > 3) bi = 3;
  return (uint8_t)((ri << 5) | (gi << 2) | bi);
}

static inline __attribute__((always_inline)) int32_t tri3d_tex_coord_wrap(int32_t coord, int32_t size)
{
  if ((size & (size - 1)) == 0)
  {
    return coord & (size - 1);
  }
  coord %= size;
  if (coord < 0)
  {
    coord += size;
  }
  return coord;
}

static inline __attribute__((always_inline)) uint8_t tri3d_sample_tex_rgb332(var* tex_mem,
                                                                             uint32_t tex_base,
                                                                             int32_t tex_w,
                                                                             int32_t tex_h,
                                                                             uint32_t flags,
                                                                             int32_t ufx,
                                                                             int32_t vfx)
{
  int32_t u = ufx >> 16;
  int32_t v = vfx >> 16;
  if (flags & 0x1u)
  {
    u = tri3d_tex_coord_wrap(u, tex_w);
  }
  else
  {
    if (u < 0) u = 0;
    else if (u >= tex_w) u = tex_w - 1;
  }
  if (flags & 0x2u)
  {
    v = tri3d_tex_coord_wrap(v, tex_h);
  }
  else
  {
    if (v < 0) v = 0;
    else if (v >= tex_h) v = tex_h - 1;
  }
  const uint32_t texel = (uint32_t)v * (uint32_t)tex_w + (uint32_t)u;
  const uint32_t word = tex_mem[tex_base + (texel >> 2)].value;
  return (uint8_t)((word >> ((texel & 3u) * 8u)) & 0xFFu);
}

#define TRI3D_TEX_PERSPECTIVE_SCALE 1073741824.0f

static inline __attribute__((always_inline)) int32_t tri3d_floor_to_i32_sat(float value)
{
  if (!(value == value))
  {
    return 0;
  }
  if (value > 2147483520.0f)
  {
    return 2147483520;
  }
  if (value < -2147483520.0f)
  {
    return -2147483520;
  }
  const int32_t integer = (int32_t)value;
  return integer - (int32_t)(value < (float)integer);
}

#define TRI3D_EDGE_FX_SHIFT 16
#define TRI3D_EDGE_FX_ONE (1LL << TRI3D_EDGE_FX_SHIFT)
#define TRI3D_EDGE_FX_HALF (TRI3D_EDGE_FX_ONE >> 1)

static inline __attribute__((always_inline)) int32_t tri3d_edge_fx_from_float(float value)
{
  return tri3d_round_i32(value * (float)TRI3D_EDGE_FX_ONE);
}

static inline __attribute__((always_inline)) int32_t tri3d_edge_fx_ceil(int64_t value)
{
  int64_t whole = value / TRI3D_EDGE_FX_ONE;
  if (value > 0 && (value % TRI3D_EDGE_FX_ONE) != 0) whole++;
  return (int32_t)whole;
}

static inline __attribute__((always_inline)) int64_t tri3d_edge_fx_slope(int32_t x0,
                                                                         int32_t y0,
                                                                         int32_t x1,
                                                                         int32_t y1)
{
  const int64_t dx = (int64_t)x1 - (int64_t)x0;
  const int64_t dy = (int64_t)y1 - (int64_t)y0;
  return (dy != 0) ? ((dx * TRI3D_EDGE_FX_ONE) / dy) : 0;
}

static inline __attribute__((always_inline)) int64_t tri3d_edge_fx_at_y(int32_t x,
                                                                        int32_t y,
                                                                        int64_t slope,
                                                                        int32_t scan_y)
{
  const int64_t sample_y = ((int64_t)scan_y * TRI3D_EDGE_FX_ONE) + TRI3D_EDGE_FX_HALF;
  return (int64_t)x + ((slope * (sample_y - (int64_t)y)) / TRI3D_EDGE_FX_ONE);
}

static inline __attribute__((always_inline)) uint8_t tri3d_sample_tex_rgb332_cached_uv(var* tex_mem,
                                                                                       uint32_t tex_base,
                                                                                       int32_t tex_w,
                                                                                       int32_t tex_h,
                                                                                       uint32_t flags,
                                                                                       uint32_t tex_w_mask,
                                                                                       uint32_t tex_h_mask,
                                                                                       uint8_t tex_w_pow2,
                                                                                       uint8_t tex_h_pow2,
                                                                                       int32_t u,
                                                                                       int32_t v,
                                                                                       uint32_t& cache_addr,
                                                                                       uint32_t& cache_word)
{
  if (flags & 0x1u)
  {
    u = tex_w_pow2 ? (int32_t)((uint32_t)u & tex_w_mask) : tri3d_tex_coord_wrap(u, tex_w);
  }
  else
  {
    if (u < 0) u = 0;
    else if (u >= tex_w) u = tex_w - 1;
  }

  if (flags & 0x2u)
  {
    v = tex_h_pow2 ? (int32_t)((uint32_t)v & tex_h_mask) : tri3d_tex_coord_wrap(v, tex_h);
  }
  else
  {
    if (v < 0) v = 0;
    else if (v >= tex_h) v = tex_h - 1;
  }

  const uint32_t texel = (uint32_t)v * (uint32_t)tex_w + (uint32_t)u;
  const uint32_t word_addr = tex_base + (texel >> 2);
  if (word_addr != cache_addr)
  {
    cache_addr = word_addr;
    cache_word = tex_mem[word_addr].value;
  }
  return (uint8_t)((cache_word >> ((texel & 3u) * 8u)) & 0xFFu);
}

static inline __attribute__((always_inline)) uint8_t tri3d_sample_tex_rgb332_persp_cached(var* tex_mem,
                                                                                          uint32_t tex_base,
                                                                                          int32_t tex_w,
                                                                                          int32_t tex_h,
                                                                                          uint32_t flags,
                                                                                          uint32_t tex_w_mask,
                                                                                          uint32_t tex_h_mask,
                                                                                          uint8_t tex_w_pow2,
                                                                                          uint8_t tex_h_pow2,
                                                                                          float uq,
                                                                                          float vq,
                                                                                          float q,
                                                                                          uint32_t& cache_addr,
                                                                                          uint32_t& cache_word)
{
  if (!(q > 0.0f))
  {
    return 0;
  }
  const float inv_q = 1.0f / q;
  const int32_t u = tri3d_floor_to_i32_sat(uq * inv_q);
  const int32_t v = tri3d_floor_to_i32_sat(vq * inv_q);
  return tri3d_sample_tex_rgb332_cached_uv(tex_mem, tex_base, tex_w, tex_h, flags,
                                           tex_w_mask, tex_h_mask, tex_w_pow2, tex_h_pow2,
                                           u, v, cache_addr, cache_word);
}

// For a positive size <= 2^24, size-1 is exactly representable as binary32.
// floor+clamp therefore reduces to the two boundary tests below and one
// truncation of a nonnegative in-range value. NaN and -infinity still map to
// zero; +infinity maps to the last texel, exactly like the general sampler.
static inline __attribute__((always_inline)) int32_t tri3d_tex_coord_clamped(float value,
                                                                          int32_t size)
{
  if (!(value > 0.0f)) return 0;
  const int32_t last = size - 1;
  if (value >= (float)last) return last;
  return (int32_t)value;
}

static inline __attribute__((always_inline)) uint8_t tri3d_clamped_sampler_supported(int32_t width,
                                                                                  int32_t height,
                                                                                  uint32_t flags)
{
  return (uint8_t)((flags & 0x3u) == 0u && width > 0 && height > 0 &&
                   width <= 16777216 && height <= 16777216);
}

static inline __attribute__((always_inline)) uint8_t tri3d_sample_tex_rgb332_clamped_persp_cached(var* tex_mem,
                                                                                               uint32_t tex_base,
                                                                                               int32_t tex_w,
                                                                                               int32_t tex_h,
                                                                                               float uq,
                                                                                               float vq,
                                                                                               float q,
                                                                                               uint32_t& cache_addr,
                                                                                               uint32_t& cache_word)
{
  if (!(q > 0.0f)) return 0;
  // Preserve the general path's reciprocal and multiplication order. In
  // particular, do not rebase UVs or approximate perspective interpolation.
  const float inv_q = 1.0f / q;
  const int32_t u = tri3d_tex_coord_clamped(uq * inv_q, tex_w);
  const int32_t v = tri3d_tex_coord_clamped(vq * inv_q, tex_h);
  const uint32_t texel = (uint32_t)v * (uint32_t)tex_w + (uint32_t)u;
  const uint32_t word_addr = tex_base + (texel >> 2);
  if (word_addr != cache_addr)
  {
    cache_addr = word_addr;
    cache_word = tex_mem[word_addr].value;
  }
  return (uint8_t)((cache_word >> ((texel & 3u) * 8u)) & 0xFFu);
}

static inline __attribute__((always_inline)) uint8_t tri3d_sample_tex_rgb332_16wrap_persp_cached(var* tex_mem,
                                                                                                uint32_t tex_base,
                                                                                                float uq,
                                                                                                float vq,
                                                                                                float q,
                                                                                                uint32_t& cache_addr,
                                                                                                uint32_t& cache_word)
{
  if (!(q > 0.0f))
  {
    return 0;
  }
  const float inv_q = 1.0f / q;
  // The 16x16 fast path is selected only after the triangle-wide bounds check
  // in tri3d_raster_projected_tri_tex proves these conversions are in range.
  // __builtin_floorf maps to VRINTM.F32 + VCVT.S32.F32 on RP2350, avoiding
  // the NaN/range/rounding fixups required by the fully general sampler.
  const uint32_t u = (uint32_t)(int32_t)__builtin_floorf(uq * inv_q) & 15u;
  const uint32_t v = (uint32_t)(int32_t)__builtin_floorf(vq * inv_q) & 15u;
  const uint32_t texel = (v << 4) | u;
  const uint32_t word_addr = tex_base + (texel >> 2);
  if (word_addr != cache_addr)
  {
    cache_addr = word_addr;
    cache_word = tex_mem[word_addr].value;
  }
  return (uint8_t)((cache_word >> ((texel & 3u) * 8u)) & 0xFFu);
}

static inline __attribute__((always_inline)) uint8_t tri3d_texel_index_persp(uint32_t& out_texel,
                                                                             int32_t tex_w,
                                                                             int32_t tex_h,
                                                                             uint32_t flags,
                                                                             uint32_t tex_w_mask,
                                                                             uint32_t tex_h_mask,
                                                                             uint8_t tex_w_pow2,
                                                                             uint8_t tex_h_pow2,
                                                                             float uq,
                                                                             float vq,
                                                                             float q)
{
  if (!(q > 0.0f))
  {
    return 0;
  }

  const float inv_q = 1.0f / q;
  int32_t u = tri3d_floor_to_i32_sat(uq * inv_q);
  int32_t v = tri3d_floor_to_i32_sat(vq * inv_q);

  if (flags & 0x1u)
  {
    u = tex_w_pow2 ? (int32_t)((uint32_t)u & tex_w_mask) : tri3d_tex_coord_wrap(u, tex_w);
  }
  else
  {
    if (u < 0) u = 0;
    else if (u >= tex_w) u = tex_w - 1;
  }

  if (flags & 0x2u)
  {
    v = tex_h_pow2 ? (int32_t)((uint32_t)v & tex_h_mask) : tri3d_tex_coord_wrap(v, tex_h);
  }
  else
  {
    if (v < 0) v = 0;
    else if (v >= tex_h) v = tex_h - 1;
  }

  out_texel = (uint32_t)v * (uint32_t)tex_w + (uint32_t)u;
  return 1;
}


static inline __attribute__((always_inline)) uint8_t tri3d_texel_index_16wrap_persp(uint32_t& out_texel,
                                                                                   float uq,
                                                                                   float vq,
                                                                                   float q)
{
  if (!(q > 0.0f))
  {
    return 0;
  }

  const float inv_q = 1.0f / q;
  const uint32_t u = (uint32_t)tri3d_floor_to_i32_sat(uq * inv_q) & 15u;
  const uint32_t v = (uint32_t)tri3d_floor_to_i32_sat(vq * inv_q) & 15u;
  out_texel = (v << 4) | u;
  return 1;
}

static inline __attribute__((always_inline)) uint8_t tri3d_mask_texel_draws(var* mask_mem,
                                                                            uint32_t mask_base,
                                                                            uint32_t texel,
                                                                            uint32_t& mask_cache_addr,
                                                                            uint32_t& mask_cache_word)
{
  const uint32_t word_addr = mask_base + (texel >> 5);
  if (word_addr != mask_cache_addr)
  {
    mask_cache_addr = word_addr;
    mask_cache_word = mask_mem[word_addr].value;
  }

  // Same bit order used by OP_FILLRECT2D_TEX_MASKING:
  // texel 0 is bit 31 of word 0, texel 31 is bit 0 of word 0.
  const uint8_t bit = 31u - (uint8_t)(texel & 31u);
  return (uint8_t)((mask_cache_word >> bit) & 1u);
}


enum
{
  TRI3D_MASK_CLASS_MIXED = 0,
  TRI3D_MASK_CLASS_EMPTY = 1,
  TRI3D_MASK_CLASS_OPAQUE = 2
};

static uint8_t __attribute__((section(".itcm"))) tri3d_classify_mask_words(var* mask_mem,
                                                                           uint32_t mask_base,
                                                                           uint32_t texel_count)
{
  const uint32_t full_words = texel_count >> 5;
  const uint32_t tail_bits = texel_count & 31u;
  uint8_t all_empty = 1;
  uint8_t all_opaque = 1;

  for (uint32_t i = 0; i < full_words; i++)
  {
    const uint32_t w = mask_mem[mask_base + i].value;
    if (w != 0u) all_empty = 0;
    if (w != 0xFFFFFFFFu) all_opaque = 0;
    if (!all_empty && !all_opaque)
    {
      return TRI3D_MASK_CLASS_MIXED;
    }
  }

  if (tail_bits != 0u)
  {
    const uint32_t valid_mask = 0xFFFFFFFFu << (32u - tail_bits);
    const uint32_t w = mask_mem[mask_base + full_words].value & valid_mask;
    if (w != 0u) all_empty = 0;
    if (w != valid_mask) all_opaque = 0;
  }

  if (all_empty) return TRI3D_MASK_CLASS_EMPTY;
  if (all_opaque) return TRI3D_MASK_CLASS_OPAQUE;
  return TRI3D_MASK_CLASS_MIXED;
}

static inline __attribute__((always_inline)) uint8_t tri3d_sample_texel_rgb332_cached(var* tex_mem,
                                                                                       uint32_t tex_base,
                                                                                       uint32_t texel,
                                                                                       uint32_t& cache_addr,
                                                                                       uint32_t& cache_word)
{
  const uint32_t word_addr = tex_base + (texel >> 2);
  if (word_addr != cache_addr)
  {
    cache_addr = word_addr;
    cache_word = tex_mem[word_addr].value;
  }
  return (uint8_t)((cache_word >> ((texel & 3u) * 8u)) & 0xFFu);
}

static inline __attribute__((always_inline)) uint8_t tri3d_z_accept(var* gfx_z_mem,
                                                                    var* gfx_z_stamp_mem,
                                                                    uint8_t frame_id,
                                                                    uint32_t pixel,
                                                                    float z)
{
  tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
  const uint32_t stamp_slot = pixel >> 2;
  const uint32_t stamp_shift = (pixel & 3u) * 8u;
  const uint32_t stamp_mask = 0xFFu << stamp_shift;
  const uint32_t stamp_word = gfx_z_stamp_mem[stamp_slot].value;
  const uint32_t stamp = (stamp_word >> stamp_shift) & 0xFFu;
  const uint8_t current = (uint8_t)(stamp == frame_id);
  const uint32_t old_z = current ? gfx_z_mem[pixel].value : 0xFFFFFFFFu;
  const uint32_t new_z = tri3d_z_from_float(z);

  if (!current || tri3d_z_nearer(new_z, old_z))
  {
    gfx_z_mem[pixel].value = new_z;
    if (!current)
    {
      gfx_z_stamp_mem[stamp_slot].value =
        (stamp_word & ~stamp_mask) | ((uint32_t)frame_id << stamp_shift);
    }
    tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
    return 1;
  }

  tri3d_stat_add(TRI3D_STAT_Z_REJECTED, 1);
  return 0;
}

// Immediate-mode fallback used when the tiled batcher cannot use its graphics
// scratch area. The legacy solid tile workspace below resides in PSRAM.
static void __attribute__((section(".itcm"), noinline)) tri3d_write_span_z(var* gfx_color_mem,
                                                                 var* gfx_z_mem,
                                                                 var* gfx_z_stamp_mem,
                                                                 uint8_t frame_id,
                                                                 uint32_t color,
                                                                 uint32_t packed_color,
                                                                 uint32_t stamp_word_full,
                                                                 int32_t y,
                                                                 int32_t x0,
                                                                 int32_t x1,
                                                                 float z,
                                                                 float dzdx)
{
  int32_t x = x0;
  float zcur = z;
  uint32_t pixel = (uint32_t)y * 320u + (uint32_t)x;
  uint32_t slot = pixel >> 2;
  int32_t lane = x & 3;

  if (lane != 0)
  {
    uint32_t word = 0;
    uint8_t dirty = 0;
    while (lane < 4 && x <= x1)
    {
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        const uint32_t shift = (uint32_t)lane * 8u;
        word = (word & ~(0xFFu << shift)) | (color << shift);
        dirty = 1;
      }
      zcur += dzdx;
      lane++;
      x++;
      pixel++;
    }
    if (dirty) gfx_color_mem[slot].value = word;
    slot++;
  }

  while (x + 3 <= x1)
  {
    const uint32_t stamp_word = gfx_z_stamp_mem[slot].value;
    if (((stamp_word & 0xFFu) != frame_id) &&
        (((stamp_word >> 8) & 0xFFu) != frame_id) &&
        (((stamp_word >> 16) & 0xFFu) != frame_id) &&
        (((stamp_word >> 24) & 0xFFu) != frame_id))
    {
      uint32_t z32;
      z32 = tri3d_z_from_float(zcur); gfx_z_mem[pixel].value = z32; zcur += dzdx; pixel++;
      z32 = tri3d_z_from_float(zcur); gfx_z_mem[pixel].value = z32; zcur += dzdx; pixel++;
      z32 = tri3d_z_from_float(zcur); gfx_z_mem[pixel].value = z32; zcur += dzdx; pixel++;
      z32 = tri3d_z_from_float(zcur); gfx_z_mem[pixel].value = z32; zcur += dzdx; pixel++;
      gfx_z_stamp_mem[slot].value = stamp_word_full;
      gfx_color_mem[slot].value = packed_color;
      tri3d_stat_add(TRI3D_STAT_FAST_UNTOUCHED_WORDS, 1);
      tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 4);
    }
    else if (stamp_word == stamp_word_full)
    {
      uint32_t word = 0;
      uint8_t dirty = 0;
      uint32_t z32;

      z32 = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(z32, gfx_z_mem[pixel].value))
      {
        gfx_z_mem[pixel].value = z32;
        word = gfx_color_mem[slot].value;
        word = (word & ~0x000000FFu) | color;
        dirty = 1;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      else tri3d_stat_add(TRI3D_STAT_Z_REJECTED, 1);
      zcur += dzdx; pixel++;

      z32 = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(z32, gfx_z_mem[pixel].value))
      {
        gfx_z_mem[pixel].value = z32;
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0x0000FF00u) | (color << 8);
        dirty = 1;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      else tri3d_stat_add(TRI3D_STAT_Z_REJECTED, 1);
      zcur += dzdx; pixel++;

      z32 = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(z32, gfx_z_mem[pixel].value))
      {
        gfx_z_mem[pixel].value = z32;
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0x00FF0000u) | (color << 16);
        dirty = 1;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      else tri3d_stat_add(TRI3D_STAT_Z_REJECTED, 1);
      zcur += dzdx; pixel++;

      z32 = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(z32, gfx_z_mem[pixel].value))
      {
        gfx_z_mem[pixel].value = z32;
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0xFF000000u) | (color << 24);
        dirty = 1;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      else tri3d_stat_add(TRI3D_STAT_Z_REJECTED, 1);
      zcur += dzdx; pixel++;

      if (dirty) gfx_color_mem[slot].value = word;
      tri3d_stat_add(TRI3D_STAT_FAST_CURRENT_WORDS, 1);
    }
    else
    {
      uint32_t word = 0;
      uint8_t dirty = 0;
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        word = gfx_color_mem[slot].value;
        word = (word & ~0x000000FFu) | color;
        dirty = 1;
      }
      zcur += dzdx; pixel++;
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0x0000FF00u) | (color << 8);
        dirty = 1;
      }
      zcur += dzdx; pixel++;
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0x00FF0000u) | (color << 16);
        dirty = 1;
      }
      zcur += dzdx; pixel++;
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0xFF000000u) | (color << 24);
        dirty = 1;
      }
      zcur += dzdx; pixel++;
      if (dirty) gfx_color_mem[slot].value = word;
    }

    slot++;
    x += 4;
  }

  if (x <= x1)
  {
    uint32_t word = 0;
    uint8_t dirty = 0;
    lane = 0;
    while (x <= x1)
    {
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        const uint32_t shift = (uint32_t)lane * 8u;
        word = (word & ~(0xFFu << shift)) | (color << shift);
        dirty = 1;
      }
      zcur += dzdx;
      lane++;
      x++;
      pixel++;
    }
    if (dirty) gfx_color_mem[slot].value = word;
  }
}

static void __attribute__((section(".itcm"))) tri3d_write_span_gouraud_z(var* gfx_color_mem,
                                                                         var* gfx_z_mem,
                                                                         var* gfx_z_stamp_mem,
                                                                         uint8_t frame_id,
                                                                         uint32_t stamp_word_full,
                                                                         int32_t y,
                                                                         int32_t x0,
                                                                         int32_t x1,
                                                                         float r,
                                                                         float g,
                                                                         float b,
                                                                         float drdx,
                                                                         float dgdx,
                                                                         float dbdx,
                                                                         float z,
                                                                         float dzdx)
{
  int32_t x = x0;
  float zcur = z;
  uint32_t pixel = (uint32_t)y * 320u + (uint32_t)x;
  uint32_t slot = pixel >> 2;
  int32_t lane = x & 3;

  if (lane != 0)
  {
    uint32_t word = 0;
    uint8_t dirty = 0;
    while (lane < 4 && x <= x1)
    {
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        const uint32_t shift = (uint32_t)lane * 8u;
        const uint32_t color = tri3d_pack_rgb332_f(r, g, b);
        word = (word & ~(0xFFu << shift)) | (color << shift);
        dirty = 1;
      }
      r += drdx; g += dgdx; b += dbdx; zcur += dzdx;
      lane++;
      x++;
      pixel++;
    }
    if (dirty) gfx_color_mem[slot].value = word;
    slot++;
  }

  while (x + 3 <= x1)
  {
    const uint32_t stamp_word = gfx_z_stamp_mem[slot].value;
    if (((stamp_word & 0xFFu) != frame_id) &&
        (((stamp_word >> 8) & 0xFFu) != frame_id) &&
        (((stamp_word >> 16) & 0xFFu) != frame_id) &&
        (((stamp_word >> 24) & 0xFFu) != frame_id))
    {
      uint32_t word = 0;
      uint32_t z32;
      uint32_t color;
      z32 = tri3d_z_from_float(zcur); gfx_z_mem[pixel].value = z32; color = tri3d_pack_rgb332_f(r, g, b); word |= color; r += drdx; g += dgdx; b += dbdx; zcur += dzdx; pixel++;
      z32 = tri3d_z_from_float(zcur); gfx_z_mem[pixel].value = z32; color = tri3d_pack_rgb332_f(r, g, b); word |= color << 8; r += drdx; g += dgdx; b += dbdx; zcur += dzdx; pixel++;
      z32 = tri3d_z_from_float(zcur); gfx_z_mem[pixel].value = z32; color = tri3d_pack_rgb332_f(r, g, b); word |= color << 16; r += drdx; g += dgdx; b += dbdx; zcur += dzdx; pixel++;
      z32 = tri3d_z_from_float(zcur); gfx_z_mem[pixel].value = z32; color = tri3d_pack_rgb332_f(r, g, b); word |= color << 24; r += drdx; g += dgdx; b += dbdx; zcur += dzdx; pixel++;
      gfx_z_stamp_mem[slot].value = stamp_word_full;
      gfx_color_mem[slot].value = word;
      tri3d_stat_add(TRI3D_STAT_FAST_UNTOUCHED_WORDS, 1);
      tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 4);
    }
    else if (stamp_word == stamp_word_full)
    {
      uint32_t word = 0;
      uint8_t dirty = 0;
      uint32_t z32;
      uint32_t color;

      z32 = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(z32, gfx_z_mem[pixel].value))
      {
        gfx_z_mem[pixel].value = z32;
        word = gfx_color_mem[slot].value;
        color = tri3d_pack_rgb332_f(r, g, b);
        word = (word & ~0x000000FFu) | color;
        dirty = 1;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      else tri3d_stat_add(TRI3D_STAT_Z_REJECTED, 1);
      r += drdx; g += dgdx; b += dbdx; zcur += dzdx; pixel++;

      z32 = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(z32, gfx_z_mem[pixel].value))
      {
        gfx_z_mem[pixel].value = z32;
        if (!dirty) word = gfx_color_mem[slot].value;
        color = tri3d_pack_rgb332_f(r, g, b);
        word = (word & ~0x0000FF00u) | (color << 8);
        dirty = 1;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      else tri3d_stat_add(TRI3D_STAT_Z_REJECTED, 1);
      r += drdx; g += dgdx; b += dbdx; zcur += dzdx; pixel++;

      z32 = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(z32, gfx_z_mem[pixel].value))
      {
        gfx_z_mem[pixel].value = z32;
        if (!dirty) word = gfx_color_mem[slot].value;
        color = tri3d_pack_rgb332_f(r, g, b);
        word = (word & ~0x00FF0000u) | (color << 16);
        dirty = 1;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      else tri3d_stat_add(TRI3D_STAT_Z_REJECTED, 1);
      r += drdx; g += dgdx; b += dbdx; zcur += dzdx; pixel++;

      z32 = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(z32, gfx_z_mem[pixel].value))
      {
        gfx_z_mem[pixel].value = z32;
        if (!dirty) word = gfx_color_mem[slot].value;
        color = tri3d_pack_rgb332_f(r, g, b);
        word = (word & ~0xFF000000u) | (color << 24);
        dirty = 1;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      else tri3d_stat_add(TRI3D_STAT_Z_REJECTED, 1);
      r += drdx; g += dgdx; b += dbdx; zcur += dzdx; pixel++;

      if (dirty) gfx_color_mem[slot].value = word;
      tri3d_stat_add(TRI3D_STAT_FAST_CURRENT_WORDS, 1);
    }
    else
    {
      uint32_t word = 0;
      uint8_t dirty = 0;
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        word = gfx_color_mem[slot].value;
        word = (word & ~0x000000FFu) | tri3d_pack_rgb332_f(r, g, b);
        dirty = 1;
      }
      r += drdx; g += dgdx; b += dbdx; zcur += dzdx; pixel++;
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0x0000FF00u) | ((uint32_t)tri3d_pack_rgb332_f(r, g, b) << 8);
        dirty = 1;
      }
      r += drdx; g += dgdx; b += dbdx; zcur += dzdx; pixel++;
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0x00FF0000u) | ((uint32_t)tri3d_pack_rgb332_f(r, g, b) << 16);
        dirty = 1;
      }
      r += drdx; g += dgdx; b += dbdx; zcur += dzdx; pixel++;
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0xFF000000u) | ((uint32_t)tri3d_pack_rgb332_f(r, g, b) << 24);
        dirty = 1;
      }
      r += drdx; g += dgdx; b += dbdx; zcur += dzdx; pixel++;
      if (dirty) gfx_color_mem[slot].value = word;
    }

    slot++;
    x += 4;
  }

  if (x <= x1)
  {
    uint32_t word = 0;
    uint8_t dirty = 0;
    lane = 0;
    while (x <= x1)
    {
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        const uint32_t shift = (uint32_t)lane * 8u;
        const uint32_t color = tri3d_pack_rgb332_f(r, g, b);
        word = (word & ~(0xFFu << shift)) | (color << shift);
        dirty = 1;
      }
      r += drdx; g += dgdx; b += dbdx; zcur += dzdx;
      lane++;
      x++;
      pixel++;
    }
    if (dirty) gfx_color_mem[slot].value = word;
  }
}

template <bool TRI3D_TEX_FAST_16_WRAP, bool TRI3D_TEX_FAST_CLAMPED = false>
static inline __attribute__((always_inline)) void tri3d_write_span_tex_z_body(var* gfx_color_mem,
                                                                          var* gfx_z_mem,
                                                                          var* gfx_z_stamp_mem,
                                                                          var* tex_mem,
                                                                          uint8_t frame_id,
                                                                          uint32_t stamp_word_full,
                                                                          uint32_t tex_base,
                                                                          int32_t tex_w,
                                                                          int32_t tex_h,
                                                                          uint32_t flags,
                                                                          int32_t y,
                                                                          int32_t x0,
                                                                          int32_t x1,
                                                                          float uq,
                                                                          float vq,
                                                                          float q,
                                                                          float duqdx,
                                                                          float dvqdx,
                                                                          float dqdx,
                                                                          float z,
                                                                          float dzdx)
{
  int32_t x = x0;
  float zcur = z;
  uint32_t pixel = (uint32_t)y * 320u + (uint32_t)x;
  uint32_t slot = pixel >> 2;
  int32_t lane = x & 3;
  uint32_t tex_cache_addr = 0xFFFFFFFFu;
  uint32_t tex_cache_word = 0;
  const uint32_t tex_w_mask = (uint32_t)tex_w - 1u;
  const uint32_t tex_h_mask = (uint32_t)tex_h - 1u;
  const uint8_t tex_w_pow2 = (uint8_t)((((uint32_t)tex_w) & tex_w_mask) == 0u);
  const uint8_t tex_h_pow2 = (uint8_t)((((uint32_t)tex_h) & tex_h_mask) == 0u);

  #define TRI3D_TEX_SAMPLE(uq_, vq_, q_) \
    (TRI3D_TEX_FAST_16_WRAP ? \
      tri3d_sample_tex_rgb332_16wrap_persp_cached(tex_mem, tex_base, (uq_), (vq_), (q_), tex_cache_addr, tex_cache_word) : \
    TRI3D_TEX_FAST_CLAMPED ? \
      tri3d_sample_tex_rgb332_clamped_persp_cached(tex_mem, tex_base, tex_w, tex_h, \
                                                  (uq_), (vq_), (q_), tex_cache_addr, tex_cache_word) : \
      tri3d_sample_tex_rgb332_persp_cached(tex_mem, tex_base, tex_w, tex_h, flags, \
                                           tex_w_mask, tex_h_mask, tex_w_pow2, tex_h_pow2, \
                                           (uq_), (vq_), (q_), tex_cache_addr, tex_cache_word))

  if (lane != 0)
  {
    uint32_t word = 0;
    uint8_t dirty = 0;
    while (lane < 4 && x <= x1)
    {
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        const uint32_t shift = (uint32_t)lane * 8u;
        const uint32_t color = TRI3D_TEX_SAMPLE(uq, vq, q);
        word = (word & ~(0xFFu << shift)) | (color << shift);
        dirty = 1;
      }
      uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx;
      lane++;
      x++;
      pixel++;
    }
    if (dirty) gfx_color_mem[slot].value = word;
    slot++;
  }

  while (x + 3 <= x1)
  {
    const uint32_t stamp_word = gfx_z_stamp_mem[slot].value;
    if (((stamp_word & 0xFFu) != frame_id) &&
        (((stamp_word >> 8) & 0xFFu) != frame_id) &&
        (((stamp_word >> 16) & 0xFFu) != frame_id) &&
        (((stamp_word >> 24) & 0xFFu) != frame_id))
    {
      uint32_t word = 0;
      uint32_t z32;
      uint32_t color;
      z32 = tri3d_z_from_float(zcur); gfx_z_mem[pixel].value = z32; color = TRI3D_TEX_SAMPLE(uq, vq, q); word |= color; uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx; pixel++;
      z32 = tri3d_z_from_float(zcur); gfx_z_mem[pixel].value = z32; color = TRI3D_TEX_SAMPLE(uq, vq, q); word |= color << 8; uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx; pixel++;
      z32 = tri3d_z_from_float(zcur); gfx_z_mem[pixel].value = z32; color = TRI3D_TEX_SAMPLE(uq, vq, q); word |= color << 16; uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx; pixel++;
      z32 = tri3d_z_from_float(zcur); gfx_z_mem[pixel].value = z32; color = TRI3D_TEX_SAMPLE(uq, vq, q); word |= color << 24; uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx; pixel++;
      gfx_z_stamp_mem[slot].value = stamp_word_full;
      gfx_color_mem[slot].value = word;
      tri3d_stat_add(TRI3D_STAT_FAST_UNTOUCHED_WORDS, 1);
      tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 4);
    }
    else if (stamp_word == stamp_word_full)
    {
      uint32_t word = 0;
      uint8_t dirty = 0;
      uint32_t z32;

      z32 = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(z32, gfx_z_mem[pixel].value))
      {
        gfx_z_mem[pixel].value = z32;
        word = gfx_color_mem[slot].value;
        word = (word & ~0x000000FFu) | TRI3D_TEX_SAMPLE(uq, vq, q);
        dirty = 1;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      else tri3d_stat_add(TRI3D_STAT_Z_REJECTED, 1);
      uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx; pixel++;

      z32 = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(z32, gfx_z_mem[pixel].value))
      {
        gfx_z_mem[pixel].value = z32;
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0x0000FF00u) | ((uint32_t)TRI3D_TEX_SAMPLE(uq, vq, q) << 8);
        dirty = 1;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      else tri3d_stat_add(TRI3D_STAT_Z_REJECTED, 1);
      uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx; pixel++;

      z32 = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(z32, gfx_z_mem[pixel].value))
      {
        gfx_z_mem[pixel].value = z32;
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0x00FF0000u) | ((uint32_t)TRI3D_TEX_SAMPLE(uq, vq, q) << 16);
        dirty = 1;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      else tri3d_stat_add(TRI3D_STAT_Z_REJECTED, 1);
      uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx; pixel++;

      z32 = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(z32, gfx_z_mem[pixel].value))
      {
        gfx_z_mem[pixel].value = z32;
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0xFF000000u) | ((uint32_t)TRI3D_TEX_SAMPLE(uq, vq, q) << 24);
        dirty = 1;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      else tri3d_stat_add(TRI3D_STAT_Z_REJECTED, 1);
      uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx; pixel++;

      if (dirty) gfx_color_mem[slot].value = word;
      tri3d_stat_add(TRI3D_STAT_FAST_CURRENT_WORDS, 1);
    }
    else
    {
      uint32_t word = 0;
      uint8_t dirty = 0;
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        word = gfx_color_mem[slot].value;
        word = (word & ~0x000000FFu) | TRI3D_TEX_SAMPLE(uq, vq, q);
        dirty = 1;
      }
      uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx; pixel++;
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0x0000FF00u) | ((uint32_t)TRI3D_TEX_SAMPLE(uq, vq, q) << 8);
        dirty = 1;
      }
      uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx; pixel++;
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0x00FF0000u) | ((uint32_t)TRI3D_TEX_SAMPLE(uq, vq, q) << 16);
        dirty = 1;
      }
      uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx; pixel++;
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        word = (word & ~0xFF000000u) | ((uint32_t)TRI3D_TEX_SAMPLE(uq, vq, q) << 24);
        dirty = 1;
      }
      uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx; pixel++;
      if (dirty) gfx_color_mem[slot].value = word;
    }

    slot++;
    x += 4;
  }

  if (x <= x1)
  {
    uint32_t word = 0;
    uint8_t dirty = 0;
    lane = 0;
    while (x <= x1)
    {
      if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur))
      {
        if (!dirty) word = gfx_color_mem[slot].value;
        const uint32_t shift = (uint32_t)lane * 8u;
        const uint32_t color = TRI3D_TEX_SAMPLE(uq, vq, q);
        word = (word & ~(0xFFu << shift)) | (color << shift);
        dirty = 1;
      }
      uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx;
      lane++;
      x++;
      pixel++;
    }
    if (dirty) gfx_color_mem[slot].value = word;
  }

  #undef TRI3D_TEX_SAMPLE
}

#define TRI3D_TEX_SPAN_ARGS \
    var* gfx_color_mem, var* gfx_z_mem, var* gfx_z_stamp_mem, var* tex_mem, \
    uint8_t frame_id, uint32_t stamp_word_full, uint32_t tex_base, \
    int32_t tex_w, int32_t tex_h, uint32_t flags, int32_t y, int32_t x0, int32_t x1, \
    float uq, float vq, float q, float duqdx, float dvqdx, float dqdx, float z, float dzdx

#define TRI3D_TEX_SPAN_PASS \
    gfx_color_mem, gfx_z_mem, gfx_z_stamp_mem, tex_mem, frame_id, stamp_word_full, \
    tex_base, tex_w, tex_h, flags, y, x0, x1, uq, vq, q, duqdx, dvqdx, dqdx, z, dzdx

// The fully general fallback is ~43 KiB of code. Keeping it in SRAM together
// with textured tiles can starve boot-time I2S/FS allocations on Core 1 and
// take the keyboard down before any program starts. Common clamped/16-wrap
// samplers and the new tile kernels stay in SRAM; this fallback executes the
// exact same implementation from flash.
static void __attribute__((section(".itcm"), noinline)) tri3d_write_span_tex_z_generic(TRI3D_TEX_SPAN_ARGS)
{
  tri3d_write_span_tex_z_body<false>(TRI3D_TEX_SPAN_PASS);
}

static void __attribute__((section(".time_critical.tri3d_write_span_tex_z_clamped"), noinline))
tri3d_write_span_tex_z_clamped(TRI3D_TEX_SPAN_ARGS)
{
  tri3d_write_span_tex_z_body<false, true>(TRI3D_TEX_SPAN_PASS);
}

// The linker recognizes .time_critical.* and copies this hot specialization
// to SRAM.  The former .itcm section is left in XIP flash by the
// Arduino-Pico linker script despite its name.
static void __attribute__((section(".time_critical.tri3d_write_span_tex_z_16wrap"), noinline))
tri3d_write_span_tex_z_16wrap(TRI3D_TEX_SPAN_ARGS)
{
  tri3d_write_span_tex_z_body<true>(TRI3D_TEX_SPAN_PASS);
}

#undef TRI3D_TEX_SPAN_PASS
#undef TRI3D_TEX_SPAN_ARGS

static void __attribute__((section(".itcm"))) tri3d_write_span_tex_z(var* gfx_color_mem,
                                                                     var* gfx_z_mem,
                                                                     var* gfx_z_stamp_mem,
                                                                     var* tex_mem,
                                                                     uint8_t frame_id,
                                                                     uint32_t stamp_word_full,
                                                                     uint32_t tex_base,
                                                                     int32_t tex_w,
                                                                     int32_t tex_h,
                                                                     uint32_t flags,
                                                                     int32_t y,
                                                                     int32_t x0,
                                                                     int32_t x1,
                                                                     float uq,
                                                                     float vq,
                                                                     float q,
                                                                     float duqdx,
                                                                     float dvqdx,
                                                                     float dqdx,
                                                                     float z,
                                                                     float dzdx,
                                                                     uint8_t fast_16_safe)
{
  if (fast_16_safe)
  {
    tri3d_sampler_span(2u, x0, x1);
    tri3d_write_span_tex_z_16wrap(gfx_color_mem, gfx_z_mem, gfx_z_stamp_mem, tex_mem,
                                  frame_id, stamp_word_full, tex_base, tex_w, tex_h,
                                  flags, y, x0, x1, uq, vq, q, duqdx, dvqdx, dqdx,
                                  z, dzdx);
    return;
  }

  // Select once per span, not per pixel. Wrapped/mixed addressing and very
  // large dimensions keep the original fully general conversion path.
  if (tri3d_clamped_sampler_supported(tex_w, tex_h, flags))
  {
    if (tri3d_stats_enabled && x1 >= x0)
    {
      ++tri3d_sampler_stats[8];
      tri3d_sampler_stats[9] += (uint32_t)x1 - (uint32_t)x0 + 1u;
    }
    if (!tri3d_force_general_sampler)
    {
      tri3d_sampler_span(1u, x0, x1);
      tri3d_write_span_tex_z_clamped(gfx_color_mem, gfx_z_mem, gfx_z_stamp_mem, tex_mem,
                                    frame_id, stamp_word_full, tex_base, tex_w, tex_h,
                                    flags, y, x0, x1, uq, vq, q, duqdx, dvqdx, dqdx,
                                    z, dzdx);
      return;
    }
  }

  tri3d_sampler_span(0u, x0, x1);
  tri3d_write_span_tex_z_generic(gfx_color_mem, gfx_z_mem, gfx_z_stamp_mem, tex_mem,
                                 frame_id, stamp_word_full, tex_base, tex_w, tex_h,
                                 flags, y, x0, x1, uq, vq, q, duqdx, dvqdx, dqdx,
                                 z, dzdx);
}

template <bool TRI3D_MASK_FAST_16_WRAP>
static void __attribute__((section(".itcm"))) tri3d_write_span_tex_z_mask_impl(var* gfx_color_mem,
                                                                               var* gfx_z_mem,
                                                                               var* gfx_z_stamp_mem,
                                                                               var* tex_mem,
                                                                               var* mask_mem,
                                                                               uint8_t frame_id,
                                                                               uint32_t tex_base,
                                                                               uint32_t mask_base,
                                                                               int32_t tex_w,
                                                                               int32_t tex_h,
                                                                               uint32_t flags,
                                                                               int32_t y,
                                                                               int32_t x0,
                                                                               int32_t x1,
                                                                               float uq,
                                                                               float vq,
                                                                               float q,
                                                                               float duqdx,
                                                                               float dvqdx,
                                                                               float dqdx,
                                                                               float z,
                                                                               float dzdx)
{
  int32_t x = x0;
  float zcur = z;
  uint32_t pixel = (uint32_t)y * 320u + (uint32_t)x;
  uint32_t slot = pixel >> 2;
  int32_t lane = x & 3;

  uint32_t tex_cache_addr = 0xFFFFFFFFu;
  uint32_t tex_cache_word = 0;
  uint32_t mask_cache_addr = 0xFFFFFFFFu;
  uint32_t mask_cache_word = 0;

  const uint32_t tex_w_mask = (uint32_t)tex_w - 1u;
  const uint32_t tex_h_mask = (uint32_t)tex_h - 1u;
  const uint8_t tex_w_pow2 = (uint8_t)((((uint32_t)tex_w) & tex_w_mask) == 0u);
  const uint8_t tex_h_pow2 = (uint8_t)((((uint32_t)tex_h) & tex_h_mask) == 0u);

  #define TRI3D_MASK_TEXEL(out_texel_, uq_, vq_, q_) \
    (TRI3D_MASK_FAST_16_WRAP ? \
      tri3d_texel_index_16wrap_persp((out_texel_), (uq_), (vq_), (q_)) : \
      tri3d_texel_index_persp((out_texel_), tex_w, tex_h, flags, \
                              tex_w_mask, tex_h_mask, tex_w_pow2, tex_h_pow2, \
                              (uq_), (vq_), (q_)))

  #define TRI3D_MASK_STEP_PIXEL(byte_shift_) do { \
      uint32_t __texel = 0; \
      if (TRI3D_MASK_TEXEL(__texel, uq, vq, q) && \
          tri3d_mask_texel_draws(mask_mem, mask_base, __texel, mask_cache_addr, mask_cache_word)) { \
        if (tri3d_z_accept(gfx_z_mem, gfx_z_stamp_mem, frame_id, pixel, zcur)) { \
          if (!dirty) word = gfx_color_mem[slot].value; \
          const uint32_t __color = tri3d_sample_texel_rgb332_cached(tex_mem, tex_base, __texel, tex_cache_addr, tex_cache_word); \
          word = (word & ~(0xFFu << (byte_shift_))) | (__color << (byte_shift_)); \
          dirty = 1; \
        } \
      } \
      uq += duqdx; vq += dvqdx; q += dqdx; zcur += dzdx; \
      x++; pixel++; \
    } while (0)

  if (lane != 0)
  {
    uint32_t word = 0;
    uint8_t dirty = 0;
    while (lane < 4 && x <= x1)
    {
      TRI3D_MASK_STEP_PIXEL((uint32_t)lane * 8u);
      lane++;
    }
    if (dirty) gfx_color_mem[slot].value = word;
    slot++;
  }

  while (x + 3 <= x1)
  {
    uint32_t word = 0;
    uint8_t dirty = 0;
    TRI3D_MASK_STEP_PIXEL(0u);
    TRI3D_MASK_STEP_PIXEL(8u);
    TRI3D_MASK_STEP_PIXEL(16u);
    TRI3D_MASK_STEP_PIXEL(24u);
    if (dirty) gfx_color_mem[slot].value = word;
    slot++;
  }

  if (x <= x1)
  {
    uint32_t word = 0;
    uint8_t dirty = 0;
    lane = 0;
    while (x <= x1)
    {
      TRI3D_MASK_STEP_PIXEL((uint32_t)lane * 8u);
      lane++;
    }
    if (dirty) gfx_color_mem[slot].value = word;
  }

  #undef TRI3D_MASK_STEP_PIXEL
  #undef TRI3D_MASK_TEXEL
}

static void __attribute__((section(".itcm"))) tri3d_write_span_tex_z_mask(var* gfx_color_mem,
                                                                          var* gfx_z_mem,
                                                                          var* gfx_z_stamp_mem,
                                                                          var* tex_mem,
                                                                          var* mask_mem,
                                                                          uint8_t frame_id,
                                                                          uint32_t tex_base,
                                                                          uint32_t mask_base,
                                                                          int32_t tex_w,
                                                                          int32_t tex_h,
                                                                          uint32_t flags,
                                                                          int32_t y,
                                                                          int32_t x0,
                                                                          int32_t x1,
                                                                          float uq,
                                                                          float vq,
                                                                          float q,
                                                                          float duqdx,
                                                                          float dvqdx,
                                                                          float dqdx,
                                                                          float z,
                                                                          float dzdx)
{
  tri3d_sampler_span(3u, x0, x1);
  if (tex_w == 16 && tex_h == 16 && ((flags & 0x3u) == 0x3u))
  {
    tri3d_write_span_tex_z_mask_impl<true>(gfx_color_mem, gfx_z_mem, gfx_z_stamp_mem,
                                           tex_mem, mask_mem, frame_id, tex_base, mask_base,
                                           tex_w, tex_h, flags, y, x0, x1,
                                           uq, vq, q, duqdx, dvqdx, dqdx, z, dzdx);
    return;
  }

  tri3d_write_span_tex_z_mask_impl<false>(gfx_color_mem, gfx_z_mem, gfx_z_stamp_mem,
                                          tex_mem, mask_mem, frame_id, tex_base, mask_base,
                                          tex_w, tex_h, flags, y, x0, x1,
                                          uq, vq, q, duqdx, dvqdx, dqdx, z, dzdx);
}

// Solid triangles are accumulated as prepared raster commands.  At a flush,
// both cores own disjoint 16-row screen tiles in the otherwise-unused 4-bit
// terminal framebuffer. This legacy workspace is PSRAM (mms.cpp), not SRAM.
#define TRI3D_TILE_ROWS 16u
#define TRI3D_TILE_PIXELS (320u * TRI3D_TILE_ROWS)
#define TRI3D_TILE_COUNT ((240u + TRI3D_TILE_ROWS - 1u) / TRI3D_TILE_ROWS)
#define TRI3D_BATCH_MAX_COMMANDS 704u
#define TRI3D_IMMEDIATE_MAX_COMMANDS 64u
#define TRI3D_IMMEDIATE_MAX_BOUNDS_PIXELS 4096u

struct Tri3dSolidRasterParams
{
  var* gfx_color_mem;
  var* gfx_z_mem;
  var* gfx_z_stamp_mem;
  uint8_t frame_id;
  uint32_t color;
  uint32_t packed_color;
  uint32_t stamp_word_full;

  float x0;
  float y0;
  float z0;
  float dzdx;
  float dzdy;

  int32_t scan_y0;
  int32_t scan_y1;
  int32_t scan_y2;
  int64_t upper_xa;
  int64_t upper_xb;
  int64_t lower_xa;
  int64_t lower_xb;
  int64_t dx01;
  int64_t dx02;
  int64_t dx12;
};

struct Tri3dSolidRasterCommand
{
  Tri3dSolidRasterParams params;
  uint16_t raster_x_start;
  uint16_t raster_x_end;
  uint16_t raster_y_start;
  uint16_t raster_y_end;
};

struct Tri3dSolidBatchJob
{
  Tri3dSolidRasterCommand* commands;
  var* gfx_color_mem;
  var* gfx_z_mem;
  var* gfx_z_stamp_mem;
  uint32_t command_count;
  uint8_t frame_id;
  uint8_t depth_pristine;
};

enum
{
  TRI3D_CORE1_IDLE = 0,
  TRI3D_CORE1_READY = 1,
  TRI3D_CORE1_ACTIVE = 2,
  TRI3D_CORE1_DONE = 3
};

static Tri3dSolidBatchJob tri3d_solid_batch_job;
static uint32_t tri3d_core1_job_state = TRI3D_CORE1_IDLE;
static uint32_t tri3d_core1_online = 0;
static uint32_t tri3d_solid_batch_count = 0;
static var* tri3d_solid_batch_color_mem = nullptr;
static var* tri3d_solid_batch_z_mem = nullptr;
static var* tri3d_solid_batch_stamp_mem = nullptr;
static uint8_t tri3d_solid_batch_frame_id = 0;
static uint8_t tri3d_depth_pristine = 0;
static uint8_t tri3d_depth_pristine_frame_id = 0;
// Enabled by the half-resolution clear instruction.  Pure VGA4 programs do
// not execute that instruction, so their 4-bit framebuffer is never borrowed.
static uint8_t tri3d_tile_scratch_enabled = 0;

void tri3d_set_tile_scratch_enabled(bool enabled)
{
  tri3d_tile_scratch_enabled = enabled ? 1u : 0u;
}

static inline __attribute__((always_inline)) void tri3d_raster_solid_half_rows(
  const Tri3dSolidRasterParams* params,
  int32_t requested_start,
  int32_t requested_end,
  int32_t half_start,
  int32_t half_end,
  int64_t xa_at_half_start,
  int64_t dxa,
  int64_t xb_at_half_start,
  int64_t dxb)
{
  int32_t y_start = requested_start > half_start ? requested_start : half_start;
  int32_t y_end = requested_end < half_end ? requested_end : half_end;
  if (y_start < 0) y_start = 0;
  if (y_end > 240) y_end = 240;
  if (y_start >= y_end) return;

  int64_t xa = xa_at_half_start + (dxa * (int64_t)(y_start - half_start));
  int64_t xb = xb_at_half_start + (dxb * (int64_t)(y_start - half_start));
  for (int32_t y = y_start; y < y_end; y++)
  {
    const int64_t min_edge = xa < xb ? xa : xb;
    const int64_t max_edge = xa < xb ? xb : xa;
    const int32_t left = tri3d_edge_fx_ceil(min_edge - TRI3D_EDGE_FX_HALF);
    const int32_t right = tri3d_edge_fx_ceil(max_edge - TRI3D_EDGE_FX_HALF) - 1;
    if (right >= 0 && left < 320)
    {
      const int32_t draw_left = left < 0 ? 0 : left;
      const int32_t draw_right = right >= 320 ? 319 : right;
      const float dx = ((float)draw_left + 0.5f) - params->x0;
      const float dy = ((float)y + 0.5f) - params->y0;
      const float z = params->z0 + (params->dzdx * dx) + (params->dzdy * dy);
      tri3d_write_span_z(params->gfx_color_mem,
                         params->gfx_z_mem,
                         params->gfx_z_stamp_mem,
                         params->frame_id,
                         params->color,
                         params->packed_color,
                         params->stamp_word_full,
                         y,
                         draw_left,
                         draw_right,
                         z,
                         params->dzdx);
    }
    xa += dxa;
    xb += dxb;
  }
}

static void __attribute__((section(".itcm"), noinline)) tri3d_raster_solid_rows(
  const Tri3dSolidRasterParams* params,
  int32_t y_start,
  int32_t y_end)
{
  if (params->scan_y1 > params->scan_y0)
  {
    tri3d_raster_solid_half_rows(params,
                                 y_start, y_end,
                                 params->scan_y0, params->scan_y1,
                                 params->upper_xa, params->dx02,
                                 params->upper_xb, params->dx01);
  }
  if (params->scan_y2 > params->scan_y1)
  {
    tri3d_raster_solid_half_rows(params,
                                 y_start, y_end,
                                 params->scan_y1, params->scan_y2,
                                 params->lower_xa, params->dx02,
                                 params->lower_xb, params->dx12);
  }
}

static inline __attribute__((always_inline)) uint32_t* tri3d_tile_z_scratch(uint32_t core_index)
{
  const uintptr_t base = (uintptr_t)vga_640_480_4_01;
  const uintptr_t per_core = (TRI3D_TILE_PIXELS * sizeof(uint32_t)) +
                             (2u * TRI3D_TILE_PIXELS);
  return (uint32_t*)(base + (core_index * per_core));
}

static inline __attribute__((always_inline)) uint8_t* tri3d_tile_dirty_scratch(uint32_t core_index)
{
  return (uint8_t*)(tri3d_tile_z_scratch(core_index) + TRI3D_TILE_PIXELS);
}

static inline __attribute__((always_inline)) uint8_t* tri3d_tile_color_scratch(uint32_t core_index)
{
  return tri3d_tile_dirty_scratch(core_index) + TRI3D_TILE_PIXELS;
}

static inline __attribute__((always_inline)) Tri3dSolidRasterCommand* tri3d_solid_command_buffer()
{
  const uintptr_t base = (uintptr_t)vga_640_480_4_01;
  const uintptr_t per_core = (TRI3D_TILE_PIXELS * sizeof(uint32_t)) +
                             (2u * TRI3D_TILE_PIXELS);
  const uintptr_t after_tiles = base + (2u * per_core);
  const uintptr_t aligned = (after_tiles + (alignof(Tri3dSolidRasterCommand) - 1u)) &
                            ~(uintptr_t)(alignof(Tri3dSolidRasterCommand) - 1u);
  return (Tri3dSolidRasterCommand*)aligned;
}

static_assert(
  (2u * ((TRI3D_TILE_PIXELS * sizeof(uint32_t)) + (2u * TRI3D_TILE_PIXELS))) +
  (alignof(Tri3dSolidRasterCommand) - 1u) +
  (TRI3D_BATCH_MAX_COMMANDS * sizeof(Tri3dSolidRasterCommand)) <= VGA_4BIT_BUFFER_SIZE,
  "solid tile renderer exceeds the graphics-mode SRAM scratch buffer");

static void __attribute__((section(".time_critical.tri3d_write_span_tile_z"), noinline))
tri3d_write_span_tile_z(const Tri3dSolidRasterParams* params,
                        uint32_t* tile_z,
                        uint8_t* tile_dirty,
                        uint8_t* tile_color,
                        int32_t tile_y0,
                        int32_t y,
                        int32_t x0,
                        int32_t x1,
                        float z,
                        float dzdx)
{
  int32_t x = x0;
  float zcur = z;
  uint32_t local_pixel = (uint32_t)(y - tile_y0) * 320u + (uint32_t)x;

  if ((x & 3) != 0)
  {
    while ((x & 3) != 0 && x <= x1)
    {
      const uint32_t new_z = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(new_z, tile_z[local_pixel]))
      {
        tile_z[local_pixel] = new_z;
        tile_dirty[local_pixel] = 1;
        tile_color[local_pixel] = (uint8_t)params->color;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      zcur += dzdx;
      local_pixel++;
      x++;
    }
  }

  while (x + 3 <= x1)
  {
    const uint32_t group_local = local_pixel;
    uint8_t accepted = 0;
    uint32_t new_z;
    new_z = tri3d_z_from_float(zcur);
    tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
    if (tri3d_z_nearer(new_z, tile_z[local_pixel]))
    {
      tile_z[local_pixel] = new_z; tile_dirty[local_pixel] = 1; accepted |= 0x1u;
      tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
    }
    zcur += dzdx; local_pixel++;
    new_z = tri3d_z_from_float(zcur);
    tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
    if (tri3d_z_nearer(new_z, tile_z[local_pixel]))
    {
      tile_z[local_pixel] = new_z; tile_dirty[local_pixel] = 1; accepted |= 0x2u;
      tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
    }
    zcur += dzdx; local_pixel++;
    new_z = tri3d_z_from_float(zcur);
    tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
    if (tri3d_z_nearer(new_z, tile_z[local_pixel]))
    {
      tile_z[local_pixel] = new_z; tile_dirty[local_pixel] = 1; accepted |= 0x4u;
      tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
    }
    zcur += dzdx; local_pixel++;
    new_z = tri3d_z_from_float(zcur);
    tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
    if (tri3d_z_nearer(new_z, tile_z[local_pixel]))
    {
      tile_z[local_pixel] = new_z; tile_dirty[local_pixel] = 1; accepted |= 0x8u;
      tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
    }
    zcur += dzdx; local_pixel++;

    if (accepted == 0xFu)
    {
      memcpy(&tile_color[group_local], &params->packed_color, sizeof(uint32_t));
    }
    else if (accepted != 0)
    {
      if (accepted & 0x1u) tile_color[group_local + 0] = (uint8_t)params->color;
      if (accepted & 0x2u) tile_color[group_local + 1] = (uint8_t)params->color;
      if (accepted & 0x4u) tile_color[group_local + 2] = (uint8_t)params->color;
      if (accepted & 0x8u) tile_color[group_local + 3] = (uint8_t)params->color;
    }
    x += 4;
  }

  if (x <= x1)
  {
    while (x <= x1)
    {
      const uint32_t new_z = tri3d_z_from_float(zcur);
      tri3d_stat_add(TRI3D_STAT_PIXELS_TESTED, 1);
      if (tri3d_z_nearer(new_z, tile_z[local_pixel]))
      {
        tile_z[local_pixel] = new_z;
        tile_dirty[local_pixel] = 1;
        tile_color[local_pixel] = (uint8_t)params->color;
        tri3d_stat_add(TRI3D_STAT_PIXELS_WRITTEN, 1);
      }
      zcur += dzdx;
      local_pixel++;
      x++;
    }
  }
}

static inline __attribute__((always_inline)) void tri3d_raster_solid_tile_half_rows(
  const Tri3dSolidRasterParams* params,
  uint32_t* tile_z,
  uint8_t* tile_dirty,
  uint8_t* tile_color,
  int32_t tile_y0,
  int32_t requested_start,
  int32_t requested_end,
  int32_t half_start,
  int32_t half_end,
  int64_t xa_at_half_start,
  int64_t dxa,
  int64_t xb_at_half_start,
  int64_t dxb)
{
  int32_t y_start = requested_start > half_start ? requested_start : half_start;
  int32_t y_end = requested_end < half_end ? requested_end : half_end;
  if (y_start >= y_end) return;
  int64_t xa = xa_at_half_start + (dxa * (int64_t)(y_start - half_start));
  int64_t xb = xb_at_half_start + (dxb * (int64_t)(y_start - half_start));
  for (int32_t y = y_start; y < y_end; y++)
  {
    const int64_t min_edge = xa < xb ? xa : xb;
    const int64_t max_edge = xa < xb ? xb : xa;
    const int32_t left = tri3d_edge_fx_ceil(min_edge - TRI3D_EDGE_FX_HALF);
    const int32_t right = tri3d_edge_fx_ceil(max_edge - TRI3D_EDGE_FX_HALF) - 1;
    if (right >= 0 && left < 320)
    {
      const int32_t draw_left = left < 0 ? 0 : left;
      const int32_t draw_right = right >= 320 ? 319 : right;
      const float dx = ((float)draw_left + 0.5f) - params->x0;
      const float dy = ((float)y + 0.5f) - params->y0;
      const float z = params->z0 + (params->dzdx * dx) + (params->dzdy * dy);
      tri3d_write_span_tile_z(params, tile_z, tile_dirty, tile_color, tile_y0,
                              y, draw_left, draw_right, z, params->dzdx);
    }
    xa += dxa;
    xb += dxb;
  }
}

static void __attribute__((section(".time_critical.tri3d_raster_solid_tile_rows"), noinline))
tri3d_raster_solid_tile_rows(const Tri3dSolidRasterParams* params,
                             uint32_t* tile_z,
                             uint8_t* tile_dirty,
                             uint8_t* tile_color,
                             int32_t tile_y0,
                             int32_t y_start,
                             int32_t y_end)
{
  if (params->scan_y1 > params->scan_y0)
  {
    tri3d_raster_solid_tile_half_rows(params, tile_z, tile_dirty, tile_color, tile_y0,
      y_start, y_end, params->scan_y0, params->scan_y1,
      params->upper_xa, params->dx02, params->upper_xb, params->dx01);
  }
  if (params->scan_y2 > params->scan_y1)
  {
    tri3d_raster_solid_tile_half_rows(params, tile_z, tile_dirty, tile_color, tile_y0,
      y_start, y_end, params->scan_y1, params->scan_y2,
      params->lower_xa, params->dx02, params->lower_xb, params->dx12);
  }
}

static void __attribute__((section(".time_critical.tri3d_prepare_tile_depth"), noinline))
tri3d_prepare_tile_depth(const Tri3dSolidBatchJob* job,
                         uint32_t* tile_z,
                         uint8_t* tile_dirty,
                         uint32_t tile_y0,
                         uint32_t work_y0,
                         uint32_t work_y1,
                         uint32_t work_x0,
                         uint32_t work_x1)
{
  const uint32_t row_width = work_x1 - work_x0;
  if (job->depth_pristine)
  {
    for (uint32_t y = work_y0; y < work_y1; y++)
    {
      const uint32_t local = (y - tile_y0) * 320u + work_x0;
      memset(&tile_dirty[local], 0, row_width);
      memset(&tile_z[local], 0xFF, row_width * sizeof(uint32_t));
    }
    return;
  }

  const uint32_t word_count = row_width >> 2;
  for (uint32_t y = work_y0; y < work_y1; y++)
  {
    uint32_t local = (y - tile_y0) * 320u + work_x0;
    memset(&tile_dirty[local], 0, row_width);
    uint32_t pixel = y * 320u + work_x0;
    uint32_t slot = pixel >> 2;
    for (uint32_t word = 0; word < word_count; word++)
    {
      const uint32_t stamps = job->gfx_z_stamp_mem[slot++].value;
      tile_z[local++] = ((stamps & 0xFFu) == job->frame_id) ? job->gfx_z_mem[pixel].value : 0xFFFFFFFFu; pixel++;
      tile_z[local++] = (((stamps >> 8) & 0xFFu) == job->frame_id) ? job->gfx_z_mem[pixel].value : 0xFFFFFFFFu; pixel++;
      tile_z[local++] = (((stamps >> 16) & 0xFFu) == job->frame_id) ? job->gfx_z_mem[pixel].value : 0xFFFFFFFFu; pixel++;
      tile_z[local++] = (((stamps >> 24) & 0xFFu) == job->frame_id) ? job->gfx_z_mem[pixel].value : 0xFFFFFFFFu; pixel++;
    }
  }
}

static void __attribute__((section(".time_critical.tri3d_commit_tile_depth"), noinline))
tri3d_commit_tile_depth(const Tri3dSolidBatchJob* job,
                        const uint32_t* tile_z,
                        const uint8_t* tile_dirty,
                        const uint8_t* tile_color,
                        uint32_t tile_y0,
                        uint32_t work_y0,
                        uint32_t work_y1,
                        uint32_t work_x0,
                        uint32_t work_x1,
                        uint8_t service_keyboard)
{
  const uint32_t word_count = (work_x1 - work_x0) >> 2;
  for (uint32_t y = work_y0; y < work_y1; y++)
  {
    uint32_t local = (y - tile_y0) * 320u + work_x0;
    uint32_t pixel = y * 320u + work_x0;
    uint32_t slot = pixel >> 2;
    for (uint32_t word = 0; word < word_count; word++)
    {
      uint8_t mask = 0;
      if (tile_dirty[local + 0]) mask |= 0x1u;
      if (tile_dirty[local + 1]) mask |= 0x2u;
      if (tile_dirty[local + 2]) mask |= 0x4u;
      if (tile_dirty[local + 3]) mask |= 0x8u;
      if (mask)
      {
        uint32_t stamps = job->depth_pristine ? 0u : job->gfx_z_stamp_mem[slot].value;
        if (mask & 0x1u) { job->gfx_z_mem[pixel + 0].value = tile_z[local + 0]; stamps = (stamps & ~0x000000FFu) | job->frame_id; }
        if (mask & 0x2u) { job->gfx_z_mem[pixel + 1].value = tile_z[local + 1]; stamps = (stamps & ~0x0000FF00u) | ((uint32_t)job->frame_id << 8); }
        if (mask & 0x4u) { job->gfx_z_mem[pixel + 2].value = tile_z[local + 2]; stamps = (stamps & ~0x00FF0000u) | ((uint32_t)job->frame_id << 16); }
        if (mask & 0x8u) { job->gfx_z_mem[pixel + 3].value = tile_z[local + 3]; stamps = (stamps & ~0xFF000000u) | ((uint32_t)job->frame_id << 24); }
        job->gfx_z_stamp_mem[slot].value = stamps;

        uint32_t colors;
        if (mask == 0xFu)
        {
          memcpy(&colors, &tile_color[local], sizeof(uint32_t));
        }
        else
        {
          colors = job->gfx_color_mem[slot].value;
          if (mask & 0x1u) colors = (colors & ~0x000000FFu) | tile_color[local + 0];
          if (mask & 0x2u) colors = (colors & ~0x0000FF00u) | ((uint32_t)tile_color[local + 1] << 8);
          if (mask & 0x4u) colors = (colors & ~0x00FF0000u) | ((uint32_t)tile_color[local + 2] << 16);
          if (mask & 0x8u) colors = (colors & ~0xFF000000u) | ((uint32_t)tile_color[local + 3] << 24);
        }
        job->gfx_color_mem[slot].value = colors;
      }
      local += 4;
      pixel += 4;
      slot++;
    }
    if (service_keyboard && ((y & 3u) == 3u)) ps2_keyboard_poll();
  }
}

static void __attribute__((section(".itcm"))) tri3d_process_solid_tiles(uint32_t core_index,
                                                                         uint32_t first_tile,
                                                                         uint32_t tile_stride,
                                                                         uint8_t service_keyboard)
{
  uint32_t* tile_z = tri3d_tile_z_scratch(core_index);
  uint8_t* tile_dirty = tri3d_tile_dirty_scratch(core_index);
  uint8_t* tile_color = tri3d_tile_color_scratch(core_index);
  const Tri3dSolidBatchJob* job = &tri3d_solid_batch_job;
  for (uint32_t tile = first_tile; tile < TRI3D_TILE_COUNT; tile += tile_stride)
  {
    const uint32_t tile_y0 = tile * TRI3D_TILE_ROWS;
    uint32_t tile_y1 = tile_y0 + TRI3D_TILE_ROWS;
    if (tile_y1 > 240u) tile_y1 = 240u;

    uint32_t work_x0 = 320u;
    uint32_t work_x1 = 0u;
    uint32_t work_y0 = tile_y1;
    uint32_t work_y1 = tile_y0;
    for (uint32_t i = 0; i < job->command_count; i++)
    {
      const Tri3dSolidRasterCommand* command = &job->commands[i];
      if (command->raster_y_end > (int32_t)tile_y0 && command->raster_y_start < (int32_t)tile_y1)
      {
        const uint32_t command_y0 = command->raster_y_start > tile_y0
          ? command->raster_y_start : tile_y0;
        const uint32_t command_y1 = command->raster_y_end < tile_y1
          ? command->raster_y_end : tile_y1;
        if (command->raster_x_start < work_x0) work_x0 = command->raster_x_start;
        if (command->raster_x_end > work_x1) work_x1 = command->raster_x_end;
        if (command_y0 < work_y0) work_y0 = command_y0;
        if (command_y1 > work_y1) work_y1 = command_y1;
      }
    }
    if (work_x0 >= work_x1 || work_y0 >= work_y1) continue;

    // Depth stamps and packed RGB332 colors are word-addressed.  Widen the
    // aggregate horizontal bounds to complete words while retaining exact row
    // bounds.  Pixels outside the command spans remain clean and are not
    // committed.
    work_x0 &= ~3u;
    work_x1 = (work_x1 + 3u) & ~3u;
    if (work_x1 > 320u) work_x1 = 320u;

    tri3d_prepare_tile_depth(job, tile_z, tile_dirty, tile_y0,
                             work_y0, work_y1, work_x0, work_x1);
    for (uint32_t i = 0; i < job->command_count; i++)
    {
      const Tri3dSolidRasterCommand* command = &job->commands[i];
      int32_t y_start = command->raster_y_start > (int32_t)tile_y0 ? command->raster_y_start : (int32_t)tile_y0;
      int32_t y_end = command->raster_y_end < (int32_t)tile_y1 ? command->raster_y_end : (int32_t)tile_y1;
      if (y_start < y_end)
      {
        tri3d_raster_solid_tile_rows(&command->params, tile_z, tile_dirty, tile_color,
                                     (int32_t)tile_y0, y_start, y_end);
      }
      if (service_keyboard) ps2_keyboard_poll();
    }
    tri3d_commit_tile_depth(job, tile_z, tile_dirty, tile_color,
                            tile_y0, work_y0, work_y1, work_x0, work_x1,
                            service_keyboard);
  }
}

static uint8_t tri3d_solid_batch_prefers_immediate(const Tri3dSolidRasterCommand* commands,
                                                    uint32_t command_count)
{
  if (command_count <= 8u) return 1u;

  uint32_t batch_x0 = 320u;
  uint32_t batch_x1 = 0u;
  uint32_t batch_y0 = 240u;
  uint32_t batch_y1 = 0u;
  uint64_t command_bounds_pixels = 0u;
  for (uint32_t i = 0; i < command_count; i++)
  {
    const Tri3dSolidRasterCommand* command = &commands[i];
    const uint32_t width = command->raster_x_end - command->raster_x_start;
    const uint32_t height = command->raster_y_end - command->raster_y_start;
    command_bounds_pixels += (uint64_t)width * height;
    if (command->raster_x_start < batch_x0) batch_x0 = command->raster_x_start;
    if (command->raster_x_end > batch_x1) batch_x1 = command->raster_x_end;
    if (command->raster_y_start < batch_y0) batch_y0 = command->raster_y_start;
    if (command->raster_y_end > batch_y1) batch_y1 = command->raster_y_end;
  }

  const uint64_t batch_bounds_pixels =
    (uint64_t)(batch_x1 - batch_x0) * (batch_y1 - batch_y0);
  if (batch_bounds_pixels <= TRI3D_IMMEDIATE_MAX_BOUNDS_PIXELS) return 1u;

  // Immediate spans avoid tile setup for small, sparse batches.  Dense or
  // overlapping batches retain the dual-core tiled path, where depth reuse
  // amortizes the staging cost.
  return (uint8_t)(command_count <= TRI3D_IMMEDIATE_MAX_COMMANDS &&
                   command_bounds_pixels <= (batch_bounds_pixels * 2u));
}

void tri3d_flush_solid_batch()
{
  if (tri3d_solid_batch_count == 0) return;

  Tri3dSolidRasterCommand* commands = tri3d_solid_command_buffer();
  tri3d_solid_batch_job.commands = commands;
  tri3d_solid_batch_job.gfx_color_mem = tri3d_solid_batch_color_mem;
  tri3d_solid_batch_job.gfx_z_mem = tri3d_solid_batch_z_mem;
  tri3d_solid_batch_job.gfx_z_stamp_mem = tri3d_solid_batch_stamp_mem;
  tri3d_solid_batch_job.command_count = tri3d_solid_batch_count;
  tri3d_solid_batch_job.frame_id = tri3d_solid_batch_frame_id;
  tri3d_solid_batch_job.depth_pristine = (uint8_t)
    (tri3d_depth_pristine && tri3d_depth_pristine_frame_id == tri3d_solid_batch_frame_id);

  if (tri3d_solid_batch_prefers_immediate(commands, tri3d_solid_batch_count))
  {
    for (uint32_t i = 0; i < tri3d_solid_batch_count; i++)
    {
      const Tri3dSolidRasterCommand* command = &commands[i];
      tri3d_raster_solid_rows(&command->params,
                              command->raster_y_start,
                              command->raster_y_end);
    }
    tri3d_depth_pristine = 0;
    tri3d_solid_batch_count = 0;
    return;
  }

  if (__atomic_load_n(&tri3d_core1_online, __ATOMIC_ACQUIRE) != 0u)
  {
    __atomic_store_n(&tri3d_core1_job_state, TRI3D_CORE1_READY, __ATOMIC_RELEASE);
    tri3d_process_solid_tiles(0u, 0u, 2u, 0u);
    while (__atomic_load_n(&tri3d_core1_job_state, __ATOMIC_ACQUIRE) != TRI3D_CORE1_DONE)
    {
      __asm volatile ("nop");
    }
    __atomic_store_n(&tri3d_core1_job_state, TRI3D_CORE1_IDLE, __ATOMIC_RELEASE);
  }
  else
  {
    tri3d_process_solid_tiles(0u, 0u, 1u, 0u);
  }

  tri3d_depth_pristine = 0;
  tri3d_solid_batch_count = 0;
}

void tri3d_mark_depth_pristine(uint8_t frame_id)
{
  tri3d_occlusion_invalidate();
  tri3d_depth_pristine = 1;
  tri3d_depth_pristine_frame_id = frame_id;
}

void tri3d_mark_depth_non_pristine()
{
  tri3d_depth_pristine = 0;
}

static void tri3d_queue_solid_command(const Tri3dSolidRasterParams* params,
                                      int32_t raster_x_start,
                                      int32_t raster_x_end,
                                      int32_t raster_y_start,
                                      int32_t raster_y_end)
{
  // OP_CLRBUFF owns the mode transition for this scratch area.  Do not gate
  // batching on graphics_mode: the current firmware never raises that legacy
  // flag while half-resolution graphics are running.
  if (!tri3d_tile_scratch_enabled)
  {
    tri3d_raster_solid_rows(params, raster_y_start, raster_y_end);
    tri3d_mark_depth_non_pristine();
    return;
  }

  if (tri3d_solid_batch_count != 0 &&
      (tri3d_solid_batch_color_mem != params->gfx_color_mem ||
       tri3d_solid_batch_z_mem != params->gfx_z_mem ||
       tri3d_solid_batch_stamp_mem != params->gfx_z_stamp_mem ||
       tri3d_solid_batch_frame_id != params->frame_id))
  {
    tri3d_flush_solid_batch();
  }
  if (tri3d_solid_batch_count >= TRI3D_BATCH_MAX_COMMANDS)
  {
    tri3d_flush_solid_batch();
  }
  if (tri3d_solid_batch_count == 0)
  {
    tri3d_solid_batch_color_mem = params->gfx_color_mem;
    tri3d_solid_batch_z_mem = params->gfx_z_mem;
    tri3d_solid_batch_stamp_mem = params->gfx_z_stamp_mem;
    tri3d_solid_batch_frame_id = params->frame_id;
  }

  Tri3dSolidRasterCommand* command = &tri3d_solid_command_buffer()[tri3d_solid_batch_count++];
  command->params = *params;
  command->raster_x_start = (uint16_t)raster_x_start;
  command->raster_x_end = (uint16_t)raster_x_end;
  command->raster_y_start = (uint16_t)raster_y_start;
  command->raster_y_end = (uint16_t)raster_y_end;
}

static bool tri3d_textured_worker_poll();

void __attribute__((section(".itcm"))) tri3d_core1_worker_poll()
{
  if (__atomic_load_n(&tri3d_core1_online, __ATOMIC_RELAXED) == 0u)
  {
    __atomic_store_n(&tri3d_core1_online, 1u, __ATOMIC_RELEASE);
  }
  if (tri3d_textured_worker_poll()) return;
  if (__atomic_load_n(&tri3d_core1_job_state, __ATOMIC_ACQUIRE) != TRI3D_CORE1_READY)
  {
    return;
  }
  uint32_t expected = TRI3D_CORE1_READY;
  if (!__atomic_compare_exchange_n(&tri3d_core1_job_state, &expected, TRI3D_CORE1_ACTIVE,
                                   false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
  {
    return;
  }
  tri3d_process_solid_tiles(1u, 1u, 2u, 1u);
  __atomic_store_n(&tri3d_core1_job_state, TRI3D_CORE1_DONE, __ATOMIC_RELEASE);
}

static void __attribute__((section(".itcm"))) tri3d_raster_projected_tri(var* gfx_color_mem,
                                                                         var* gfx_z_mem,
                                                                         var* gfx_z_stamp_mem,
                                                                         uint8_t frame_id,
                                                                         uint32_t color,
                                                                         uint8_t cull_positive_area,
                                                                         uint8_t cull_negative_area,
                                                                         float x0,
                                                                         float y0,
                                                                         float z0,
                                                                         float x1,
                                                                         float y1,
                                                                         float z1,
                                                                         float x2,
                                                                         float y2,
                                                                         float z2)
{
  tri3d_stat_add(TRI3D_STAT_TRIANGLES_SUBMITTED, 1);
  const float cull_area = ((x1 - x0) * (y2 - y0)) -
                          ((x2 - x0) * (y1 - y0));
  if (((cull_area > 0) && cull_positive_area) || ((cull_area < 0) && cull_negative_area))
  {
    tri3d_stat_add(TRI3D_STAT_TRIANGLES_CULLED, 1);
    return;
  }

  #define TRI3D_HELPER_SWAP(ax, ay, az, bx, by, bz) do { \
      float __tx = (ax); (ax) = (bx); (bx) = __tx; \
      float __ty = (ay); (ay) = (by); (by) = __ty; \
      float __tz = (az); (az) = (bz); (bz) = __tz; \
    } while (0)

  if (y0 > y1) TRI3D_HELPER_SWAP(x0, y0, z0, x1, y1, z1);
  if (y1 > y2) TRI3D_HELPER_SWAP(x1, y1, z1, x2, y2, z2);
  if (y0 > y1) TRI3D_HELPER_SWAP(x0, y0, z0, x1, y1, z1);

  #undef TRI3D_HELPER_SWAP

  const float area = ((x1 - x0) * (y2 - y0)) -
                     ((x2 - x0) * (y1 - y0));
  if (area == 0.0f || y2 < -0.5f || y0 >= 239.5f)
  {
    return;
  }

  tri3d_stat_add(TRI3D_STAT_TRIANGLES_RASTERIZED, 1);

  const float inv_area = 1.0f / area;
  const float dzdx =
    (((z1 - z0) * (y2 - y0)) - ((z2 - z0) * (y1 - y0))) * inv_area;
  const float dzdy =
    (((x1 - x0) * (z2 - z0)) - ((x2 - x0) * (z1 - z0))) * inv_area;
  const int32_t x0edge = tri3d_edge_fx_from_float(x0);
  const int32_t y0edge = tri3d_edge_fx_from_float(y0);
  const int32_t x1edge = tri3d_edge_fx_from_float(x1);
  const int32_t y1edge = tri3d_edge_fx_from_float(y1);
  const int32_t x2edge = tri3d_edge_fx_from_float(x2);
  const int32_t y2edge = tri3d_edge_fx_from_float(y2);
  const int64_t dx01 = tri3d_edge_fx_slope(x0edge, y0edge, x1edge, y1edge);
  const int64_t dx02 = tri3d_edge_fx_slope(x0edge, y0edge, x2edge, y2edge);
  const int64_t dx12 = tri3d_edge_fx_slope(x1edge, y1edge, x2edge, y2edge);
  const uint32_t packed_color = color * 0x01010101u;
  const uint32_t stamp_word_full = (uint32_t)frame_id * 0x01010101u;

  const int32_t scan_y0 = tri3d_edge_fx_ceil(y0edge - TRI3D_EDGE_FX_HALF);
  const int32_t scan_y1 = tri3d_edge_fx_ceil(y1edge - TRI3D_EDGE_FX_HALF);
  const int32_t scan_y2 = tri3d_edge_fx_ceil(y2edge - TRI3D_EDGE_FX_HALF);

  Tri3dSolidRasterParams params;
  params.gfx_color_mem = gfx_color_mem;
  params.gfx_z_mem = gfx_z_mem;
  params.gfx_z_stamp_mem = gfx_z_stamp_mem;
  params.frame_id = frame_id;
  params.color = color;
  params.packed_color = packed_color;
  params.stamp_word_full = stamp_word_full;
  params.x0 = x0;
  params.y0 = y0;
  params.z0 = z0;
  params.dzdx = dzdx;
  params.dzdy = dzdy;
  params.scan_y0 = scan_y0;
  params.scan_y1 = scan_y1;
  params.scan_y2 = scan_y2;
  params.dx01 = dx01;
  params.dx02 = dx02;
  params.dx12 = dx12;
  params.upper_xa = (scan_y1 > scan_y0)
    ? tri3d_edge_fx_at_y(x0edge, y0edge, dx02, scan_y0) : 0;
  params.upper_xb = (scan_y1 > scan_y0)
    ? tri3d_edge_fx_at_y(x0edge, y0edge, dx01, scan_y0) : 0;
  params.lower_xa = (scan_y2 > scan_y1)
    ? tri3d_edge_fx_at_y(x0edge, y0edge, dx02, scan_y1) : 0;
  params.lower_xb = (scan_y2 > scan_y1)
    ? tri3d_edge_fx_at_y(x1edge, y1edge, dx12, scan_y1) : 0;

  int32_t raster_y_start = scan_y0 < 0 ? 0 : scan_y0;
  int32_t raster_y_end = scan_y2 > 240 ? 240 : scan_y2;
  int32_t min_xedge = x0edge < x1edge ? x0edge : x1edge;
  if (x2edge < min_xedge) min_xedge = x2edge;
  int32_t max_xedge = x0edge > x1edge ? x0edge : x1edge;
  if (x2edge > max_xedge) max_xedge = x2edge;
  int32_t raster_x_start = tri3d_edge_fx_ceil((int64_t)min_xedge - TRI3D_EDGE_FX_HALF);
  int32_t raster_x_end = tri3d_edge_fx_ceil((int64_t)max_xedge - TRI3D_EDGE_FX_HALF);
  if (raster_x_start < 0) raster_x_start = 0;
  if (raster_x_end > 320) raster_x_end = 320;
  if (raster_x_start >= raster_x_end || raster_y_start >= raster_y_end) return;

  tri3d_queue_solid_command(&params, raster_x_start, raster_x_end,
                            raster_y_start, raster_y_end);
}

static void __attribute__((section(".itcm"))) tri3d_raster_projected_tri_gouraud(var* gfx_color_mem,
                                                                                 var* gfx_z_mem,
                                                                                 var* gfx_z_stamp_mem,
                                                                                 uint8_t frame_id,
                                                                                 uint8_t cull_positive_area,
                                                                                 uint8_t cull_negative_area,
                                                                                 float x0,
                                                                                 float y0,
                                                                                 float z0,
                                                                                 float r0,
                                                                                 float g0,
                                                                                 float b0,
                                                                                 float x1,
                                                                                 float y1,
                                                                                 float z1,
                                                                                 float r1,
                                                                                 float g1,
                                                                                 float b1,
                                                                                 float x2,
                                                                                 float y2,
                                                                                 float z2,
                                                                                 float r2,
                                                                                 float g2,
                                                                                 float b2)
{
  tri3d_stat_add(TRI3D_STAT_TRIANGLES_SUBMITTED, 1);
  const float cull_area = ((x1 - x0) * (y2 - y0)) -
                          ((x2 - x0) * (y1 - y0));
  if (((cull_area > 0) && cull_positive_area) || ((cull_area < 0) && cull_negative_area))
  {
    tri3d_stat_add(TRI3D_STAT_TRIANGLES_CULLED, 1);
    return;
  }

  #define TRI3D_GOURAUD_SWAP(ax, ay, az, ar, ag, ab, bx, by, bz, br, bg, bb) do { \
      float __tx = (ax); (ax) = (bx); (bx) = __tx; \
      float __ty = (ay); (ay) = (by); (by) = __ty; \
      float __tz = (az); (az) = (bz); (bz) = __tz; \
      float __tr = (ar); (ar) = (br); (br) = __tr; \
      float __tg = (ag); (ag) = (bg); (bg) = __tg; \
      float __tb = (ab); (ab) = (bb); (bb) = __tb; \
    } while (0)

  if (y0 > y1) TRI3D_GOURAUD_SWAP(x0, y0, z0, r0, g0, b0, x1, y1, z1, r1, g1, b1);
  if (y1 > y2) TRI3D_GOURAUD_SWAP(x1, y1, z1, r1, g1, b1, x2, y2, z2, r2, g2, b2);
  if (y0 > y1) TRI3D_GOURAUD_SWAP(x0, y0, z0, r0, g0, b0, x1, y1, z1, r1, g1, b1);

  #undef TRI3D_GOURAUD_SWAP

  const float area = ((x1 - x0) * (y2 - y0)) -
                     ((x2 - x0) * (y1 - y0));
  if (area == 0.0f || y2 < -0.5f || y0 >= 239.5f)
  {
    return;
  }

  tri3d_stat_add(TRI3D_STAT_TRIANGLES_RASTERIZED, 1);

  const float inv_area = 1.0f / area;
  const float dzdx =
    (((z1 - z0) * (y2 - y0)) - ((z2 - z0) * (y1 - y0))) * inv_area;
  const float dzdy =
    (((x1 - x0) * (z2 - z0)) - ((x2 - x0) * (z1 - z0))) * inv_area;
  const float drdx =
    (((r1 - r0) * (y2 - y0)) - ((r2 - r0) * (y1 - y0))) * inv_area;
  const float dgdx =
    (((g1 - g0) * (y2 - y0)) - ((g2 - g0) * (y1 - y0))) * inv_area;
  const float dbdx =
    (((b1 - b0) * (y2 - y0)) - ((b2 - b0) * (y1 - y0))) * inv_area;
  const float drdy =
    (((x1 - x0) * (r2 - r0)) - ((x2 - x0) * (r1 - r0))) * inv_area;
  const float dgdy =
    (((x1 - x0) * (g2 - g0)) - ((x2 - x0) * (g1 - g0))) * inv_area;
  const float dbdy =
    (((x1 - x0) * (b2 - b0)) - ((x2 - x0) * (b1 - b0))) * inv_area;
  const int32_t x0edge = tri3d_edge_fx_from_float(x0);
  const int32_t y0edge = tri3d_edge_fx_from_float(y0);
  const int32_t x1edge = tri3d_edge_fx_from_float(x1);
  const int32_t y1edge = tri3d_edge_fx_from_float(y1);
  const int32_t x2edge = tri3d_edge_fx_from_float(x2);
  const int32_t y2edge = tri3d_edge_fx_from_float(y2);
  const int64_t dx01 = tri3d_edge_fx_slope(x0edge, y0edge, x1edge, y1edge);
  const int64_t dx02 = tri3d_edge_fx_slope(x0edge, y0edge, x2edge, y2edge);
  const int64_t dx12 = tri3d_edge_fx_slope(x1edge, y1edge, x2edge, y2edge);
  const uint32_t stamp_word_full = (uint32_t)frame_id * 0x01010101u;

  auto draw_half = [&](int32_t y_start, int32_t y_end, int64_t xa, int64_t dxa, int64_t xb, int64_t dxb)
  {
    int32_t y = y_start;
    if (y < 0)
    {
      const int32_t skip = -y;
      xa += dxa * skip;
      xb += dxb * skip;
      y = 0;
    }
    if (y_end > 240) y_end = 240;
    for (; y < y_end; y++)
    {
      const int64_t min_edge = xa < xb ? xa : xb;
      const int64_t max_edge = xa < xb ? xb : xa;
      const int32_t left = tri3d_edge_fx_ceil(min_edge - TRI3D_EDGE_FX_HALF);
      const int32_t right = tri3d_edge_fx_ceil(max_edge - TRI3D_EDGE_FX_HALF) - 1;
      if (right >= 0 && left < 320)
      {
        const int32_t draw_left = left < 0 ? 0 : left;
        const int32_t draw_right = right >= 320 ? 319 : right;
        const float dx = ((float)draw_left + 0.5f) - x0;
        const float dy = ((float)y + 0.5f) - y0;
        const float z = z0 + (dzdx * dx) + (dzdy * dy);
        const float r = r0 + (drdx * dx) + (drdy * dy);
        const float g = g0 + (dgdx * dx) + (dgdy * dy);
        const float b = b0 + (dbdx * dx) + (dbdy * dy);
        tri3d_write_span_gouraud_z(gfx_color_mem, gfx_z_mem, gfx_z_stamp_mem, frame_id,
                                   stamp_word_full, y, draw_left, draw_right,
                                   r, g, b, drdx, dgdx, dbdx, z, dzdx);
      }
      xa += dxa;
      xb += dxb;
    }
  };

  const int32_t scan_y0 = tri3d_edge_fx_ceil(y0edge - TRI3D_EDGE_FX_HALF);
  const int32_t scan_y1 = tri3d_edge_fx_ceil(y1edge - TRI3D_EDGE_FX_HALF);
  const int32_t scan_y2 = tri3d_edge_fx_ceil(y2edge - TRI3D_EDGE_FX_HALF);
  if (scan_y1 > scan_y0)
  {
    draw_half(scan_y0, scan_y1,
              tri3d_edge_fx_at_y(x0edge, y0edge, dx02, scan_y0), dx02,
              tri3d_edge_fx_at_y(x0edge, y0edge, dx01, scan_y0), dx01);
  }
  if (scan_y2 > scan_y1)
  {
    draw_half(scan_y1, scan_y2,
              tri3d_edge_fx_at_y(x0edge, y0edge, dx02, scan_y1), dx02,
              tri3d_edge_fx_at_y(x1edge, y1edge, dx12, scan_y1), dx12);
  }
}

// Explicit textured lists borrow only command storage from the terminal's
// PSRAM staging buffer. The pixel working sets below are ordinary .bss SRAM.
// Four rows per core keep the additional pixel allocation at exactly 15 KiB.
#define TRI3D_TEX_TILE_ROWS 4u
#define TRI3D_TEX_TILE_PIXELS (320u * TRI3D_TEX_TILE_ROWS)
#define TRI3D_TEX_COMMANDS 512u
struct Tri3dTexTile
{
  uint32_t z[TRI3D_TEX_TILE_PIXELS];
  uint8_t state[TRI3D_TEX_TILE_PIXELS]; // bit 0 valid, bit 1 dirty
  uint8_t color[TRI3D_TEX_TILE_PIXELS];
};
static Tri3dTexTile tri3d_tex_tiles[2];
static_assert(sizeof(tri3d_tex_tiles) == 15360u, "textured tile SRAM budget");
struct Tri3dTexCommand
{
  var *texture, *mask;
  uint32_t tex_base, mask_base, flags;
  int32_t width, height;
  uint8_t use_mask, fast16;
  float x0, y0, z0, uq0, vq0, q0;
  float dzdx, dzdy, duqdx, duqdy, dvqdx, dvqdy, dqdx, dqdy;
  int32_t sy0, sy1, sy2;
  int64_t uxa, uxb, lxa, lxb, dx01, dx02, dx12;
  int32_t xmin, xmax, ymin, ymax;
};
struct Tri3dTexBin
{
  uint32_t members[TRI3D_TEX_COMMANDS / 32u];
  uint16_t xmin, xmax, ymin, ymax;
};
static_assert(sizeof(Tri3dTexCommand) * TRI3D_TEX_COMMANDS + alignof(Tri3dTexCommand) +
              sizeof(Tri3dTexBin) * (240u / TRI3D_TEX_TILE_ROWS)
              <= VGA_4BIT_BUFFER_SIZE, "textured commands exceed PSRAM scratch");
static Tri3dTexCommand* tri3d_tex_commands()
{
  uintptr_t p = (uintptr_t)vga_640_480_4_01;
  p = (p + alignof(Tri3dTexCommand) - 1u) & ~(uintptr_t)(alignof(Tri3dTexCommand) - 1u);
  return (Tri3dTexCommand*)p;
}
static Tri3dTexBin* tri3d_tex_bins()
{
  return (Tri3dTexBin*)(tri3d_tex_commands() + TRI3D_TEX_COMMANDS);
}
static void tri3d_tex_reset_bins()
{
  memset(tri3d_tex_bins(), 0, sizeof(Tri3dTexBin) * (240u / TRI3D_TEX_TILE_ROWS));
  if (tri3d_stage_profile && tri3d_pixel_stats)
    tri3d_stage_stats[57u - 35u] += sizeof(Tri3dTexBin) * (240u / TRI3D_TEX_TILE_ROWS);
}
static uint32_t tri3d_tex_count, tri3d_tex_mode, tri3d_tex_worker_state;
static uint8_t tri3d_tex_active, tri3d_tex_frame, tri3d_tex_occlusion;
static uint8_t tri3d_occlusion_initialized; // Ordinary .bss, unlike PSRAM NOLOAD.
static var *tri3d_tex_color, *tri3d_tex_depth, *tri3d_tex_stamp;
// Each worker exclusively owns its counters. Merge only after the join.
static uint32_t tri3d_tex_counts[2][14];
// Diagnostic-only stage counters have a single writer per worker. They are
// published by the same READY/DONE release/acquire handoff as the tile output.
struct Tri3dTexStageCounts
{
  uint32_t load_us, raster_us, commit_us, active_us;
  uint32_t occupied, memberships, command_bytes, staged_pixels, valid_loads;
  uint32_t dirty_depth, dirty_words, bin_bytes;
};
static Tri3dTexStageCounts tri3d_tex_stage_counts[2];

// A cell is 16x4 actual pixels. Generation zero means unknown: a maximum
// depth of UINT32_MAX alone is NOT a valid coverage marker. Old same-frame
// maxima remain conservative because depth only becomes nearer after a draw.
struct Tri3dOcclusionCell { uint32_t maximum, generation; };
struct Tri3dOcclusionState
{
  Tri3dOcclusionCell cells[20u * 60u];
  uint32_t generation;
  const var *depth, *stamp;
  uint8_t frame;
  uint32_t updates[2]; // worker-local detailed counts, merged after join.
};
#if defined(PSRAM)
PSRAM
#endif
static Tri3dOcclusionState tri3d_occlusion;

static void tri3d_occlusion_invalidate()
{
  if (!tri3d_occlusion_initialized)
  {
    memset(&tri3d_occlusion, 0, sizeof(tri3d_occlusion));
    tri3d_occlusion.generation = 1u;
    tri3d_occlusion_initialized = 1u;
    return;
  }
  if (++tri3d_occlusion.generation == 0u)
  {
    memset(tri3d_occlusion.cells, 0, sizeof(tri3d_occlusion.cells));
    tri3d_occlusion.generation = 1u;
  }
}

struct Tri3dOcclusionAccumulator
{
  uint32_t maximum[20];
  uint8_t covered[20];
  uint32_t first, end, row;
};
static inline __attribute__((always_inline)) void tri3d_occlusion_accumulator_begin(
  Tri3dOcclusionAccumulator& a, uint32_t ty, int32_t xs, int32_t xe,
  int32_t ys, int32_t ye)
{
  a.first = (uint32_t)(xs + 15) / 16u; a.end = (uint32_t)xe / 16u;
  a.row = ty / 4u;
  // Edge cells not fully imported into this SRAM tile must not be summarized.
  if (ys != (int32_t)ty || ye != (int32_t)(ty + 4u)) a.end = a.first;
  for (uint32_t c = a.first; c < a.end; ++c)
  { a.maximum[c] = 0u; a.covered[c] = 1u; }
}
static inline __attribute__((always_inline)) void tri3d_occlusion_accumulator_word(
  Tri3dOcclusionAccumulator& a, const Tri3dTexTile& tile, uint32_t local, int32_t x)
{
  const uint32_t c = (uint32_t)x / 16u;
  if (c < a.first || c >= a.end) return;
  for (uint32_t lane = 0u; lane < 4u; ++lane)
  {
    if (!(tile.state[local + lane] & 1u)) a.covered[c] = 0u;
    else if (tile.z[local + lane] > a.maximum[c]) a.maximum[c] = tile.z[local + lane];
  }
}
template<bool COUNT>
static inline __attribute__((always_inline)) void tri3d_occlusion_accumulator_end(
  const Tri3dOcclusionAccumulator& a, uint32_t& updates)
{
  for (uint32_t c = a.first; c < a.end; ++c)
  {
    Tri3dOcclusionCell& cell = tri3d_occlusion.cells[a.row * 20u + c];
    cell.maximum = a.maximum[c];
    cell.generation = a.covered[c] ? tri3d_occlusion.generation : 0u;
    if (COUNT && a.covered[c]) ++updates;
  }
}

static inline __attribute__((always_inline)) bool tri3d_occlusion_finite(float v)
{ return v <= __FLT_MAX__ && v >= -__FLT_MAX__; }

// Outward IEEE binary32 bounds, including flush-to-zero behavior. Mapping the
// subnormal interval to 0/+/-FLT_MIN is deliberately looser than nextafterf,
// so both gradual-underflow and FZ execution remain enclosed. No double math.
static bool tri3d_occlusion_outward(float v, bool upward, float& out)
{
  uint32_t bits; memcpy(&bits, &v, sizeof(bits));
  const uint32_t magnitude = bits & 0x7fffffffu;
  if (magnitude >= 0x7f800000u) return false;
  if (!magnitude) bits = upward ? 0x00800000u : 0x80800000u;
  else if (upward)
  {
    if (bits & 0x80000000u)
      bits = magnitude <= 0x00800000u ? 0u : bits - 1u;
    else bits = magnitude < 0x00800000u ? 0x00800000u : bits + 1u;
  }
  else
  {
    if (bits & 0x80000000u)
      bits = magnitude < 0x00800000u ? 0x80800000u : bits + 1u;
    else bits = magnitude <= 0x00800000u ? 0u : bits - 1u;
  }
  memcpy(&out, &bits, sizeof(out));
  return tri3d_occlusion_finite(out);
}

static bool tri3d_occlusion_depth_lower(const Tri3dTexCommand& p, uint32_t& lower)
{
  if (!tri3d_occlusion_finite(p.x0) || !tri3d_occlusion_finite(p.y0) ||
      !tri3d_occlusion_finite(p.z0) || !tri3d_occlusion_finite(p.dzdx) ||
      !tri3d_occlusion_finite(p.dzdy)) return false;
  // These bounds are the same conservative fixed-edge bounds already used
  // for SRAM import/bin membership. Every actual span starts inside them.
  const int32_t x = p.dzdx < 0.0f ? p.xmax - 1 : p.xmin;
  const int32_t y = p.dzdy < 0.0f ? p.ymax - 1 : p.ymin;
  const float dx = ((float)x + 0.5f) - p.x0;
  const float dy = ((float)y + 0.5f) - p.y0;
  float tx, ty, start, next;
  if (!tri3d_occlusion_outward(p.dzdx * dx, false, tx) ||
      !tri3d_occlusion_outward(p.dzdy * dy, false, ty) ||
      !tri3d_occlusion_outward(p.z0 + tx, false, next) ||
      !tri3d_occlusion_outward(next + ty, false, start)) return false;
  // Products/additions were each rounded outward. Therefore this also bounds
  // either legal FMA contraction of the normal span's row-start expression.
  if (p.dzdx < 0.0f)
  {
    const float n = (float)(p.xmax - p.xmin - 1); // at most319 repeated adds.
    float delta, magnitude, step, error;
    const float absolute_start = start < 0.0f ? -start : start;
    if (!tri3d_occlusion_outward(n * -p.dzdx, true, next) ||
        !tri3d_occlusion_outward(absolute_start + next, true, magnitude)) return false;
    if (magnitude < 1.0f) magnitude = 1.0f;
    if (!tri3d_occlusion_outward(magnitude * 2.0f, true, magnitude)) return false;
    uint32_t bits; memcpy(&bits, &magnitude, sizeof(bits));
    // magnitude>=2, so its ULP is a normal power of two. It exceeds a full
    // rounding error for every intermediate in the monotone add recurrence:
    // 319*2^-22 <<1 leaves the extra factor-of-two magnitude budget ample.
    bits = (((bits >> 23u) & 255u) - 23u) << 23u;
    memcpy(&step, &bits, sizeof(step));
    if (!tri3d_occlusion_outward(n * step, true, error) ||
        !tri3d_occlusion_outward(n * p.dzdx, false, delta) ||
        !tri3d_occlusion_outward(start + delta, false, next) ||
        !tri3d_occlusion_outward(next - error, false, start)) return false;
  }
  lower = tri3d_z_from_float(start);
  return true;
}

template<bool COUNT>
static bool tri3d_occlusion_reject(const Tri3dTexCommand& p)
{
  if (COUNT) ++tri3d_stage_stats[25];
  if (p.xmin < 0 || p.xmax > 320 || p.ymin < 0 || p.ymax > 240 ||
      p.xmin >= p.xmax || p.ymin >= p.ymax) return false;
  uint32_t maximum = 0u;
  for (uint32_t y = (uint32_t)p.ymin / 4u; y <= (uint32_t)(p.ymax - 1) / 4u; ++y)
    for (uint32_t x = (uint32_t)p.xmin / 16u; x <= (uint32_t)(p.xmax - 1) / 16u; ++x)
    {
      if (COUNT) ++tri3d_stage_stats[27];
      const Tri3dOcclusionCell& cell = tri3d_occlusion.cells[y * 20u + x];
      if (!tri3d_occlusion.generation || cell.generation != tri3d_occlusion.generation) return false;
      if (cell.maximum > maximum) maximum = cell.maximum;
    }
  uint32_t lower;
  if (!tri3d_occlusion_depth_lower(p, lower))
  { if (COUNT) ++tri3d_stage_stats[29]; return false; }
  // Strict original depth acceptance is new<old. Equality is already hidden.
  // NEVER reverse this comparison: a nearer candidate must never be culled.
  if (maximum > lower) return false;
  if (COUNT) ++tri3d_stage_stats[26];
  return true;
}

template <bool PIXEL_COUNTS>
static inline __attribute__((always_inline)) void
tri3d_textured_tile_span_body(const Tri3dTexCommand& p, Tri3dTexTile& tile, uint32_t* counts,
                         int32_t tile_y, int32_t y, int32_t left, int32_t right)
{
  const float dx = ((float)left + 0.5f) - p.x0;
  const float dy = ((float)y + 0.5f) - p.y0;
  float z = p.z0 + (p.dzdx * dx) + (p.dzdy * dy);
  float uq = p.uq0 + (p.duqdx * dx) + (p.duqdy * dy);
  float vq = p.vq0 + (p.dvqdx * dx) + (p.dvqdy * dy);
  float q = p.q0 + (p.dqdx * dx) + (p.dqdy * dy);
  const bool clamped = !p.use_mask && !p.fast16 &&
    tri3d_clamped_sampler_supported(p.width, p.height, p.flags);
  if (tri3d_stats_enabled)
  {
    uint32_t path = p.use_mask ? 3u : p.fast16 ? 2u :
      (clamped && !tri3d_force_general_sampler) ? 1u : 0u;
    ++counts[path]; counts[path + 4u] += (uint32_t)(right - left + 1);
    counts[10] += (uint32_t)(right - left + 1);
    if (clamped) { ++counts[8]; counts[9] += (uint32_t)(right - left + 1); }
  }
  uint32_t tc = 0xffffffffu, tw = 0u, mc = 0xffffffffu, mw = 0u;
  const uint32_t wm = (uint32_t)p.width - 1u, hm = (uint32_t)p.height - 1u;
  const uint8_t wp = ((uint32_t)p.width & wm) == 0u;
  const uint8_t hp = ((uint32_t)p.height & hm) == 0u;
  uint32_t local = (uint32_t)(y - tile_y) * 320u + (uint32_t)left;
  for (int32_t x = left; x <= right; ++x, ++local)
  {
    uint32_t nz = tri3d_z_from_float(z);
    // Test without committing: transparent texels must not acquire depth.
    if (!(tile.state[local] & 1u) || tri3d_z_nearer(nz, tile.z[local]))
    {
      uint32_t texel = 0; uint8_t color = 0; bool draws = true;
      if (p.use_mask)
      {
        draws = (p.width == 16 && p.height == 16 && (p.flags & 3u) == 3u)
          ? tri3d_texel_index_16wrap_persp(texel, uq, vq, q)
          : tri3d_texel_index_persp(texel, p.width, p.height, p.flags,
                                    wm, hm, wp, hp, uq, vq, q);
        if (draws)
        {
          if (PIXEL_COUNTS) ++counts[12];
          draws = tri3d_mask_texel_draws(p.mask, p.mask_base, texel, mc, mw);
        }
        if (draws) color = tri3d_sample_texel_rgb332_cached(p.texture, p.tex_base, texel, tc, tw);
      }
      else if (p.fast16)
        color = tri3d_sample_tex_rgb332_16wrap_persp_cached(p.texture, p.tex_base, uq, vq, q, tc, tw);
      else if (clamped && !tri3d_force_general_sampler)
        color = tri3d_sample_tex_rgb332_clamped_persp_cached(p.texture, p.tex_base, p.width, p.height, uq, vq, q, tc, tw);
      else
        color = tri3d_sample_tex_rgb332_persp_cached(p.texture, p.tex_base, p.width, p.height,
                                                    p.flags, wm, hm, wp, hp, uq, vq, q, tc, tw);
      if (draws)
      {
        tile.z[local] = nz; tile.state[local] = 3u; tile.color[local] = color;
        if (PIXEL_COUNTS) ++counts[13];
      }
    }
    else if (PIXEL_COUNTS) ++counts[11];
    // Keep the immediate path's operation order and repeated additions.
    uq += p.duqdx; vq += p.dvqdx; q += p.dqdx; z += p.dzdx;
  }
}

static void __attribute__((section(".time_critical.tri3d_textured_tile_span"), noinline))
tri3d_textured_tile_span(const Tri3dTexCommand& p, Tri3dTexTile& tile, uint32_t* counts,
                         int32_t tile_y, int32_t y, int32_t left, int32_t right)
{
  tri3d_textured_tile_span_body<false>(p, tile, counts, tile_y, y, left, right);
}
// Detailed counts are used only for untimed diagnostic warm-ups. Keep their
// separate kernel out of SRAM and out of the primary measurement pixel loop.
static void __attribute__((section(".itcm"), noinline))
tri3d_textured_tile_span_counted(const Tri3dTexCommand& p, Tri3dTexTile& tile, uint32_t* counts,
                                 int32_t tile_y, int32_t y, int32_t left, int32_t right)
{
  tri3d_textured_tile_span_body<true>(p, tile, counts, tile_y, y, left, right);
}

static void __attribute__((section(".time_critical.tri3d_textured_half"), noinline))
tri3d_textured_half(const Tri3dTexCommand& p, Tri3dTexTile& tile,
                               uint32_t* counts, int32_t tile_y,
                               int32_t start, int32_t end, int32_t hs, int32_t he,
                               int64_t xa, int64_t da, int64_t xb, int64_t db)
{
  if (start < hs) start = hs;
  if (end > he) end = he;
  if (start >= end) return;
  xa += da * (int64_t)(start - hs); xb += db * (int64_t)(start - hs);
  for (int32_t y = start; y < end; ++y)
  {
    int32_t left = tri3d_edge_fx_ceil((xa < xb ? xa : xb) - TRI3D_EDGE_FX_HALF);
    int32_t right = tri3d_edge_fx_ceil((xa < xb ? xb : xa) - TRI3D_EDGE_FX_HALF) - 1;
    if (right >= 0 && left < 320)
    {
      if (left < 0) left = 0;
      if (right > 319) right = 319;
      if (left <= right)
      {
        if (tri3d_pixel_stats) tri3d_textured_tile_span_counted(p, tile, counts, tile_y, y, left, right);
        else tri3d_textured_tile_span(p, tile, counts, tile_y, y, left, right);
      }
    }
    xa += da; xb += db;
  }
}

template<bool CULL, bool COUNT_UPDATES>
static inline __attribute__((always_inline)) void
tri3d_process_textured_tiles_body(uint32_t core, uint32_t stride)
{
  Tri3dTexTile& tile = tri3d_tex_tiles[core];
  uint32_t* counts = tri3d_tex_counts[core];
  Tri3dTexCommand* commands = tri3d_tex_commands();
  uint32_t occlusion_updates = 0u;
  for (uint32_t ty = core * TRI3D_TEX_TILE_ROWS; ty < 240u; ty += stride * TRI3D_TEX_TILE_ROWS)
  {
    // Membership is recorded once at submission. Empty/off-tile commands are
    // never repeatedly fetched from PSRAM for all 60 screen strips.
    const Tri3dTexBin bin = tri3d_tex_bins()[ty / TRI3D_TEX_TILE_ROWS];
    int32_t xs = bin.xmin, xe = bin.xmax, ys = bin.ymin, ye = bin.ymax;
    if (ys < (int32_t)ty) ys = ty;
    if (ye > (int32_t)(ty + TRI3D_TEX_TILE_ROWS)) ye = ty + TRI3D_TEX_TILE_ROWS;
    xs &= ~3; xe = (xe + 3) & ~3;
    if (xs >= xe || ys >= ye) continue;
    Tri3dOcclusionAccumulator occlusion_accumulator;
    if (CULL) tri3d_occlusion_accumulator_begin(occlusion_accumulator, ty, xs, xe, ys, ye);
    for (int32_t y = ys; y < ye; ++y)
      for (int32_t x = xs; x < xe; x += 4)
      {
        uint32_t pixel = y * 320u + x, local = (y - ty) * 320u + x;
        uint32_t stamps = tri3d_tex_stamp[pixel >> 2].value;
        for (uint32_t lane = 0; lane < 4; ++lane)
        {
          bool valid = ((stamps >> (lane * 8u)) & 255u) == tri3d_tex_frame;
          tile.state[local + lane] = valid ? 1u : 0u;
          if (valid) tile.z[local + lane] = tri3d_tex_depth[pixel + lane].value;
        }
      }
    // Per-pixel order stays exactly as submitted, including equal-depth ties.
    for (uint32_t word = 0; word < (tri3d_tex_count + 31u) / 32u; ++word)
    {
      uint32_t members = bin.members[word];
      while (members)
      {
        uint32_t i = word * 32u + (uint32_t)__builtin_ctz(members);
        members &= members - 1u;
        const Tri3dTexCommand p = commands[i]; // one small local copy from PSRAM
        tri3d_textured_half(p, tile, counts, ty, ys, ye, p.sy0, p.sy1,
                           p.uxa, p.dx02, p.uxb, p.dx01);
        tri3d_textured_half(p, tile, counts, ty, ys, ye, p.sy1, p.sy2,
                           p.lxa, p.dx02, p.lxb, p.dx12);
      }
    }
    for (int32_t y = ys; y < ye; ++y)
      for (int32_t x = xs; x < xe; x += 4)
      {
        uint32_t pixel = y * 320u + x, local = (y - ty) * 320u + x;
        if (CULL) tri3d_occlusion_accumulator_word(occlusion_accumulator, tile, local, x);
        if (!((tile.state[local] | tile.state[local+1] | tile.state[local+2] | tile.state[local+3]) & 2u)) continue;
        uint32_t colors = tri3d_tex_color[pixel >> 2].value;
        uint32_t stamps = tri3d_tex_stamp[pixel >> 2].value;
        for (uint32_t lane = 0; lane < 4; ++lane)
          if (tile.state[local + lane] & 2u)
          {
            uint32_t shift = lane * 8u, mask = 255u << shift;
            colors = (colors & ~mask) | ((uint32_t)tile.color[local + lane] << shift);
            stamps = (stamps & ~mask) | ((uint32_t)tri3d_tex_frame << shift);
            tri3d_tex_depth[pixel + lane].value = tile.z[local + lane];
          }
        tri3d_tex_color[pixel >> 2].value = colors;
        tri3d_tex_stamp[pixel >> 2].value = stamps;
      }
    if (CULL) tri3d_occlusion_accumulator_end<COUNT_UPDATES>(occlusion_accumulator, occlusion_updates);
    if (core == 1u) { ps2_keyboard_poll(); audio_core1_poll(); }
    else audio_core0_poll_if_due();
  }
  if (CULL && COUNT_UPDATES) tri3d_occlusion.updates[core] = occlusion_updates;
}

static void __attribute__((section(".time_critical.tri3d_process_textured_tiles"), noinline))
tri3d_process_textured_tiles(uint32_t core, uint32_t stride)
{ tri3d_process_textured_tiles_body<false, false>(core, stride); }

// Keep candidate outer workers in FLASH: the original hot SRAM span/half
// kernels are reused, with no duplicate multi-kilobyte SRAM allocation.
template<bool COUNTS>
static void tri3d_process_textured_tiles_occlusion(uint32_t core, uint32_t stride)
{ tri3d_process_textured_tiles_body<true, COUNTS>(core, stride); }

// Keep stage diagnostics separate from the normal SRAM tile and span kernels.
// Primary stage timing also runs in SRAM; only the discarded counted worker
// is flash-resident, avoiding a flash/PSRAM placement change in timed samples.
// Timed raster work INCLUDES bin traversal, command fetch and span dispatch,
// not just texture sampling. Import includes bin fetch/empty-strip checks.
// Detailed counts deliberately use a separate untimed warmup instantiation.
template <bool COUNTS, bool CULL = false>
static inline __attribute__((always_inline)) void
tri3d_process_textured_tiles_profile_body(uint32_t core, uint32_t stride)
{
  const uint32_t active_start = micros();
  Tri3dTexStageCounts& stage = tri3d_tex_stage_counts[core];
  Tri3dTexTile& tile = tri3d_tex_tiles[core];
  uint32_t* counts = tri3d_tex_counts[core];
  Tri3dTexCommand* commands = tri3d_tex_commands();
  uint32_t occlusion_updates = 0u;
  for (uint32_t ty = core * TRI3D_TEX_TILE_ROWS; ty < 240u; ty += stride * TRI3D_TEX_TILE_ROWS)
  {
    uint32_t start = micros();
    const Tri3dTexBin bin = tri3d_tex_bins()[ty / TRI3D_TEX_TILE_ROWS];
    if (COUNTS) stage.bin_bytes += sizeof(Tri3dTexBin);
    int32_t xs = bin.xmin, xe = bin.xmax, ys = bin.ymin, ye = bin.ymax;
    if (ys < (int32_t)ty) ys = ty;
    if (ye > (int32_t)(ty + TRI3D_TEX_TILE_ROWS)) ye = ty + TRI3D_TEX_TILE_ROWS;
    xs &= ~3; xe = (xe + 3) & ~3;
    if (xs >= xe || ys >= ye)
    {
      stage.load_us += (uint32_t)(micros() - start);
      continue;
    }
    if (COUNTS)
    {
      ++stage.occupied;
      stage.staged_pixels += (uint32_t)(xe - xs) * (uint32_t)(ye - ys);
    }
    Tri3dOcclusionAccumulator occlusion_accumulator;
    if (CULL) tri3d_occlusion_accumulator_begin(occlusion_accumulator, ty, xs, xe, ys, ye);
    for (int32_t y = ys; y < ye; ++y)
      for (int32_t x = xs; x < xe; x += 4)
      {
        uint32_t pixel = y * 320u + x, local = (y - ty) * 320u + x;
        uint32_t stamps = tri3d_tex_stamp[pixel >> 2].value;
        for (uint32_t lane = 0; lane < 4; ++lane)
        {
          bool valid = ((stamps >> (lane * 8u)) & 255u) == tri3d_tex_frame;
          tile.state[local + lane] = valid ? 1u : 0u;
          if (valid)
          {
            tile.z[local + lane] = tri3d_tex_depth[pixel + lane].value;
            if (COUNTS) ++stage.valid_loads;
          }
        }
      }
    stage.load_us += (uint32_t)(micros() - start);
    start = micros();
    for (uint32_t word = 0; word < (tri3d_tex_count + 31u) / 32u; ++word)
    {
      uint32_t members = bin.members[word];
      while (members)
      {
        uint32_t i = word * 32u + (uint32_t)__builtin_ctz(members);
        members &= members - 1u;
        const Tri3dTexCommand p = commands[i];
        if (COUNTS)
        {
          ++stage.memberships;
          stage.command_bytes += sizeof(Tri3dTexCommand);
        }
        tri3d_textured_half(p, tile, counts, ty, ys, ye, p.sy0, p.sy1,
                           p.uxa, p.dx02, p.uxb, p.dx01);
        tri3d_textured_half(p, tile, counts, ty, ys, ye, p.sy1, p.sy2,
                           p.lxa, p.dx02, p.lxb, p.dx12);
      }
    }
    stage.raster_us += (uint32_t)(micros() - start);
    start = micros();
    for (int32_t y = ys; y < ye; ++y)
      for (int32_t x = xs; x < xe; x += 4)
      {
        uint32_t pixel = y * 320u + x, local = (y - ty) * 320u + x;
        if (CULL) tri3d_occlusion_accumulator_word(occlusion_accumulator, tile, local, x);
        if (!((tile.state[local] | tile.state[local+1] | tile.state[local+2] | tile.state[local+3]) & 2u)) continue;
        uint32_t colors = tri3d_tex_color[pixel >> 2].value;
        uint32_t stamps = tri3d_tex_stamp[pixel >> 2].value;
        if (COUNTS) ++stage.dirty_words;
        for (uint32_t lane = 0; lane < 4; ++lane)
          if (tile.state[local + lane] & 2u)
          {
            uint32_t shift = lane * 8u, mask = 255u << shift;
            colors = (colors & ~mask) | ((uint32_t)tile.color[local + lane] << shift);
            stamps = (stamps & ~mask) | ((uint32_t)tri3d_tex_frame << shift);
            tri3d_tex_depth[pixel + lane].value = tile.z[local + lane];
            if (COUNTS) ++stage.dirty_depth;
          }
        tri3d_tex_color[pixel >> 2].value = colors;
        tri3d_tex_stamp[pixel >> 2].value = stamps;
      }
    if (CULL) tri3d_occlusion_accumulator_end<COUNTS>(occlusion_accumulator, occlusion_updates);
    stage.commit_us += (uint32_t)(micros() - start);
    if (core == 1u) { ps2_keyboard_poll(); audio_core1_poll(); }
    else audio_core0_poll_if_due();
  }
  stage.active_us += (uint32_t)(micros() - active_start);
  if (CULL && COUNTS) tri3d_occlusion.updates[core] = occlusion_updates;
}

static void __attribute__((section(".time_critical.tri3d_process_textured_tiles_profile"), noinline))
tri3d_process_textured_tiles_profile(uint32_t core, uint32_t stride)
{
  tri3d_process_textured_tiles_profile_body<false>(core, stride);
}

static void __attribute__((section(".itcm"), noinline))
tri3d_process_textured_tiles_profile_counted(uint32_t core, uint32_t stride)
{
  tri3d_process_textured_tiles_profile_body<true>(core, stride);
}

// Keep the large optional FLASH bodies out of the shared dispatch. The small
// dispatch below must inline into constant core/stride call sites so the
// original SRAM workers retain their Build24 constant-propagated variants.
static void __attribute__((noinline))
tri3d_process_textured_tiles_occlusion_dispatch(uint32_t core, uint32_t stride)
{
  if (!tri3d_stage_profile)
  {
    if (tri3d_pixel_stats) tri3d_process_textured_tiles_occlusion<true>(core, stride);
    else tri3d_process_textured_tiles_occlusion<false>(core, stride);
  }
  else if (tri3d_pixel_stats) tri3d_process_textured_tiles_profile_body<true, true>(core, stride);
  else tri3d_process_textured_tiles_profile_body<false, true>(core, stride);
}

static inline __attribute__((always_inline)) void
tri3d_process_textured_tiles_dispatch(uint32_t core, uint32_t stride)
{
  if (tri3d_tex_occlusion) tri3d_process_textured_tiles_occlusion_dispatch(core, stride);
  else if (!tri3d_stage_profile) tri3d_process_textured_tiles(core, stride);
  else if (tri3d_pixel_stats) tri3d_process_textured_tiles_profile_counted(core, stride);
  else tri3d_process_textured_tiles_profile(core, stride);
}

static bool tri3d_textured_worker_poll()
{
  // Match the legacy worker: idle polling performs only an acquire load.
  // Exclusive read/modify/write is needed only for a published job.
  if (__atomic_load_n(&tri3d_tex_worker_state, __ATOMIC_ACQUIRE) != TRI3D_CORE1_READY)
    return false;
  uint32_t expected = TRI3D_CORE1_READY;
  if (!__atomic_compare_exchange_n(&tri3d_tex_worker_state, &expected, TRI3D_CORE1_ACTIVE,
                                   false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
  tri3d_process_textured_tiles_dispatch(1u, 2u);
  __atomic_store_n(&tri3d_tex_worker_state, TRI3D_CORE1_DONE, __ATOMIC_RELEASE);
  return true;
}

static void tri3d_flush_textured_list()
{
  if (!tri3d_tex_count) return;
  const bool profile = tri3d_stage_profile != 0u;
  const uint32_t flush_start = profile ? micros() : 0u;
  memset(tri3d_tex_counts, 0, sizeof(tri3d_tex_counts));
  if (profile) memset(tri3d_tex_stage_counts, 0, sizeof(tri3d_tex_stage_counts));
  if (tri3d_tex_occlusion && tri3d_pixel_stats)
  { tri3d_occlusion.updates[0] = 0u; tri3d_occlusion.updates[1] = 0u; }
  if (tri3d_tex_mode == 2u && __atomic_load_n(&tri3d_core1_online, __ATOMIC_ACQUIRE))
  {
    __atomic_store_n(&tri3d_tex_worker_state, TRI3D_CORE1_READY, __ATOMIC_RELEASE);
    tri3d_process_textured_tiles_dispatch(0u, 2u);
    const uint32_t join_start = profile ? micros() : 0u;
    while (__atomic_load_n(&tri3d_tex_worker_state, __ATOMIC_ACQUIRE) != TRI3D_CORE1_DONE)
      audio_core0_poll_if_due();
    if (profile) tri3d_stage_stats[39u - 35u] += (uint32_t)(micros() - join_start);
    __atomic_store_n(&tri3d_tex_worker_state, TRI3D_CORE1_IDLE, __ATOMIC_RELEASE);
  }
  else tri3d_process_textured_tiles_dispatch(0u, 1u);
  if (tri3d_stats_enabled)
  {
    ++tri3d_tile_stats[0]; tri3d_tile_stats[1] += tri3d_tex_count;
    for (uint32_t c = 0; c < 2u; ++c)
    {
      for (uint32_t i = 0; i < 10u; ++i) tri3d_sampler_stats[i] += tri3d_tex_counts[c][i];
      for (uint32_t i = 0; i < 4u; ++i) tri3d_tile_stats[2u+i] += tri3d_tex_counts[c][10u+i];
    }
  }
  if (profile)
  {
    for (uint32_t c = 0; c < 2u; ++c)
    {
      const Tri3dTexStageCounts& s = tri3d_tex_stage_counts[c];
      uint32_t base = 40u + c * 4u - 35u;
      tri3d_stage_stats[base] += s.load_us;
      tri3d_stage_stats[base + 1u] += s.raster_us;
      tri3d_stage_stats[base + 2u] += s.commit_us;
      tri3d_stage_stats[base + 3u] += s.active_us;
      if (tri3d_pixel_stats)
      {
        tri3d_stage_stats[49u - 35u] += s.occupied;
        tri3d_stage_stats[50u - 35u] += s.memberships;
        tri3d_stage_stats[51u - 35u] += s.command_bytes;
        tri3d_stage_stats[52u - 35u] += s.staged_pixels;
        tri3d_stage_stats[53u - 35u] += s.valid_loads;
        tri3d_stage_stats[54u - 35u] += s.dirty_depth;
        tri3d_stage_stats[55u - 35u] += s.dirty_words;
        tri3d_stage_stats[56u - 35u] += s.bin_bytes;
        tri3d_stage_stats[58u + c - 35u] += s.occupied;
      }
    }
  }
  if (tri3d_tex_occlusion && tri3d_pixel_stats)
    tri3d_stage_stats[28] += tri3d_occlusion.updates[0] + tri3d_occlusion.updates[1];
  tri3d_tex_count = 0; tri3d_tex_reset_bins(); tri3d_mark_depth_non_pristine();
  if (profile) tri3d_stage_stats[38u - 35u] += (uint32_t)(micros() - flush_start);
}

// These cache only computations invariant during one synchronous API call.
// Mask classification never survives end: programs may edit their own assets
// between calls. No queued command refers to either cache.
static struct Tri3dGeometryListCache
{
  float near_z, far_z, near_inv_z, far_inv_z, depth_mul;
  const var* mask;
  uint32_t texels;
  uint8_t depth_valid, mask_valid, mask_class;
} tri3d_geometry_cache;

static void tri3d_geometry_cache_reset()
{
  tri3d_geometry_cache.depth_valid = 0u;
  tri3d_geometry_cache.mask_valid = 0u;
  tri3d_geometry_cache.mask = nullptr;
}

bool tri3d_begin_textured_list(var* color, var* depth, var* stamp, uint8_t frame, uint32_t mode)
{
  if (tri3d_tex_active || !color || !depth || !stamp || !frame ||
      (mode & ~0x103u) || (mode & 3u) > 2u ||
      ((mode & 0x100u) && !(mode & 3u)) ||
      ((mode & 3u) && !tri3d_tile_scratch_enabled)) return false;
  tri3d_flush_solid_batch();
  tri3d_tex_count = 0; tri3d_tex_mode = mode & 3u; tri3d_tex_frame = frame;
  tri3d_tex_occlusion = (mode & 0x100u) ? 1u : 0u;
  if (tri3d_tex_occlusion && (!tri3d_occlusion_initialized || tri3d_occlusion.depth != depth ||
      tri3d_occlusion.stamp != stamp || tri3d_occlusion.frame != frame))
  {
    tri3d_occlusion_invalidate();
    tri3d_occlusion.depth = depth; tri3d_occlusion.stamp = stamp;
    tri3d_occlusion.frame = frame;
  }
  tri3d_tex_color = color; tri3d_tex_depth = depth; tri3d_tex_stamp = stamp;
  tri3d_geometry_cache_reset();
  if (mode) tri3d_tex_reset_bins();
  tri3d_tex_active = 1u;
  return true;
}
bool tri3d_end_textured_list()
{
  if (!tri3d_tex_active) return false;
  tri3d_flush_textured_list(); tri3d_tex_active = 0u;
  tri3d_geometry_cache_reset();
  tri3d_tex_color = tri3d_tex_depth = tri3d_tex_stamp = nullptr;
  return true;
}

// This setup routine has more than forty scalar arguments. Keep it inlined in
// its projected-geometry callers, as in the Build 22 linked firmware: outlining
// after adding the packed caller otherwise spills/copies a large argument list
// for every triangle. This affects FLASH code size, not static SRAM placement.
static inline __attribute__((always_inline)) void tri3d_raster_projected_tri_tex(var* gfx_color_mem,
                                                                             var* gfx_z_mem,
                                                                             var* gfx_z_stamp_mem,
                                                                             var* tex_mem,
                                                                             var* mask_mem,
                                                                             uint8_t frame_id,
                                                                             uint32_t tex_base,
                                                                             uint32_t mask_base,
                                                                             uint8_t use_mask,
                                                                             int32_t tex_w,
                                                                             int32_t tex_h,
                                                                             uint32_t flags,
                                                                             uint8_t cull_positive_area,
                                                                             uint8_t cull_negative_area,
                                                                             int32_t x0,
                                                                             int32_t y0,
                                                                             float x0f,
                                                                             float y0f,
                                                                             float z0,
                                                                             float uq0,
                                                                             float vq0,
                                                                             float q0,
                                                                             int32_t x1,
                                                                             int32_t y1,
                                                                             float x1f,
                                                                             float y1f,
                                                                             float z1,
                                                                             float uq1,
                                                                             float vq1,
                                                                             float q1,
                                                                             int32_t x2,
                                                                             int32_t y2,
                                                                             float x2f,
                                                                             float y2f,
                                                                             float z2,
                                                                             float uq2,
                                                                             float vq2,
                                                                             float q2)
{
  tri3d_stat_add(TRI3D_STAT_TRIANGLES_SUBMITTED, 1);
  const float cull_area = ((x1f - x0f) * (y2f - y0f)) - ((x2f - x0f) * (y1f - y0f));
  if (((cull_area > 0) && cull_positive_area) || ((cull_area < 0) && cull_negative_area))
  {
    tri3d_stat_add(TRI3D_STAT_TRIANGLES_CULLED, 1);
    return;
  }

  #define TRI3D_TEX_SWAP(ax, ay, asx, asy, az, auq, avq, aq, bx, by, bsx, bsy, bz, buq, bvq, bq) do { \
      int32_t __tx = (ax); (ax) = (bx); (bx) = __tx; \
      int32_t __ty = (ay); (ay) = (by); (by) = __ty; \
      float __tsx = (asx); (asx) = (bsx); (bsx) = __tsx; \
      float __tsy = (asy); (asy) = (bsy); (bsy) = __tsy; \
      float __tz = (az); (az) = (bz); (bz) = __tz; \
      float __tuq = (auq); (auq) = (buq); (buq) = __tuq; \
      float __tvq = (avq); (avq) = (bvq); (bvq) = __tvq; \
      float __tq = (aq); (aq) = (bq); (bq) = __tq; \
    } while (0)

  if (y0f > y1f) TRI3D_TEX_SWAP(x0, y0, x0f, y0f, z0, uq0, vq0, q0, x1, y1, x1f, y1f, z1, uq1, vq1, q1);
  if (y1f > y2f) TRI3D_TEX_SWAP(x1, y1, x1f, y1f, z1, uq1, vq1, q1, x2, y2, x2f, y2f, z2, uq2, vq2, q2);
  if (y0f > y1f) TRI3D_TEX_SWAP(x0, y0, x0f, y0f, z0, uq0, vq0, q0, x1, y1, x1f, y1f, z1, uq1, vq1, q1);

  #undef TRI3D_TEX_SWAP

  const float area = ((x1f - x0f) * (y2f - y0f)) - ((x2f - x0f) * (y1f - y0f));
  if (area == 0.0f || y2f < -0.5f || y0f >= 239.5f)
  {
    return;
  }

  tri3d_stat_add(TRI3D_STAT_TRIANGLES_RASTERIZED, 1);

  const float inv_area = 1.0f / area;
  const float dzdx =
    (((z1 - z0) * (y2f - y0f)) - ((z2 - z0) * (y1f - y0f))) * inv_area;
  const float dzdy =
    (((x1f - x0f) * (z2 - z0)) - ((x2f - x0f) * (z1 - z0))) * inv_area;
  const float duqdx =
    (((uq1 - uq0) * (y2f - y0f)) - ((uq2 - uq0) * (y1f - y0f))) * inv_area;
  const float dvqdx =
    (((vq1 - vq0) * (y2f - y0f)) - ((vq2 - vq0) * (y1f - y0f))) * inv_area;
  const float dqdx =
    (((q1 - q0) * (y2f - y0f)) - ((q2 - q0) * (y1f - y0f))) * inv_area;
  const float duqdy =
    (((x1f - x0f) * (uq2 - uq0)) - ((x2f - x0f) * (uq1 - uq0))) * inv_area;
  const float dvqdy =
    (((x1f - x0f) * (vq2 - vq0)) - ((x2f - x0f) * (vq1 - vq0))) * inv_area;
  const float dqdy =
    (((x1f - x0f) * (q2 - q0)) - ((x2f - x0f) * (q1 - q0))) * inv_area;
  const int32_t x0edge = tri3d_edge_fx_from_float(x0f);
  const int32_t y0edge = tri3d_edge_fx_from_float(y0f);
  const int32_t x1edge = tri3d_edge_fx_from_float(x1f);
  const int32_t y1edge = tri3d_edge_fx_from_float(y1f);
  const int32_t x2edge = tri3d_edge_fx_from_float(x2f);
  const int32_t y2edge = tri3d_edge_fx_from_float(y2f);
  const int64_t dx01 = tri3d_edge_fx_slope(x0edge, y0edge, x1edge, y1edge);
  const int64_t dx02 = tri3d_edge_fx_slope(x0edge, y0edge, x2edge, y2edge);
  const int64_t dx12 = tri3d_edge_fx_slope(x1edge, y1edge, x2edge, y2edge);
  const uint32_t stamp_word_full = (uint32_t)frame_id * 0x01010101u;

  uint8_t fast_16_safe = 0;
  if (!use_mask && tex_w == 16 && tex_h == 16 && ((flags & 0x3u) == 0x3u))
  {
    // Conservatively validate the entire pixel-center bounding box rather than
    // only the three projected vertices.
    // q, uq and vq are affine over the triangle; their extrema over this box are
    // therefore at its four corners.  A positive minimum q and bounded numerators
    // prove every per-pixel floor conversion in the fast sampler is well-defined.
    int32_t min_x = x0;
    int32_t max_x = x0;
    int32_t min_y = y0;
    int32_t max_y = y0;
    if (x1 < min_x) min_x = x1; if (x1 > max_x) max_x = x1;
    if (x2 < min_x) min_x = x2; if (x2 > max_x) max_x = x2;
    if (y1 < min_y) min_y = y1; if (y1 > max_y) max_y = y1;
    if (y2 < min_y) min_y = y2; if (y2 > max_y) max_y = y2;
    if (min_x < 0) min_x = 0; if (max_x > 319) max_x = 319;
    if (min_y < 0) min_y = 0; if (max_y > 239) max_y = 239;

    float min_q = 0.0f;
    float max_abs_uq = 0.0f;
    float max_abs_vq = 0.0f;
    uint8_t bounds_finite = 1;
    uint8_t first_corner = 1;
    for (uint8_t corner = 0; corner < 4; corner++)
    {
      const float cx = (float)((corner & 1u) ? max_x : min_x) + 0.5f;
      const float cy = (float)((corner & 2u) ? max_y : min_y) + 0.5f;
      const float dx = cx - x0f;
      const float dy = cy - y0f;
      const float corner_q = q0 + (dqdx * dx) + (dqdy * dy);
      const float corner_uq = uq0 + (duqdx * dx) + (duqdy * dy);
      const float corner_vq = vq0 + (dvqdx * dx) + (dvqdy * dy);
      const float abs_uq = corner_uq < 0.0f ? -corner_uq : corner_uq;
      const float abs_vq = corner_vq < 0.0f ? -corner_vq : corner_vq;
      if (!(corner_q <= __FLT_MAX__ && corner_q >= -__FLT_MAX__ &&
            corner_uq <= __FLT_MAX__ && corner_uq >= -__FLT_MAX__ &&
            corner_vq <= __FLT_MAX__ && corner_vq >= -__FLT_MAX__))
      {
        bounds_finite = 0;
      }
      if (first_corner || corner_q < min_q) min_q = corner_q;
      if (first_corner || abs_uq > max_abs_uq) max_abs_uq = abs_uq;
      if (first_corner || abs_vq > max_abs_vq) max_abs_vq = abs_vq;
      first_corner = 0;
    }

    const float coord_limit = 2147000000.0f;
    if (bounds_finite && min_q > 0.0f &&
        max_abs_uq <= (min_q * coord_limit) &&
        max_abs_vq <= (min_q * coord_limit))
    {
      fast_16_safe = 1;
    }
  }

  if (tri3d_tex_active && tri3d_tex_mode)
  {
    if (tri3d_tex_count == TRI3D_TEX_COMMANDS) tri3d_flush_textured_list();
    // Build the optimized command on the caller's bounded stack, not through
    // the external PSRAM queue. In particular, its binning bounds and edge
    // start calculations must not read back the fields just written to PSRAM.
    // The retained geometry diagnostic selects the original construction for
    // same-build comparisons. No queue command is visible to a worker until
    // the synchronous flush below, so publishing after binning is safe.
    Tri3dTexCommand local_command;
    const bool local_construction = tri3d_geometry_optimized_enabled();
    Tri3dTexCommand& p = local_construction ? local_command
                                           : tri3d_tex_commands()[tri3d_tex_count];
    p.texture = tex_mem; p.mask = mask_mem;
    p.tex_base = tex_base; p.mask_base = mask_base; p.flags = flags;
    p.width = tex_w; p.height = tex_h; p.use_mask = use_mask; p.fast16 = fast_16_safe;
    p.x0 = x0f; p.y0 = y0f; p.z0 = z0; p.uq0 = uq0; p.vq0 = vq0; p.q0 = q0;
    p.dzdx = dzdx; p.dzdy = dzdy; p.duqdx = duqdx; p.duqdy = duqdy;
    p.dvqdx = dvqdx; p.dvqdy = dvqdy; p.dqdx = dqdx; p.dqdy = dqdy;
    p.sy0 = tri3d_edge_fx_ceil(y0edge - TRI3D_EDGE_FX_HALF);
    p.sy1 = tri3d_edge_fx_ceil(y1edge - TRI3D_EDGE_FX_HALF);
    p.sy2 = tri3d_edge_fx_ceil(y2edge - TRI3D_EDGE_FX_HALF);
    p.uxa = tri3d_edge_fx_at_y(x0edge, y0edge, dx02, p.sy0);
    p.uxb = tri3d_edge_fx_at_y(x0edge, y0edge, dx01, p.sy0);
    p.lxa = tri3d_edge_fx_at_y(x0edge, y0edge, dx02, p.sy1);
    p.lxb = tri3d_edge_fx_at_y(x1edge, y1edge, dx12, p.sy1);
    p.dx01 = dx01; p.dx02 = dx02; p.dx12 = dx12;
    int32_t minx = x0 < x1 ? x0 : x1, maxx = x0 > x1 ? x0 : x1;
    if (x2 < minx) minx = x2; if (x2 > maxx) maxx = x2;
    // Conservative bounds include rounding of projected edge coordinates.
    p.xmin = minx > 1 ? minx - 1 : 0;
    p.xmax = maxx < 318 ? maxx + 2 : 320;
    p.ymin = p.sy0 < 0 ? 0 : p.sy0; p.ymax = p.sy2 > 240 ? 240 : p.sy2;
    if (p.xmin < p.xmax && p.ymin < p.ymax)
    {
      if (tri3d_tex_occlusion && (tri3d_pixel_stats
          ? tri3d_occlusion_reject<true>(p) : tri3d_occlusion_reject<false>(p))) return;
      for (uint32_t t = (uint32_t)p.ymin / TRI3D_TEX_TILE_ROWS;
           t <= (uint32_t)(p.ymax - 1) / TRI3D_TEX_TILE_ROWS; ++t)
      {
        Tri3dTexBin& bin = tri3d_tex_bins()[t];
        if (!bin.xmax) { bin.xmin = p.xmin; bin.xmax = p.xmax; bin.ymin = p.ymin; bin.ymax = p.ymax; }
        else
        {
          if (p.xmin < bin.xmin) bin.xmin = p.xmin; if (p.xmax > bin.xmax) bin.xmax = p.xmax;
          if (p.ymin < bin.ymin) bin.ymin = p.ymin; if (p.ymax > bin.ymax) bin.ymax = p.ymax;
        }
        bin.members[tri3d_tex_count >> 5] |= 1u << (tri3d_tex_count & 31u);
      }
      if (local_construction)
        memcpy(tri3d_tex_commands() + tri3d_tex_count, &local_command,
               sizeof(local_command));
      ++tri3d_tex_count;
    }
    return;
  }

  auto draw_half = [&](int32_t y_start, int32_t y_end, int64_t xa, int64_t dxa, int64_t xb, int64_t dxb)
  {
    int32_t y = y_start;
    if (y < 0)
    {
      const int32_t skip = -y;
      xa += dxa * skip;
      xb += dxb * skip;
      y = 0;
    }
    if (y_end > 240) y_end = 240;
    for (; y < y_end; y++)
    {
      const int64_t min_edge = xa < xb ? xa : xb;
      const int64_t max_edge = xa < xb ? xb : xa;
      const int32_t left = tri3d_edge_fx_ceil(min_edge - TRI3D_EDGE_FX_HALF);
      const int32_t right = tri3d_edge_fx_ceil(max_edge - TRI3D_EDGE_FX_HALF) - 1;
      if (right >= 0 && left < 320)
      {
        const int32_t draw_left = left < 0 ? 0 : left;
        const int32_t draw_right = right >= 320 ? 319 : right;
        const float dx = ((float)draw_left + 0.5f) - x0f;
        const float dy = ((float)y + 0.5f) - y0f;
        const float z = z0 + (dzdx * dx) + (dzdy * dy);
        const float uq = uq0 + (duqdx * dx) + (duqdy * dy);
        const float vq = vq0 + (dvqdx * dx) + (dvqdy * dy);
        const float q = q0 + (dqdx * dx) + (dqdy * dy);
        if (use_mask)
        {
          tri3d_write_span_tex_z_mask(gfx_color_mem, gfx_z_mem, gfx_z_stamp_mem, tex_mem, mask_mem, frame_id,
                                      tex_base, mask_base, tex_w, tex_h, flags,
                                      y, draw_left, draw_right, uq, vq, q, duqdx, dvqdx, dqdx, z, dzdx);
        }
        else
        {
          tri3d_write_span_tex_z(gfx_color_mem, gfx_z_mem, gfx_z_stamp_mem, tex_mem, frame_id,
                                 stamp_word_full, tex_base, tex_w, tex_h, flags,
                                 y, draw_left, draw_right, uq, vq, q, duqdx, dvqdx, dqdx, z, dzdx,
                                 fast_16_safe);
        }
      }
      xa += dxa;
      xb += dxb;
    }
  };

  const int32_t scan_y0 = tri3d_edge_fx_ceil(y0edge - TRI3D_EDGE_FX_HALF);
  const int32_t scan_y1 = tri3d_edge_fx_ceil(y1edge - TRI3D_EDGE_FX_HALF);
  const int32_t scan_y2 = tri3d_edge_fx_ceil(y2edge - TRI3D_EDGE_FX_HALF);
  if (scan_y1 > scan_y0)
  {
    draw_half(scan_y0, scan_y1,
              tri3d_edge_fx_at_y(x0edge, y0edge, dx02, scan_y0), dx02,
              tri3d_edge_fx_at_y(x0edge, y0edge, dx01, scan_y0), dx01);
  }
  if (scan_y2 > scan_y1)
  {
    draw_half(scan_y1, scan_y2,
              tri3d_edge_fx_at_y(x0edge, y0edge, dx02, scan_y1), dx02,
              tri3d_edge_fx_at_y(x1edge, y1edge, dx12, scan_y1), dx12);
  }
}

static inline __attribute__((always_inline)) void tri3d_screen_clip_copy(uint8_t& poly_count,
                                          int32_t* poly_x,
                                          int32_t* poly_y,
                                          float* poly_z,
                                          uint8_t tmp_count,
                                          int32_t* tmp_x,
                                          int32_t* tmp_y,
                                          float* tmp_z)
{
  poly_count = tmp_count;
  for (uint8_t i = 0; i < poly_count; i++)
  {
    poly_x[i] = tmp_x[i];
    poly_y[i] = tmp_y[i];
    poly_z[i] = tmp_z[i];
  }
}

static inline __attribute__((always_inline)) void tri3d_screen_clip_copy_gouraud(uint8_t& poly_count,
                                                  int32_t* poly_x,
                                                  int32_t* poly_y,
                                                  float* poly_z,
                                                  float* poly_r,
                                                  float* poly_g,
                                                  float* poly_b,
                                                  uint8_t tmp_count,
                                                  int32_t* tmp_x,
                                                  int32_t* tmp_y,
                                                  float* tmp_z,
                                                  float* tmp_r,
                                                  float* tmp_g,
                                                  float* tmp_b)
{
  poly_count = tmp_count;
  for (uint8_t i = 0; i < poly_count; i++)
  {
    poly_x[i] = tmp_x[i];
    poly_y[i] = tmp_y[i];
    poly_z[i] = tmp_z[i];
    poly_r[i] = tmp_r[i];
    poly_g[i] = tmp_g[i];
    poly_b[i] = tmp_b[i];
  }
}

struct Tri3dNeutralVertex
{
  float x;
  float y;
  float z;
  float u;
  float v;
  float r;
  float g;
  float b;
};

static inline __attribute__((always_inline)) Tri3dNeutralVertex
tri3d_neutral_lerp(const Tri3dNeutralVertex& a,
                   const Tri3dNeutralVertex& b,
                   float t)
{
  Tri3dNeutralVertex out;
  out.x = a.x + ((b.x - a.x) * t);
  out.y = a.y + ((b.y - a.y) * t);
  out.z = a.z + ((b.z - a.z) * t);
  out.u = a.u + ((b.u - a.u) * t);
  out.v = a.v + ((b.v - a.v) * t);
  out.r = a.r + ((b.r - a.r) * t);
  out.g = a.g + ((b.g - a.g) * t);
  out.b = a.b + ((b.b - a.b) * t);
  return out;
}

static uint8_t tri3d_neutral_clip_z(const Tri3dNeutralVertex* input,
                                    uint8_t input_count,
                                    Tri3dNeutralVertex* output,
                                    float plane,
                                    bool keep_greater)
{
  if (input_count == 0) return 0;
  uint8_t output_count = 0;
  for (uint8_t current = 0; current < input_count; current++)
  {
    const uint8_t previous = current == 0 ? (uint8_t)(input_count - 1u)
                                           : (uint8_t)(current - 1u);
    const bool previous_inside = keep_greater ? input[previous].z >= plane
                                               : input[previous].z <= plane;
    const bool current_inside = keep_greater ? input[current].z >= plane
                                              : input[current].z <= plane;
    if (previous_inside != current_inside && output_count < 8u)
    {
      const float denominator = input[current].z - input[previous].z;
      const float t = denominator != 0.0f
                        ? (plane - input[previous].z) / denominator
                        : 0.0f;
      output[output_count++] = tri3d_neutral_lerp(input[previous], input[current], t);
      output[output_count - 1u].z = plane;
    }
    if (current_inside && output_count < 8u)
    {
      output[output_count++] = input[current];
    }
  }
  return output_count;
}

void tri3d_draw_screen_gouraud(var *color,
                               var *depth,
                               var *depth_stamp,
                               uint8_t frame_id,
                               const Graphics3dVertex *vertices)
{
  if (!color || !depth || !depth_stamp || !vertices) return;
  tri3d_flush_solid_batch();
  tri3d_raster_projected_tri_gouraud(
    color, depth, depth_stamp, frame_id, 0, 0,
    vertices[0].x, vertices[0].y, vertices[0].z,
    tri3d_color_r_f(vertices[0].color), tri3d_color_g_f(vertices[0].color), tri3d_color_b_f(vertices[0].color),
    vertices[1].x, vertices[1].y, vertices[1].z,
    tri3d_color_r_f(vertices[1].color), tri3d_color_g_f(vertices[1].color), tri3d_color_b_f(vertices[1].color),
    vertices[2].x, vertices[2].y, vertices[2].z,
    tri3d_color_r_f(vertices[2].color), tri3d_color_g_f(vertices[2].color), tri3d_color_b_f(vertices[2].color));
  tri3d_mark_depth_non_pristine();
}

// Screen-space clipping is required before the fixed-point edge setup used by
// the rasterizers. The original renderer performed this step, while the
// direct NARM path initially stopped after near/far clipping and allowed a
// close polygon to project far outside the int32 fixed-point range and caused
// missing faces, long stray spans, and unstable edge interpolation.
struct Tri3dProjectedVertex
{
  float x;
  float y;
  float z;
  float uq;
  float vq;
  float q;
  float r;
  float g;
  float b;
};

static inline Tri3dProjectedVertex tri3d_projected_lerp(
  const Tri3dProjectedVertex& a,
  const Tri3dProjectedVertex& b,
  float t)
{
  Tri3dProjectedVertex result;
  result.x = a.x + (b.x - a.x) * t;
  result.y = a.y + (b.y - a.y) * t;
  result.z = a.z + (b.z - a.z) * t;
  result.uq = a.uq + (b.uq - a.uq) * t;
  result.vq = a.vq + (b.vq - a.vq) * t;
  result.q = a.q + (b.q - a.q) * t;
  result.r = a.r + (b.r - a.r) * t;
  result.g = a.g + (b.g - a.g) * t;
  result.b = a.b + (b.b - a.b) * t;
  return result;
}

enum Tri3dScreenClipPlane : uint8_t
{
  TRI3D_CLIP_LEFT = 0,
  TRI3D_CLIP_RIGHT,
  TRI3D_CLIP_TOP,
  TRI3D_CLIP_BOTTOM
};

static uint8_t tri3d_clip_projected_screen_plane(
  const Tri3dProjectedVertex *input,
  uint8_t input_count,
  Tri3dProjectedVertex *output,
  Tri3dScreenClipPlane plane)
{
  if (!input || !output || input_count < 3u || input_count > 16u) return 0u;
  const bool x_axis = plane == TRI3D_CLIP_LEFT || plane == TRI3D_CLIP_RIGHT;
  const bool keep_greater = plane == TRI3D_CLIP_LEFT || plane == TRI3D_CLIP_TOP;
  const float boundary = plane == TRI3D_CLIP_RIGHT ? 320.0f
                         : plane == TRI3D_CLIP_BOTTOM ? 240.0f : 0.0f;
  uint8_t output_count = 0u;
  for (uint8_t current = 0u; current < input_count; ++current)
  {
    const uint8_t previous = current == 0u ? (uint8_t)(input_count - 1u)
                                            : (uint8_t)(current - 1u);
    const float previous_coordinate = x_axis ? input[previous].x : input[previous].y;
    const float current_coordinate = x_axis ? input[current].x : input[current].y;
    const bool previous_inside = keep_greater
                                   ? previous_coordinate >= boundary
                                   : previous_coordinate <= boundary;
    const bool current_inside = keep_greater
                                  ? current_coordinate >= boundary
                                  : current_coordinate <= boundary;
    if (previous_inside != current_inside && output_count < 16u)
    {
      const float denominator = current_coordinate - previous_coordinate;
      const float t = denominator != 0.0f
                        ? (boundary - previous_coordinate) / denominator : 0.0f;
      output[output_count] = tri3d_projected_lerp(input[previous], input[current], t);
      if (x_axis) output[output_count].x = boundary;
      else output[output_count].y = boundary;
      ++output_count;
    }
    if (current_inside && output_count < 16u)
    {
      output[output_count++] = input[current];
    }
  }
  return output_count;
}

bool tri3d_draw_projected(var *color,
                          var *depth,
                          var *depth_stamp,
                          uint8_t frame_id,
                          const Graphics3dVertex *vertices,
                          uint8_t vertex_count,
                          const Graphics3dTransform *transform,
                          uint32_t flat_color,
                          bool gouraud,
                          const var *texture_words,
                          int32_t texture_width,
                          int32_t texture_height,
                          uint32_t flags,
                          bool tiled_uv,
                          const var *mask_words)
{
  if (!color || !depth || !depth_stamp || !vertices || !transform ||
      (vertex_count != 3u && vertex_count != 4u))
  {
    return false;
  }
  const bool textured = texture_words != nullptr;
  if (tri3d_tex_active && (!textured || color != tri3d_tex_color ||
      depth != tri3d_tex_depth || depth_stamp != tri3d_tex_stamp || frame_id != tri3d_tex_frame))
    return false;
  if (textured && (texture_width <= 0 || texture_height <= 0)) return false;

  const float* m = transform->value;
  const float near_z = m[16];
  const float far_z = m[17];
  if (!(near_z == near_z) || near_z <= 0.0f) return false;
  const bool far_clip = (far_z == far_z) && far_z > near_z;
  const float near_inv_z = 1.0f / near_z;
  const float far_inv_z = far_clip ? (1.0f / far_z) : 0.0f;
  const float depth_mul = near_inv_z > far_inv_z
                            ? 4294967295.0f / (near_inv_z - far_inv_z)
                            : 0.0f;
  if (!(depth_mul > 0.0f)) return false;

  Tri3dNeutralVertex camera[8];
  Tri3dNeutralVertex near_clipped[8];
  Tri3dNeutralVertex clipped[8];
  for (uint8_t i = 0; i < vertex_count; i++)
  {
    const Graphics3dVertex& source = vertices[i];
    Tri3dNeutralVertex& destination = camera[i];
    destination.x = source.x * m[0] + source.y * m[1] + source.z * m[2] + m[3];
    destination.y = source.x * m[4] + source.y * m[5] + source.z * m[6] + m[7];
    destination.z = source.x * m[8] + source.y * m[9] + source.z * m[10] + m[11];
    destination.u = tiled_uv ? source.u * (float)texture_width : source.u;
    destination.v = tiled_uv ? source.v * (float)texture_height : source.v;
    destination.r = tri3d_color_r_f(source.color);
    destination.g = tri3d_color_g_f(source.color);
    destination.b = tri3d_color_b_f(source.color);
    if (!(destination.x == destination.x) || !(destination.y == destination.y) ||
        !(destination.z == destination.z) || !(destination.u == destination.u) ||
        !(destination.v == destination.v))
    {
      return false;
    }
  }

  uint8_t count = tri3d_neutral_clip_z(camera, vertex_count, near_clipped, near_z, true);
  if (count < 3u) return true;
  if (far_clip)
  {
    count = tri3d_neutral_clip_z(near_clipped, count, clipped, far_z, false);
  }
  else
  {
    for (uint8_t i = 0; i < count; i++) clipped[i] = near_clipped[i];
  }
  if (count < 3u) return true;

  uint8_t effective_mask = mask_words != nullptr ? 1u : 0u;
  if (effective_mask)
  {
    const uint32_t texels = (uint32_t)texture_width * (uint32_t)texture_height;
    const uint8_t mask_class = tri3d_classify_mask_words((var*)mask_words, 0u, texels);
    if (mask_class == TRI3D_MASK_CLASS_EMPTY) return true;
    if (mask_class == TRI3D_MASK_CLASS_OPAQUE) effective_mask = 0u;
  }

  Tri3dProjectedVertex projected_a[16] = {};
  Tri3dProjectedVertex projected_b[16] = {};
  for (uint8_t i = 0; i < count; i++)
  {
    const float inv_z = 1.0f / clipped[i].z;
    projected_a[i].x = m[14] + clipped[i].x * m[12] * inv_z;
    projected_a[i].y = m[15] - clipped[i].y * m[13] * inv_z;
    float mapped_depth = (near_inv_z - inv_z) * depth_mul;
    if (mapped_depth < 0.0f) mapped_depth = 0.0f;
    if (mapped_depth > 4294967040.0f) mapped_depth = 4294967040.0f;
    projected_a[i].z = mapped_depth;
    projected_a[i].q = inv_z * TRI3D_TEX_PERSPECTIVE_SCALE;
    projected_a[i].uq = clipped[i].u * projected_a[i].q;
    projected_a[i].vq = clipped[i].v * projected_a[i].q;
    projected_a[i].r = clipped[i].r;
    projected_a[i].g = clipped[i].g;
    projected_a[i].b = clipped[i].b;
    if (!(projected_a[i].x == projected_a[i].x) ||
        !(projected_a[i].y == projected_a[i].y) ||
        !(projected_a[i].z == projected_a[i].z) ||
        !(projected_a[i].uq == projected_a[i].uq) ||
        !(projected_a[i].vq == projected_a[i].vq) ||
        !(projected_a[i].q > 0.0f)) return false;
  }

  // Clip all interpolants in screen space before
  // triangulation.  Apart from preserving visual parity this ensures every
  // coordinate passed to 16.16 edge setup is in a small, defined range.
  count = tri3d_clip_projected_screen_plane(projected_a, count, projected_b,
                                             TRI3D_CLIP_LEFT);
  if (count < 3u) return true;
  count = tri3d_clip_projected_screen_plane(projected_b, count, projected_a,
                                             TRI3D_CLIP_RIGHT);
  if (count < 3u) return true;
  count = tri3d_clip_projected_screen_plane(projected_a, count, projected_b,
                                             TRI3D_CLIP_TOP);
  if (count < 3u) return true;
  count = tri3d_clip_projected_screen_plane(projected_b, count, projected_a,
                                             TRI3D_CLIP_BOTTOM);
  if (count < 3u) return true;

  const uint8_t cull_positive = (uint8_t)(((textured ? flags : (gouraud ? vertices[0].color : flat_color)) & 0x100u) != 0u);
  const uint8_t cull_negative = (uint8_t)(((textured ? flags : (gouraud ? vertices[0].color : flat_color)) & 0x200u) != 0u);
  if (gouraud || textured) tri3d_flush_solid_batch();

  for (uint8_t i = 1; i + 1u < count; i++)
  {
    if (textured)
    {
      tri3d_raster_projected_tri_tex(
        color, depth, depth_stamp, (var*)texture_words, (var*)mask_words, frame_id,
        0u, 0u, effective_mask, texture_width, texture_height, flags,
        cull_positive, cull_negative,
        tri3d_round_i32(projected_a[0].x), tri3d_round_i32(projected_a[0].y), projected_a[0].x, projected_a[0].y, projected_a[0].z, projected_a[0].uq, projected_a[0].vq, projected_a[0].q,
        tri3d_round_i32(projected_a[i].x), tri3d_round_i32(projected_a[i].y), projected_a[i].x, projected_a[i].y, projected_a[i].z, projected_a[i].uq, projected_a[i].vq, projected_a[i].q,
        tri3d_round_i32(projected_a[i + 1u].x), tri3d_round_i32(projected_a[i + 1u].y), projected_a[i + 1u].x, projected_a[i + 1u].y, projected_a[i + 1u].z, projected_a[i + 1u].uq, projected_a[i + 1u].vq, projected_a[i + 1u].q);
    }
    else if (gouraud)
    {
      tri3d_raster_projected_tri_gouraud(
        color, depth, depth_stamp, frame_id, cull_positive, cull_negative,
        projected_a[0].x, projected_a[0].y, projected_a[0].z, projected_a[0].r, projected_a[0].g, projected_a[0].b,
        projected_a[i].x, projected_a[i].y, projected_a[i].z, projected_a[i].r, projected_a[i].g, projected_a[i].b,
        projected_a[i + 1u].x, projected_a[i + 1u].y, projected_a[i + 1u].z, projected_a[i + 1u].r, projected_a[i + 1u].g, projected_a[i + 1u].b);
    }
    else
    {
      tri3d_raster_projected_tri(color, depth, depth_stamp, frame_id,
                                 flat_color & 0xffu, cull_positive, cull_negative,
                                 projected_a[0].x, projected_a[0].y, projected_a[0].z,
                                 projected_a[i].x, projected_a[i].y, projected_a[i].z,
                                 projected_a[i + 1u].x, projected_a[i + 1u].y, projected_a[i + 1u].z);
    }
  }
  if (gouraud || textured) tri3d_mark_depth_non_pristine();
  return true;
}

// Textured lists have no RGB interpolants. Keep precisely the original float
// expression/clip order but avoid expanding XYZUV to Graphics3dVertex and then
// copying eight/nine-float vertices through six wholly-inside clip planes.
struct Tri3dTexturedCameraVertex { float x, y, z, u, v; };
struct Tri3dTexturedProjectedVertex { float x, y, z, uq, vq, q; };

static uint8_t tri3d_textured_clip_z(const Tri3dTexturedCameraVertex* input,
                    uint8_t input_count, Tri3dTexturedCameraVertex* output,
                    float plane, bool keep_greater)
{
  uint8_t output_count = 0u;
  for (uint8_t current = 0u; current < input_count; ++current)
  {
    const uint8_t previous = current == 0u ? (uint8_t)(input_count - 1u)
                                           : (uint8_t)(current - 1u);
    const bool previous_inside = keep_greater ? input[previous].z >= plane : input[previous].z <= plane;
    const bool current_inside = keep_greater ? input[current].z >= plane : input[current].z <= plane;
    if (previous_inside != current_inside && output_count < 8u)
    {
      const float denominator = input[current].z - input[previous].z;
      const float t = denominator != 0.0f ? (plane - input[previous].z) / denominator : 0.0f;
      const Tri3dTexturedCameraVertex& a = input[previous];
      const Tri3dTexturedCameraVertex& b = input[current];
      Tri3dTexturedCameraVertex& out = output[output_count++];
      out.x = a.x + ((b.x - a.x) * t);
      out.y = a.y + ((b.y - a.y) * t);
      out.z = plane;
      out.u = a.u + ((b.u - a.u) * t);
      out.v = a.v + ((b.v - a.v) * t);
    }
    if (current_inside && output_count < 8u) output[output_count++] = input[current];
  }
  return output_count;
}

static inline __attribute__((always_inline)) uint8_t tri3d_textured_clip_screen(const Tri3dTexturedProjectedVertex* input,
                    uint8_t input_count, Tri3dTexturedProjectedVertex* output,
                    Tri3dScreenClipPlane plane)
{
  const bool x_axis = plane == TRI3D_CLIP_LEFT || plane == TRI3D_CLIP_RIGHT;
  const bool keep_greater = plane == TRI3D_CLIP_LEFT || plane == TRI3D_CLIP_TOP;
  const float boundary = plane == TRI3D_CLIP_RIGHT ? 320.0f
                         : plane == TRI3D_CLIP_BOTTOM ? 240.0f : 0.0f;
  uint8_t output_count = 0u;
  for (uint8_t current = 0u; current < input_count; ++current)
  {
    const uint8_t previous = current == 0u ? (uint8_t)(input_count - 1u) : (uint8_t)(current - 1u);
    const float previous_coordinate = x_axis ? input[previous].x : input[previous].y;
    const float current_coordinate = x_axis ? input[current].x : input[current].y;
    const bool previous_inside = keep_greater ? previous_coordinate >= boundary : previous_coordinate <= boundary;
    const bool current_inside = keep_greater ? current_coordinate >= boundary : current_coordinate <= boundary;
    if (previous_inside != current_inside && output_count < 16u)
    {
      const float denominator = current_coordinate - previous_coordinate;
      const float t = denominator != 0.0f ? (boundary - previous_coordinate) / denominator : 0.0f;
      const Tri3dTexturedProjectedVertex& a = input[previous];
      const Tri3dTexturedProjectedVertex& b = input[current];
      Tri3dTexturedProjectedVertex& out = output[output_count++];
      out.x = a.x + (b.x - a.x) * t;
      out.y = a.y + (b.y - a.y) * t;
      out.z = a.z + (b.z - a.z) * t;
      out.uq = a.uq + (b.uq - a.uq) * t;
      out.vq = a.vq + (b.vq - a.vq) * t;
      out.q = a.q + (b.q - a.q) * t;
      if (x_axis) out.x = boundary; else out.y = boundary;
    }
    if (current_inside && output_count < 16u) output[output_count++] = input[current];
  }
  return output_count;
}

bool tri3d_draw_projected_textured_packed(var *color, var *depth,
                          var *depth_stamp, uint8_t frame_id,
                          const float *xyzuv, uint8_t vertex_count,
                          const Graphics3dTransform *transform,
                          const var *texture_words, int32_t texture_width,
                          int32_t texture_height, uint32_t flags,
                          const var *mask_words)
{
  if (!color || !depth || !depth_stamp || !xyzuv || !transform || !texture_words ||
      (vertex_count != 3u && vertex_count != 4u) || texture_width <= 0 || texture_height <= 0)
    return false;
  if (tri3d_tex_active && (color != tri3d_tex_color || depth != tri3d_tex_depth ||
      depth_stamp != tri3d_tex_stamp || frame_id != tri3d_tex_frame)) return false;
  if (!tri3d_geometry_optimized_enabled())
  {
    Graphics3dVertex unpacked[4];
    for (uint8_t i = 0u; i < vertex_count; ++i)
    {
      const float* p = xyzuv + (uint32_t)i * 5u;
      unpacked[i] = {p[0], p[1], p[2], p[3], p[4], 0u};
    }
    return tri3d_draw_projected(color, depth, depth_stamp, frame_id, unpacked,
                               vertex_count, transform, 0u, false, texture_words,
                               texture_width, texture_height, flags, false, mask_words);
  }

  const float* m = transform->value;
  const float near_z = m[16], far_z = m[17];
  if (!(near_z == near_z) || near_z <= 0.0f) return false;
  const bool far_clip = (far_z == far_z) && far_z > near_z;
  float near_inv_z, far_inv_z, depth_mul;
  if (tri3d_tex_active && tri3d_geometry_cache.depth_valid &&
      near_z == tri3d_geometry_cache.near_z && far_z == tri3d_geometry_cache.far_z)
  {
    near_inv_z = tri3d_geometry_cache.near_inv_z;
    far_inv_z = tri3d_geometry_cache.far_inv_z;
    depth_mul = tri3d_geometry_cache.depth_mul;
  }
  else
  {
    near_inv_z = 1.0f / near_z;
    far_inv_z = far_clip ? (1.0f / far_z) : 0.0f;
    depth_mul = near_inv_z > far_inv_z ? 4294967295.0f / (near_inv_z - far_inv_z) : 0.0f;
    if (!(depth_mul > 0.0f)) return false;
    if (tri3d_tex_active)
    {
      tri3d_geometry_cache.near_z = near_z; tri3d_geometry_cache.far_z = far_z;
      tri3d_geometry_cache.near_inv_z = near_inv_z;
      tri3d_geometry_cache.far_inv_z = far_inv_z;
      tri3d_geometry_cache.depth_mul = depth_mul;
      tri3d_geometry_cache.depth_valid = 1u;
    }
  }

  Tri3dTexturedCameraVertex camera_a[8], camera_b[8];
  bool near_inside = true, far_inside = true;
  for (uint8_t i = 0u; i < vertex_count; ++i)
  {
    const float* source = xyzuv + (uint32_t)i * 5u;
    Tri3dTexturedCameraVertex& destination = camera_a[i];
    destination.x = source[0] * m[0] + source[1] * m[1] + source[2] * m[2] + m[3];
    destination.y = source[0] * m[4] + source[1] * m[5] + source[2] * m[6] + m[7];
    destination.z = source[0] * m[8] + source[1] * m[9] + source[2] * m[10] + m[11];
    destination.u = source[3]; destination.v = source[4];
    if (!(destination.x == destination.x) || !(destination.y == destination.y) ||
        !(destination.z == destination.z) || !(destination.u == destination.u) ||
        !(destination.v == destination.v)) return false;
    near_inside = near_inside && destination.z >= near_z;
    far_inside = far_inside && destination.z <= far_z;
  }
  uint8_t count = vertex_count;
  Tri3dTexturedCameraVertex* clipped = camera_a;
  Tri3dTexturedCameraVertex* other_camera = camera_b;
  if (!near_inside)
  {
    count = tri3d_textured_clip_z(clipped, count, other_camera, near_z, true);
    if (count < 3u) return true;
    Tri3dTexturedCameraVertex* swap = clipped; clipped = other_camera; other_camera = swap;
    // Near-plane intersections can round beyond an endpoint. Re-evaluate far
    // membership on the actual new vertices, rather than using an old outcode.
    far_inside = true;
    for (uint8_t i = 0u; i < count; ++i) far_inside = far_inside && clipped[i].z <= far_z;
  }
  if (far_clip && !far_inside)
  {
    count = tri3d_textured_clip_z(clipped, count, other_camera, far_z, false);
    if (count < 3u) return true;
    clipped = other_camera;
  }

  uint8_t effective_mask = mask_words ? 1u : 0u;
  if (effective_mask)
  {
    const uint32_t texels = (uint32_t)texture_width * (uint32_t)texture_height;
    uint8_t mask_class;
    if (tri3d_tex_active && tri3d_geometry_cache.mask_valid &&
        mask_words == tri3d_geometry_cache.mask && texels == tri3d_geometry_cache.texels)
      mask_class = tri3d_geometry_cache.mask_class;
    else
    {
      mask_class = tri3d_classify_mask_words((var*)mask_words, 0u, texels);
      if (tri3d_tex_active)
      {
        tri3d_geometry_cache.mask = mask_words; tri3d_geometry_cache.texels = texels;
        tri3d_geometry_cache.mask_class = mask_class; tri3d_geometry_cache.mask_valid = 1u;
      }
    }
    if (mask_class == TRI3D_MASK_CLASS_EMPTY) return true;
    if (mask_class == TRI3D_MASK_CLASS_OPAQUE) effective_mask = 0u;
  }

  Tri3dTexturedProjectedVertex projected_a[16], projected_b[16];
  uint8_t screen_outcode = 0u;
  for (uint8_t i = 0u; i < count; ++i)
  {
    const float inv_z = 1.0f / clipped[i].z;
    Tri3dTexturedProjectedVertex& p = projected_a[i];
    p.x = m[14] + clipped[i].x * m[12] * inv_z;
    p.y = m[15] - clipped[i].y * m[13] * inv_z;
    float mapped_depth = (near_inv_z - inv_z) * depth_mul;
    if (mapped_depth < 0.0f) mapped_depth = 0.0f;
    if (mapped_depth > 4294967040.0f) mapped_depth = 4294967040.0f;
    p.z = mapped_depth; p.q = inv_z * TRI3D_TEX_PERSPECTIVE_SCALE;
    p.uq = clipped[i].u * p.q; p.vq = clipped[i].v * p.q;
    if (!(p.x == p.x) || !(p.y == p.y) || !(p.z == p.z) ||
        !(p.uq == p.uq) || !(p.vq == p.vq) || !(p.q > 0.0f)) return false;
    if (!(p.x >= 0.0f)) screen_outcode |= 1u;
    if (!(p.x <= 320.0f)) screen_outcode |= 2u;
    if (!(p.y >= 0.0f)) screen_outcode |= 4u;
    if (!(p.y <= 240.0f)) screen_outcode |= 8u;
  }
  Tri3dTexturedProjectedVertex* projected = projected_a;
  Tri3dTexturedProjectedVertex* other_projected = projected_b;
  if (screen_outcode)
  {
    for (uint8_t plane = 0u; plane < 4u; ++plane)
    {
      const bool x_axis = plane < 2u;
      const bool keep_greater = (plane & 1u) == 0u;
      const float boundary = plane == TRI3D_CLIP_RIGHT ? 320.0f
                           : plane == TRI3D_CLIP_BOTTOM ? 240.0f : 0.0f;
      bool all_inside = true;
      for (uint8_t i = 0u; i < count; ++i)
      {
        const float coordinate = x_axis ? projected[i].x : projected[i].y;
        if (!(keep_greater ? coordinate >= boundary : coordinate <= boundary)) { all_inside = false; break; }
      }
      if (all_inside) continue;
      count = tri3d_textured_clip_screen(projected, count, other_projected, (Tri3dScreenClipPlane)plane);
      if (count < 3u) return true;
      Tri3dTexturedProjectedVertex* swap = projected; projected = other_projected; other_projected = swap;
    }
  }
  const uint8_t cull_positive = (uint8_t)((flags & 0x100u) != 0u);
  const uint8_t cull_negative = (uint8_t)((flags & 0x200u) != 0u);
  tri3d_flush_solid_batch();
  for (uint8_t i = 1u; i + 1u < count; ++i)
  {
    const Tri3dTexturedProjectedVertex& a = projected[0];
    const Tri3dTexturedProjectedVertex& b = projected[i];
    const Tri3dTexturedProjectedVertex& c = projected[i + 1u];
    tri3d_raster_projected_tri_tex(color, depth, depth_stamp,
      (var*)texture_words, (var*)mask_words, frame_id, 0u, 0u, effective_mask,
      texture_width, texture_height, flags, cull_positive, cull_negative,
      tri3d_round_i32(a.x), tri3d_round_i32(a.y), a.x, a.y, a.z, a.uq, a.vq, a.q,
      tri3d_round_i32(b.x), tri3d_round_i32(b.y), b.x, b.y, b.z, b.uq, b.vq, b.q,
      tri3d_round_i32(c.x), tri3d_round_i32(c.y), c.x, c.y, c.z, c.uq, c.vq, c.q);
  }
  tri3d_mark_depth_non_pristine();
  return true;
}

