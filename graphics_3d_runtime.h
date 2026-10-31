#ifndef GRAPHICS_3D_RUNTIME_H
#define GRAPHICS_3D_RUNTIME_H

#include "globals.h"

// Shared 3D renderer lifecycle used by the NARM video ABI.
void tri3d_flush_solid_batch();
void tri3d_mark_depth_pristine(uint8_t frame_id);
void tri3d_mark_depth_non_pristine();
void tri3d_set_tile_scratch_enabled(bool enabled);
// Existing renderStats(0/1) retains its meaning. Diagnostic flag 0x100 forces
// eligible clamped spans through the original general sampler for exact A/B.
// read IDs: 9..12 general/clamped/16wrap/masked spans; 13..16 matching candidate
// pixels (before depth/mask rejection, NOT actual texel fetches); 17/18 clamped
// eligible spans/pixels; 19 protocol=0x53500001; 20 active flags.
// Native video submit additionally supplies 22/23 SDK-reported sys/peripheral
// Hz (configured frequencies, not an independent hardware frequency test).
// Clear resets counters but not flags. Graphics release disables diagnostics.
// Flag 0x200 enables the separate detailed tile kernel (use outside primary
// timings). 24=tile protocol 0x54420001; 25=jobs; 26=prepared triangles;
// 27=covered pixel candidates; 28/29/30=depth rejects/mask tests/writes, only
// with 0x200; 31=SRAM pixel workspace bytes. Native IDs32/33 hash color and
// canonical valid-depth buffers respectively. Hashing is an untimed diagnostic.
// Flag 0x400 enables stage-boundary microsecond timers. 34=stage protocol
// 0x44530001; 35=whole native list service; 36=validation; 37=setup excluding
// nested flushes; 38=complete tile flush; 39=core-0 join wait; 40..43=core-0
// tile import/raster+command fetch/commit/active wall time; 44..47=core-1
// equivalents; 48=list calls. Worker times overlap: do not add them to obtain
// elapsed frame time. Counters 49..59 are enabled ONLY with both 0x200/0x400:
// occupied strips, triangle-bin memberships, command-copy bytes, staged pixel
// slots, valid-depth imports, dirty-depth stores, dirty four-pixel words,
// bin-read bytes, bin-reset bytes, core-0 strips, core-1 strips. 60..64 reserved.
// These counters describe logical accesses, not PSRAM bus/cache transactions.
// Timers use unsigned 32-bit subtraction (wrap-safe for intervals < 2^32 us).
// The normal SRAM span and tile kernels have no stage timer/pixel hooks.
// Flag 0x800 retains the original textured geometry preparation for exact A/B.
// 65=geometry preparation protocol 0x47500001. Packed preparation shares the
// existing rasterizer; its small caches exist only inside one synchronous list.
// 66=preparation revision 0x50500001: triangle setup is explicitly inlined;
// optimized lists construct commands locally before publishing to PSRAM.
// Flag 0x800 also retains direct PSRAM command construction. Existing protocol
// IDs and native ABI layout remain unchanged.
// Optional conservative tiled occlusion: list mode bit 0x100 enables it with
// worker mode 1 or 2. No draw reordering, opacity assumptions or quality loss.
// 70=protocol 0x43550001. Detailed-only (flag 0x200) counters 71..75 are HiZ
// queries, rejected whole triangles, cells inspected, fully covered cells
// published, and numeric-bound fallbacks. Public reserved IDs60..64 stay zero.
// The summary is PSRAM-owned and invalidated on each actual depth clear.
void tri3d_set_stats_enabled(uint32_t flags);
void tri3d_stats_reset();
uint32_t tri3d_stats_read(uint32_t stat);
bool tri3d_stage_profile_enabled();
bool tri3d_geometry_optimized_enabled();
// Main-core native service accounting; worker-local statistics are merged
// only after the synchronous list worker join. No atomics in pixel loops.
void tri3d_stage_profile_add(uint32_t stat, uint32_t amount);

// Runtime-neutral entry points. The caller supplies the
// already-authorized shared color/depth buffers and program-owned assets.
// Vertices are transformed and clipped by the renderer; vertex_count is 3 or
// 4. A textured call may pass mask_words == nullptr when masking is disabled.
struct Graphics3dVertex
{
  float x;
  float y;
  float z;
  float u;
  float v;
  uint32_t color;
};

struct Graphics3dTransform
{
  float value[18];
};

void tri3d_draw_screen_gouraud(var *color,
                               var *depth,
                               var *depth_stamp,
                               uint8_t frame_id,
                               const Graphics3dVertex *vertices);
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
                          const var *mask_words);

// Native textured-list vertex layout: tightly packed x,y,z,u,v floats, in texel
// UV coordinates. The caller performs the same source-range/finite validation
// as the retained descriptor path. No pointer or cached classification survives
// end_textured_list; callers must keep source assets immutable until that end.
bool tri3d_draw_projected_textured_packed(var *color, var *depth,
                          var *depth_stamp, uint8_t frame_id,
                          const float *xyzuv, uint8_t vertex_count,
                          const Graphics3dTransform *transform,
                          const var *texture_words, int32_t texture_width,
                          int32_t texture_height, uint32_t flags,
                          const var *mask_words);

// Called continuously by RP2350 Core 1. It is non-blocking when idle.
void tri3d_core1_worker_poll();

// Explicit synchronous list scope; no source pointer survives end. Only
// textured draws with the same color/depth/stamp/frame are permitted inside.
// mode 0: immediate; 1: SRAM tiles; 2: two SRAM tile workers. Bit0x100 enables
// conservative occlusion only with modes1/2 (257/258); 256 is invalid.
bool tri3d_begin_textured_list(var *color, var *depth, var *stamp,
                              uint8_t frame_id, uint32_t mode);
bool tri3d_end_textured_list();

#endif
