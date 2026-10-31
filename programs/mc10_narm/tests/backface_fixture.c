#include "textured_list_fixture.c"
void fixture_cull_neighbors(int id)
{
 fixture_blocks[4*32768+(4<<7)+64]=(char)id;
 fixture_blocks[4*32768+(1<<11)+(4<<7)+64]=(char)id;
}
void fixture_cull_scene(int id,int metadata,int extra)
{
 int result;
 fixture_trace_count=0;fixture_trace_error=0;fixture_hash=2166136261;
 fixture_micros_calls=0;fixture_hud_calls=0;fixture_footer=0;
 mc_render_cached_block=0;mc_render_cached_standard=0;
 mc_render_begin_frame();
 mc_render_block((uint)id,(uint)metadata,0,64,4);
 if(extra)mc_render_block(1,0,1,64,4);
 mc_render_list_flush();
}
