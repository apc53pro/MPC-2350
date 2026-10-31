/* Real game/provider integration, with host-only services/physics/cache pumps.
 * No rendering/world-state transition is replaced by a fake game-loop copy. */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "narm.tch"
#include "stream_fixture.c"
#define MC10_TILE_PROFILE 1
#define MC10_INTERACTIVE 1
struct MCMenuButton {int id,x_position,y_position,width,height,enabled,draw_button;char *display_string;};
struct MCSettings {int key_codes[13];int occlusion_culling;};
struct MCSettings mc_options;
struct FixturePlayer {int block_x,block_y,block_z;};
struct FixturePlayer mc_runtime_player;
struct FixtureBlock {int material;};
struct FixtureBlock mc_blocks[256];
#define MC_MATERIAL_WATER 8
int mc_player_status,mc_render_terrain_ready,mc_render_cache_ready;
int mc_render_cull_mode,mc_render_cull_diagnostics,mc_render_list_enabled,mc_render_list_count,mc_render_list_error;
int mc_inventory_ready=1,mc_inventory_creative,mc_viewmodel_status,mc_hud_hardcore,mc_hud_underwater;
int held[256],next_key,next_action,hover=-1,fixture_exit_requested;
int fixture_tick_calls,fixture_look_calls,fixture_options_calls,fixture_inventory_calls,fixture_swing_calls,fixture_selected=-1;
int fixture_world_presents,fixture_begin_calls,fixture_render_pumps,fixture_frame_part,fixture_flush_calls;
int fixture_script_stage,fixture_error_waits,fixture_failed_load_calls,fixture_error_present_count,fixture_scenario;
int fixture_micros_calls,fixture_stats_calls,fixture_loop_calls;
uint fixture_stats_flags,clock_now;
extern int mc_game_frame_active,mc_game_paused,mc_game_inventory_pending;
extern uint mc_game_accumulator,mc_game_last_time;
void fixture_drive(void);
uint narm_millis(void){return clock_now;}
uint narm_micros(void){fixture_micros_calls++;return clock_now*1000;}
void renderStats(uint flags){fixture_stats_calls++;fixture_stats_flags=flags;}
uint renderStat(uint counter){return counter==76?0x44430001:0;}
int narm_key_down(uint key){return held[key];}
int narm_next_key_event(uint *key,uint *action,uint *mods,uint *character)
{if(!next_key)return 0;*key=next_key;*action=next_action;*mods=0;*character=0;next_key=0;return 1;}
int narm_poll_system_event(void)
{fixture_drive();assert(++fixture_loop_calls<10000);return fixture_exit_requested?NARM_EVENT_CLOSE_REQUESTED:0;}
int narm_yield(void){clock_now+=8;fixture_drive();return 0;}
int narm_sleep_ms(uint ms){clock_now+=ms;fixture_drive();return 0;}
int narm_half_present(void){return 0;}
void mc_render_fps_reset(void){}
void mc_render_list_flush(void){fixture_flush_calls++;mc_render_list_count=0;}
void mc_options_draw_background(void){assert(!mc_render_list_count);}
void mc_menu_draw_centered_string(char *s,int x,int y,uint color){}
void mc_menu_draw_button(struct MCMenuButton *b){}
void mc_menu_draw_pointer(void){}
int mc_menu_button_hovered(struct MCMenuButton *b){return hover==b->id;}
int mc_menu_update_pointer_held(uint now){return 0;}
void mc_options_run(void)
{assert(!mc_render_list_count);fixture_options_calls++;clock_now+=5000;mc_options.occlusion_culling=1;}
void mc_inventory_select(int slot){fixture_selected=slot;}
void mc_viewmodel_swing(void){fixture_swing_calls++;}
int mc_inventory_run(void)
{assert(!mc_game_frame_active&&!mc_render_list_count&&mc_stream_ready());fixture_inventory_calls++;clock_now+=2000;return 0;}
void mc_viewmodel_begin_tick(void){assert(mc_stream_ready());}
void mc_viewmodel_tick(void){assert(mc_stream_ready());}
void mc_player_ui_tick(void){assert(mc_stream_ready());}
int mc_player_ui_init(void){return 1;}
void mc_viewmodel_init(void){mc_viewmodel_status=0;}
void mc_player_init(void)
{mc_player_status=0;mc_runtime_player.block_x=0;mc_runtime_player.block_y=64;mc_runtime_player.block_z=0;}
int mc_player_tick(float strafe,float forward,int jump,int sneak)
{
 assert(mc_stream_ready());fixture_tick_calls++;
 if(forward>0)mc_runtime_player.block_x=16;
 return 1;
}
void mc_player_look(float dx,float dy){assert(mc_stream_ready());fixture_look_calls++;}
int mc_math_init(void){return 1;}
int mc_render_prepare_assets(void){return 1;}
int mc_render_build_cache(void){assert(!"synchronous cache compiler used in streaming gameplay");return 0;}
int mc_region_floor_div(int x,int by){int q=x/by;return x<0&&x%by?q-1:q;}
int mc_runtime_try_get_block(int x,int y,int z,uint *id){*id=0;return 1;}
int mc_runtime_try_get_metadata(int x,int y,int z,uint *metadata){*metadata=0;return 1;}
void mc_fp_reset(void){}
int mc_fp_add(int a,int b){return a+b;}
int mc_fp_f32(float value){return 0;}
int mc_fp_floor(int value){return 0;}
int mc_fp_lt(int a,int b){return a<b;}
int mc_render_rebuild_bad_id;
char mc_world_line[160];
void mc_world_append(char *out,int capacity,char *text)
{int used=(int)strlen(out);snprintf(out+used,(size_t)(capacity-used),"%s",text);}
void mc_world_append_int(char *out,int capacity,int value)
{char text[32];snprintf(text,sizeof(text),"%d",value);mc_world_append(out,capacity,text);}
char *mc_runtime_error_text(int status){return "explicit stream failure";}
void mc_ui_draw_status(char *text){assert(!"unexpected startup/error status screen");}
void mc_wait_for_return(void){}
void mc_render_begin_frame(void)
{
 assert(mc_stream_ready()&&!mc_game_frame_active&&!mc_render_list_count);
 fixture_begin_calls++;fixture_frame_part=0;
}
int mc_render_step_frame(void)
{
 assert(mc_stream_ready());fixture_render_pumps++;fixture_frame_part++;
 mc_render_list_count=4;
 if(fixture_frame_part<3)return 0;
 mc_render_list_flush();fixture_world_presents++;return 1;
}
int fixture_game_request(int x,int z)
{assert(!mc_game_frame_active&&!mc_render_list_count);return mc_stream_request(x,z);}
int fixture_game_step(void)
{assert(!mc_game_frame_active&&!mc_render_list_count);return mc_stream_step();}
#define mc_stream_request fixture_game_request
#define mc_stream_step fixture_game_step
#include "key_map.tc"
#include "profile_under_test.tc"
#include "game_under_test.tc"
#undef mc_stream_request
#undef mc_stream_step

void fixture_event(int key){assert(!next_key);next_key=key;next_action=NARM_KEY_ACTION_PRESS;}
void fixture_drive(void)
{
 if(fixture_scenario==3)
 {
  if(fixture_script_stage==0&&mc_game_paused&&mc_stream_status<0)
  {
   assert(mc_stream_status==-48&&!mc_stream_ready()&&!mc_runtime_access_ok);
   assert(mc_render_rebuild_bad_id==200&&!fixture_world_presents&&!fixture_load_calls);
   fixture_block_error=0;hover=4;fixture_event(NARM_KEY_ENTER);fixture_script_stage=1;
  }
  else if(fixture_script_stage==1&&fixture_world_presents>0)
  {assert(mc_stream_ready()&&!mc_render_rebuild_bad_id);fixture_exit_requested=1;fixture_script_stage=2;}
  return;
 }
 if(fixture_scenario==2)
 {
  if(fixture_script_stage==0&&fixture_world_presents>0)
  {mc_player_status=1;fixture_script_stage=1;}
  else if(fixture_script_stage==1&&mc_game_paused)
  {assert(mc_player_status==1&&mc_stream_ready());fixture_exit_requested=1;fixture_script_stage=2;}
  return;
 }
 if(fixture_script_stage==0&&fixture_world_presents>0&&mc_stream_ready())
 {held[NARM_KEY_W]=1;fixture_script_stage=1;}
 else if(fixture_script_stage==1&&mc_runtime_player.block_x==16)
 {
  held[NARM_KEY_W]=0;fixture_fail_enabled=1;fixture_fail_x=2;fixture_fail_z=-1;fixture_fail_status=-27;
  /* Model a rolled-back next tick waiting for its already-crossed chunk's
   * neighbor. Completion must clear status1, not pause forever after loading. */
  mc_player_status=1;fixture_script_stage=2;
 }
 else if(fixture_script_stage==2&&mc_game_paused&&mc_stream_status<0)
 {
  fixture_failed_load_calls=fixture_load_calls;fixture_error_present_count=fixture_world_presents;
  fixture_script_stage=3;fixture_error_waits=0;
 }
 else if(fixture_script_stage==3&&mc_game_paused)
 {
  assert(fixture_load_calls==fixture_failed_load_calls);
  assert(fixture_world_presents==fixture_error_present_count);
  if(++fixture_error_waits>=4){hover=0;fixture_event(NARM_KEY_ENTER);fixture_script_stage=4;}
 }
 else if(fixture_script_stage==4&&fixture_options_calls==1&&mc_game_paused)
 {
  assert(fixture_load_calls==fixture_failed_load_calls&&!mc_stream_ready());
  assert(fixture_world_presents==fixture_error_present_count);
  fixture_fail_enabled=0;hover=4;fixture_event(NARM_KEY_ENTER);fixture_script_stage=5;
 }
 else if(fixture_script_stage==5&&mc_stream_generation>=2&&mc_stream_ready()&&fixture_world_presents>fixture_error_present_count)
 {assert(mc_player_status==0);fixture_event(NARM_KEY_E);fixture_script_stage=6;}
 else if(fixture_script_stage==6&&fixture_inventory_calls==1)
 {fixture_event(0x20);fixture_script_stage=7;}
 else if(fixture_script_stage==7&&fixture_selected==2)
 {fixture_event(NARM_KEY_ENTER);fixture_script_stage=8;}
 else if(fixture_script_stage==8&&fixture_swing_calls==1)
 {assert(!mc_profile_enabled&&!fixture_micros_calls);fixture_event(NARM_KEY_F3);fixture_script_stage=9;}
 else if(fixture_script_stage==9&&mc_profile_enabled)
 {fixture_event(NARM_KEY_F3);fixture_script_stage=10;}
 else if(fixture_script_stage==10&&!mc_profile_enabled)
 {assert(fixture_micros_calls>0);fixture_exit_requested=1;fixture_script_stage=11;}
}
int main(void)
{
 int ticks,looks;
 mc_options.key_codes[0]=-100;mc_options.key_codes[2]=17;mc_options.key_codes[3]=30;
 mc_options.key_codes[4]=31;mc_options.key_codes[5]=32;mc_options.key_codes[6]=57;
 mc_options.key_codes[7]=42;mc_options.key_codes[9]=18;
 /* Pending streaming cannot advance physics, viewpoint or timer catch-up. */
 fixture_init(0,0);mc_game_build_pause();mc_game_clear_input();
 mc_stream_request(1,0);held[NARM_KEY_W]=1;held[NARM_KEY_RIGHT]=1;clock_now=10000;
 ticks=fixture_tick_calls;looks=fixture_look_calls;mc_game_pump();
 assert(fixture_tick_calls==ticks&&fixture_look_calls==looks&&mc_game_accumulator==0);
 assert(mc_game_last_time==clock_now);held[NARM_KEY_W]=held[NARM_KEY_RIGHT]=0;
 /* Full production game run: init bounded cache -> partial native frames ->
  * crossing -> one successful slot -> decoder failure -> idle error pause ->
  * options -> explicit retry -> gameplay -> E/inventory, hotbar, Enter, F3. */
 fixture_init(0,0);fixture_scenario=1;fixture_script_stage=0;clock_now=0;
 mc_game_run(&fixture_world);
 assert(fixture_script_stage==11&&fixture_options_calls==1&&fixture_inventory_calls==1);
 assert(fixture_selected==2&&fixture_swing_calls==1&&fixture_tick_calls>ticks);
 assert(mc_runtime_center_x==1&&mc_runtime_center_z==0&&mc_runtime_loaded==9);
 assert(fixture_load_calls==4); /* three successes plus the failed retry. */
 assert(mc_stream_status==MC_STREAM_ERR_CANCELLED&&!mc_runtime_access_ok);
 assert(!mc_render_list_count&&!mc_profile_active&&!mc_profile_enabled);
 /* A ready provider with a distinct unresolved player error must NOT pretend
  * streaming fixed it; pause rather than clearing status unconditionally. */
 fixture_init(0,0);fixture_scenario=2;fixture_script_stage=0;fixture_exit_requested=0;
 fixture_world_presents=0;fixture_begin_calls=0;next_key=0;memset(held,0,sizeof(held));
 mc_game_run(&fixture_world);assert(fixture_script_stage==2&&mc_player_status==1);
 assert(fixture_load_calls==0);
 /* A block/shape unsupported by the current source port cannot become an
  * invisible/unsafe first frame, even though all nine chunks decoded fine. */
 fixture_init(0,0);fixture_scenario=3;fixture_script_stage=0;fixture_exit_requested=0;
 fixture_world_presents=0;fixture_begin_calls=0;next_key=0;fixture_block_error=1;
 mc_game_run(&fixture_world);assert(fixture_script_stage==2&&!fixture_load_calls);
 puts("Stream/game integration: PASS frame/native-boundary gates, frozen physics/camera/clocks, decoder/block errors before access/first frame, no busy retry, pause/options/Retry, status recovery, inventory/hotbar/Enter/F3, no synchronous cache compiler.");
 return 0;
}
