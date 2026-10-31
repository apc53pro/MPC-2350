#include <assert.h>
#include <stdint.h>
#define MC10_TILE_PROFILE 1
#define fixture_render_frame fixture_base_render_frame
#include "render_fixture.c"
#undef fixture_render_frame
void fixture_render_frame(void)
{
 int result;
 fixture_trace_count=0;fixture_trace_error=0;fixture_hash=2166136261;
 fixture_block_reads=0;fixture_light_reads=0;fixture_clock_calls=0;fixture_hud_calls=0;
 fixture_micros_calls=0;fixture_footer=0;
 assert(!mc_render_list_count);mc_render_begin_frame();
 do {result=mc_render_step_frame();assert(!mc_render_list_count);} while(!result);
}
int fixture_list_calls,fixture_list_max,fixture_list_cached,fixture_list_copied;
int fixture_list_fail=-1;
/* Production commands intentionally contain target-width 32-bit pointers.
 * Resolve only known fixture ranges when running in a 64-bit host process. */
static float *fixture_vertex_pointer(uint word)
{
 uint offset=word-(uint)(uintptr_t)&mc_render_geometry[0];
 if(offset<sizeof(mc_render_geometry))
 {assert(offset%sizeof(struct MCCachedQuad)==0);fixture_list_cached++;return (float *)((char *)&mc_render_geometry[0]+offset);}
 offset=word-(uint)(uintptr_t)&mc_render_list_vertices[0];
 assert(offset<sizeof(mc_render_list_vertices)&&offset%(20*sizeof(float))==0);
 fixture_list_copied++;return (float *)((char *)&mc_render_list_vertices[0]+offset);
}
int drawTexturedList(const unsigned *commands,unsigned count,const float *transform,unsigned mode)
{
 uint i,offset;int saved=mc_render_atlas_variant;
 assert(count>0&&count<=256&&transform==mc_render_camera&&(mode==2||mode==258));
 fixture_list_calls++;if((int)count>fixture_list_max)fixture_list_max=(int)count;
 if(fixture_list_fail>=0&&fixture_list_calls>fixture_list_fail)return -4;
 for(i=0;i<count;i++)
 {
  const uint *c=&commands[i*7];float *vertices=fixture_vertex_pointer(c[0]);
  assert(c[3]==256&&c[4]==256&&c[5]==0&&c[6]==4);
  assert(!c[2]||c[2]==(uint)(uintptr_t)&mc_asset_mask[0]);
  offset=c[1]-(uint)(uintptr_t)&mc_render_light_atlas[0];
  assert(offset<sizeof(mc_render_light_atlas)&&offset%65536==0);
  mc_render_atlas_variant=(int)(offset/65536);fixture_capture(vertices,c[2]!=0);
 }
 mc_render_atlas_variant=saved;return 0;
}
