/* Test-only NARM service adapter; real block/chunk/renderer modules unchanged. */
#define narm_millis fixture_millis
#define narm_micros fixture_micros
#define narm_yield fixture_yield
#define narm_half_clear fixture_clear
#define narm_half_present fixture_present
#define narm_half_line fixture_line
#define fillQuad3DTexZ fixture_quad
#define fillQuad3DTexZMask fixture_masked_quad
#include "narm.tch"
typedef unsigned int uint;
#include "../src/mc10_types.tc"
#include "../src/mc10_paths.tc"
#include "../src/mc10_u64.tc"
#include "../src/mc10_java_double.tc"
#ifdef MC10_WORLD_SYSTEMS
#include "../src/mc10_fp64.tc"
#include "../src/mc10_math.tc"
#endif
#include "../src/mc10_java_random.tc"
#include "../src/mc10_chunk.tc"
struct MCWorldInfo {uint world_time_lo;uint world_time_hi;int raining;int thundering;};
struct MCWorldRuntime {struct MCWorldInfo *info;float light_brightness[16];};
struct MCPlayerState {
 int present;struct MCJDouble pos_x;struct MCJDouble pos_y;struct MCJDouble pos_z;
 struct MCJDouble motion_x;struct MCJDouble motion_y;struct MCJDouble motion_z;uint yaw_bits;uint pitch_bits;
 int block_x;int block_y;int block_z;
};
struct MCSettings {float gamma_setting;int fancy_graphics;};
struct MCPhysicalPlayer {int ticks;};
struct MCResidentChunk {int state;struct MCChunk chunk;struct MCNibbleArray data;struct MCNibbleArray skylight;struct MCNibbleArray blocklight;};
#define MC_RUNTIME_CHUNKS 9
struct MCResidentChunk mc_runtime_chunks[9];
struct MCWorldInfo fixture_info;
struct MCWorldRuntime mc_runtime_world;
struct MCPlayerState mc_runtime_player;
struct MCSettings mc_options;
struct MCPhysicalPlayer mc_player;
int mc_runtime_center_x,mc_runtime_center_z,mc_runtime_access_ok=1;
char fixture_blocks[9*32768];char fixture_metadata[9*16384];char fixture_sky[9*16384];char fixture_light[9*16384];char fixture_height[9*256];
int fixture_block_reads,fixture_light_reads,fixture_clock_calls,fixture_yields;
uint fixture_clock_now,fixture_clock_increment;
uint fixture_micro_offset,fixture_draw_us,fixture_clear_us,fixture_present_us;
uint fixture_hud_us;int fixture_hud_calls;
int fixture_micros_calls;
char *fixture_footer;
uint fixture_millis(void){uint v=fixture_clock_now;fixture_clock_calls++;fixture_clock_now+=fixture_clock_increment;return v;}
uint fixture_micros(void){fixture_micros_calls++;return fixture_clock_now*1000+fixture_micro_offset;}
int fixture_yield(void){fixture_yields++;return 0;}
int fixture_clear(uint c){fixture_micro_offset+=fixture_clear_us;return 0;}
int fixture_present(void){fixture_micro_offset+=fixture_present_us;return 0;}
int fixture_line(int a,int b,int c,int d,uint color){fixture_hud_calls++;fixture_micro_offset+=fixture_hud_us;return 0;}
#ifndef MC_RENDER_ARM_TEST
#undef sin
#undef cos
#include <math.h>
float narm_sin(float v){return sinf(v);}float narm_cos(float v){return cosf(v);}
#endif
int fixture_find(int x,int z)
{int c;for(c=0;c<9;c=c+1)if(mc_runtime_chunks[c].state==1&&x>=mc_runtime_chunks[c].chunk.x_position*16&&x<mc_runtime_chunks[c].chunk.x_position*16+16&&z>=mc_runtime_chunks[c].chunk.z_position*16&&z<mc_runtime_chunks[c].chunk.z_position*16+16)return c;return -1;}
int mc_runtime_try_get_block(int x,int y,int z,uint *out)
{int c;fixture_block_reads++;*out=0;if(y<0||y>=128)return 1;c=fixture_find(x,z);if(c<0)return 0;*out=mc_chunk_get_block_id(&mc_runtime_chunks[c].chunk,x&15,y,z&15);return 1;}
int mc_runtime_try_get_metadata(int x,int y,int z,uint *out)
{int c;*out=0;if(y<0||y>=128)return 1;c=fixture_find(x,z);if(c<0)return 0;*out=mc_chunk_get_block_metadata(&mc_runtime_chunks[c].chunk,x&15,y,z&15);return 1;}
int mc_runtime_try_get_saved_light(int sky,int x,int y,int z,uint *out)
{int c;fixture_light_reads++;*out=sky?15:0;if(y<0)y=0;if(y>127)y=127;c=fixture_find(x,z);if(c<0){*out=0;return 0;}*out=mc_nibble_array_get(sky?&mc_runtime_chunks[c].skylight:&mc_runtime_chunks[c].blocklight,x&15,y,z&15);return 1;}
#include "../src/mc10_blocks.tc"
#include "../src/mc10_blocks_extra.tc"
uint mc_asset_pixels[16384];uint mc_asset_mask[2048];uint mc_asset_loaded_mask_bytes=8192;
int mc_asset_loaded_width=256,mc_asset_loaded_height=256;
int mc_asset_load_image(char *name){return 0;}
char mc_world_line[160];
void mc_world_append(char *s,int size,char *text){}void mc_world_append_int(char *s,int size,int value){}void mc_world_append_uint(char *s,int size,uint value){}
void mc_menu_draw_string(char *s,int x,int y,uint color)
{fixture_hud_calls++;fixture_micro_offset+=fixture_hud_us;if(x==4&&y==229)fixture_footer=s;}
void mc_wait_for_return(void){}void mc_game_run(struct MCWorldInfo *world){}
#include "../src/mc10_profile.tc"
#ifdef MC10_WORLD_SYSTEMS
#include "../src/mc10_cos_celestial.tc"
#include "../src/mc10_world_clock.tc"
#include "../src/mc10_block_render_extra.tc"
#include "../src/mc10_block_colors.tc"
#include "../src/mc10_biome_tint.tc"
#endif
#include "renderer_under_test.tc"
#ifndef MC_RENDER_ARM_TEST
#define FIXTURE_TRACE_CAP 65536
#else
#define FIXTURE_TRACE_CAP 1
#endif
uint fixture_trace[FIXTURE_TRACE_CAP*22];int fixture_trace_count,fixture_trace_error;
#ifdef MC10_WORLD_SYSTEMS
uint fixture_tint_trace[FIXTURE_TRACE_CAP];
#endif
uint fixture_hash=2166136261;
void fixture_capture(float *v,int masked)
{
 int i;uint bits;
 fixture_micro_offset+=fixture_draw_us;
#ifdef MC10_WORLD_SYSTEMS
 if(fixture_trace_count<FIXTURE_TRACE_CAP)fixture_tint_trace[fixture_trace_count]=mc_render_source_dynamic?mc_render_source_dynamic_key:(uint)mc_render_source_tint_slot;
#endif
 for(i=0;i<22;i=i+1)
 {
  bits=i<20?((uint *)v)[i]:(i==20?(uint)mc_render_atlas_variant:(uint)masked);
  fixture_hash=(fixture_hash^bits)*16777619;
  if(fixture_trace_count<FIXTURE_TRACE_CAP)fixture_trace[fixture_trace_count*22+i]=bits;
 }
 fixture_trace_count++;
 if(fixture_trace_count>FIXTURE_TRACE_CAP)fixture_trace_error=1;
}
void fixture_quad(float a,float b,float c,float d,float e,float f,float g,float h,float i,float j,float k,float l,float m,float n,float o,float p,float q,float r,float s,float t,const uint *tex,uint width,uint height,uint flags,const float *camera)
{float v[20]={a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t};fixture_capture(v,0);}
void fixture_masked_quad(float a,float b,float c,float d,float e,float f,float g,float h,float i,float j,float k,float l,float m,float n,float o,float p,float q,float r,float s,float t,const uint *tex,uint width,uint height,uint flags,const float *camera,const uint *mask)
{float v[20]={a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t};fixture_capture(v,1);}
void fixture_render_init(int center_x,int center_z)
{
 int c,i;float v;struct MCResidentChunk *r;
#ifndef MC_RENDER_BASELINE
 mc_render_cull_mode=0;mc_render_cull_diagnostics=0;
#endif
 mc_runtime_center_x=center_x;mc_runtime_center_z=center_z;mc_runtime_world.info=&fixture_info;mc_blocks_init();
 mc_render_texture_override=-1;mc_render_force_mask=0;
 for(i=0;i<16;i=i+1){v=1.0f-(float)i/15.0f;mc_runtime_world.light_brightness[i]=(1.0f-v)/(v*3.0f+1.0f);}
 for(c=0;c<9;c=c+1)
 {
  r=&mc_runtime_chunks[c];r->state=1;
  mc_nibble_array_bind(&r->data,&fixture_metadata[c*16384],7);mc_nibble_array_bind(&r->skylight,&fixture_sky[c*16384],7);mc_nibble_array_bind(&r->blocklight,&fixture_light[c*16384],7);
  mc_chunk_bind(&r->chunk,&fixture_blocks[c*32768],&fixture_height[c*256],&r->data,&r->skylight,&r->blocklight,center_x+(c%3)-1,center_z+(c/3)-1);
 }
}
void fixture_render_frame(void)
{
 fixture_trace_count=0;fixture_trace_error=0;fixture_hash=2166136261;fixture_block_reads=0;fixture_light_reads=0;fixture_clock_calls=0;fixture_hud_calls=0;
 fixture_footer=0;
 fixture_micros_calls=0;
 mc_render_begin_frame();while(!mc_render_step_frame()){}
}
