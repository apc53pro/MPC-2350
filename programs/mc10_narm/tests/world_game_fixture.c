/* Real production game/profile + resident provider; only hardware, physics and
 * rendering are host services. No copied game-loop state machine. */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "narm.tch"
#define MC_TEST_WORLD_STREAM 1
#include "stream_fixture.c"
#define MC10_TILE_PROFILE 1
#define MC10_INTERACTIVE 1
#define MC10_WORLD_SYSTEMS 1
struct MCMenuButton{int id,x_position,y_position,width,height,enabled,draw_button;char *display_string;};
struct MCSettings{int key_codes[13];int occlusion_culling,fancy_graphics;};struct MCSettings mc_options;
struct FixturePlayer{int block_x,block_y,block_z;};struct FixturePlayer mc_runtime_player;
struct FixtureBlock{int material,opaque_cube,texture;};struct FixtureBlock mc_blocks[256];
#define MC_MATERIAL_WATER 8
int mc_player_status,mc_render_terrain_ready,mc_render_cache_ready;
int mc_render_cull_mode,mc_render_cull_diagnostics,mc_render_list_enabled,mc_render_list_count,mc_render_list_error;
int mc_block_mesh_ready=1,mc_block_mesh_error,mc_tint_error;
int mc_inventory_ready=1,mc_inventory_creative,mc_viewmodel_status,mc_hud_hardcore,mc_hud_underwater;
int held[256],keys[32],actions[32],event_count,event_read,hover=-1,script,script_stage,exit_requested;
int ticks,world_ticks,third_ticks,third_begin,view_ticks,ui_ticks,looks,swings,selected=-1,camera_mode,camera_cycles;
int micros_calls,stats_calls,stats_flags,flush_calls,options_calls,inventory_calls,begin_calls,presents,frame_parts,decoded_live,mode_at_begin;
int tick_fail_next,fancy_calls,last_fancy_sequence,rebuild_sequence,sequence,clock_init_calls,sky_init_calls,seed_calls;
uint clock_now;
extern int mc_game_frame_active,mc_game_paused,mc_game_inventory_pending,mc_game_camera_toggle_pending;
extern uint mc_game_accumulator,mc_game_last_time,mc_game_last_look;
void drive(void);
uint narm_millis(void){return clock_now;}
uint narm_micros(void){micros_calls++;return clock_now*1000;}
void renderStats(uint flags){stats_calls++;stats_flags=flags;}
uint renderStat(uint i){return i==76?0x44430001:0;}
int narm_key_down(uint key){return held[key];}
int narm_next_key_event(uint *key,uint *action,uint *mods,uint *character)
{if(event_read==event_count){event_read=event_count=0;return 0;}*key=keys[event_read];*action=actions[event_read++];*mods=*character=0;return 1;}
int narm_poll_system_event(void){drive();return exit_requested?NARM_EVENT_CLOSE_REQUESTED:0;}
int narm_yield(void){clock_now+=8;drive();return 0;}
int narm_sleep_ms(uint ms){clock_now+=ms;drive();return 0;}
int narm_half_present(void){return 0;}
void mc_render_fps_reset(void){}
void mc_render_list_flush(void){flush_calls++;mc_render_list_count=0;sequence++;}
void mc_options_draw_background(void){assert(!mc_render_list_count);}
void mc_menu_draw_centered_string(char *s,int x,int y,uint color){}
void mc_menu_draw_button(struct MCMenuButton *b){}
void mc_menu_draw_pointer(void){}
int mc_menu_button_hovered(struct MCMenuButton *b){return hover==b->id;}
int mc_menu_update_pointer_held(uint now){return 0;}
void mc_options_run(void){assert(mc_game_paused&&!mc_game_frame_active&&!mc_render_list_count);options_calls++;clock_now+=5000;mc_options.fancy_graphics=!mc_options.fancy_graphics;}
void mc_inventory_select(int slot){selected=slot;}
void mc_viewmodel_swing(void){swings++;}
int mc_inventory_run(void)
{assert(!mc_game_frame_active&&!mc_render_list_count&&mc_stream_ready());inventory_calls++;int before=world_ticks;clock_now+=4000;assert(world_ticks==before);return 1;}
void mc_viewmodel_begin_tick(void){assert(mc_stream_ready());}
void mc_viewmodel_tick(void){assert(mc_stream_ready());view_ticks++;}
void mc_player_ui_tick(void){assert(mc_stream_ready());ui_ticks++;}
int mc_player_ui_init(void){return 1;}
void mc_viewmodel_init(void){mc_viewmodel_status=0;}
void mc_third_init(void){camera_mode=0;}
void mc_third_cycle(void){camera_mode=(camera_mode+1)%3;camera_cycles++;}
void mc_third_begin_tick(void){assert(mc_stream_ready());third_begin++;}
void mc_third_tick(void){assert(mc_stream_ready());third_ticks++;}
void mc_world_clock_init(void){clock_init_calls++;world_ticks=0;}
void mc_world_clock_tick(void){assert(mc_stream_ready()&&!mc_game_paused);world_ticks++;}
void mc_biome_seed(struct MCU64 *seed){assert(seed->lo==0x12345678&&seed->hi==0xabcdef01);seed_calls++;}
int mc_sky_init(void){sky_init_calls++;return 1;}
void mc_blocks_set_graphics_level(int fancy)
{assert(!mc_render_list_count&&!mc_game_frame_active);fancy_calls++;last_fancy_sequence=++sequence;mc_blocks[18].opaque_cube=!fancy;mc_blocks[18].texture=fancy?52:53;}
void mc_player_init(void){mc_player_status=0;mc_runtime_player.block_x=0;mc_runtime_player.block_y=64;mc_runtime_player.block_z=0;}
int mc_player_tick(float strafe,float forward,int jump,int sneak)
{assert(mc_stream_ready());ticks++;if(tick_fail_next){tick_fail_next=0;mc_player_status=1;return 0;}if(forward>0)mc_runtime_player.block_x=16;return 1;}
void mc_player_look(float dx,float dy){assert(mc_stream_ready());looks++;}
int mc_math_init(void){return 1;}
int mc_render_prepare_assets(void){return 1;}
int mc_render_build_cache(void){assert(!"synchronous compiler must not run");return 0;}
int mc_region_floor_div(int x,int by){int q=x/by;return x<0&&x%by?q-1:q;}
int mc_runtime_try_get_block(int x,int y,int z,uint *id){*id=0;return 1;}
int mc_runtime_try_get_metadata(int x,int y,int z,uint *metadata){*metadata=0;return 1;}
void mc_fp_reset(void){}
int mc_fp_add(int a,int b){return a+b;}
int mc_fp_f32(float x){return 0;}
int mc_fp_floor(int x){return 0;}
int mc_fp_lt(int a,int b){return a<b;}
char mc_world_line[160];
void mc_world_append(char *out,int capacity,char *text){int n=strlen(out);snprintf(out+n,(size_t)(capacity-n),"%s",text);}
void mc_world_append_int(char *out,int capacity,int value){char text[32];snprintf(text,sizeof(text),"%d",value);mc_world_append(out,capacity,text);}
char *mc_runtime_error_text(int status){return "explicit source dependency";}
void mc_ui_draw_status(char *s){fprintf(stderr,"Unexpected status: %s\n",s);assert(!"startup failed");}
void mc_wait_for_return(void){}
void mc_render_begin_frame(void)
{assert(mc_stream_ready()&&!mc_game_frame_active&&!mc_render_list_count);assert(!mc_stream_dirty);if(fancy_calls)assert(last_fancy_sequence<rebuild_sequence);begin_calls++;frame_parts=0;mode_at_begin=camera_mode;}
int mc_render_step_frame(void)
{assert(mc_stream_ready()&&camera_mode==mode_at_begin);frame_parts++;mc_render_list_count=4;if(frame_parts<3)return 0;mc_render_list_flush();presents++;return 1;}
int fixture_safe_request(int x,int z){assert(!mc_game_frame_active&&!mc_render_list_count);return mc_stream_request(x,z);}
int fixture_safe_step(void)
{assert(!mc_game_frame_active&&!mc_render_list_count);int before=fixture_load_calls;int result=mc_stream_step();if(fixture_load_calls!=before&&mc_game_frame_active)decoded_live++;if(result==1)rebuild_sequence=++sequence;return result;}
#define mc_stream_request fixture_safe_request
#define mc_stream_step fixture_safe_step
#include "key_map.tc"
#include "profile_under_test.tc"
#include "game_under_test.tc"
#undef mc_stream_request
#undef mc_stream_step
void event(int key,int action){assert(event_count<32);keys[event_count]=key;actions[event_count++]=action;}
void pump_at(uint time){clock_now=time;mc_game_pump();}
void reset_ready(void)
{
 script=0;fixture_init(0,0);mc_options.fancy_graphics=0;mc_blocks[18].opaque_cube=1;mc_blocks[18].texture=53;mc_game_paused=mc_game_exit=mc_game_frame_active=mc_game_inventory_pending=mc_game_camera_toggle_pending=0;mc_profile_enabled=mc_profile_active=0;event_count=event_read=0;exit_requested=0;memset(held,0,sizeof(held));clock_now=0;mc_game_build_pause();mc_game_clear_input();
}
void drive(void)
{
 if(!script)return;
 if(script_stage==0&&mc_game_frame_active&&frame_parts==1)
 {event(0x3e,NARM_KEY_ACTION_PRESS);script_stage=1;}
 else if(script_stage==1&&presents>=1&&camera_mode==1)
 {assert(!micros_calls);event(NARM_KEY_E,NARM_KEY_ACTION_PRESS);script_stage=2;}
 else if(script_stage==2&&inventory_calls==1)
 {held[NARM_KEY_W]=1;script_stage=3;}
 else if(script_stage==3&&mc_runtime_player.block_x==16)
 {held[NARM_KEY_W]=0;script_stage=4;}
 else if(script_stage==4&&mc_stream_generation>=2&&mc_stream_ready()&&presents>=3)
 {assert(!decoded_live&&!micros_calls&&!stats_flags);exit_requested=1;script_stage=5;}
 assert(clock_now<60000);
}
int main(void)
{
 int before,old_micro,old_cycles,old_view,old_third,old_ui;
 mc_options.key_codes[0]=-100;mc_options.key_codes[2]=17;mc_options.key_codes[3]=30;mc_options.key_codes[4]=31;mc_options.key_codes[5]=32;mc_options.key_codes[6]=57;mc_options.key_codes[7]=42;mc_options.key_codes[9]=18;
 fixture_world.random_seed_lo=0x12345678;fixture_world.random_seed_hi=0xabcdef01;
 reset_ready();ticks=world_ticks=third_ticks=view_ticks=ui_ticks=0;micros_calls=0;
 /* 49 ms is not a tick, 50ms is one, one idle second is20ticks. Renderpump
  * cadence is irrelevant and lack of movement does not freeze worldtime. */
 for(int t=1;t<=1000;t++){pump_at((uint)t);if(t==49)assert(!world_ticks);if(t==50)assert(world_ticks==1);}
 assert(ticks==20&&world_ticks==20&&third_ticks==20&&view_ticks==20&&ui_ticks==20&&!looks&&!micros_calls);
 /* Required original Timer catch-up ceiling remains10 ticks. */
 before=world_ticks;pump_at(9000);assert(world_ticks-before==10&&mc_game_accumulator==0);
 /* Pause/stream/inventory gates do not accumulate frozen time. */
 before=world_ticks;mc_game_pause();pump_at(12000);assert(world_ticks==before);mc_game_resume();pump_at(12049);assert(world_ticks==before);pump_at(12050);assert(world_ticks==before+1);
 before=world_ticks;mc_game_inventory_pending=1;pump_at(20000);assert(world_ticks==before&&!mc_game_accumulator&&mc_game_last_time==clock_now);mc_game_inventory_pending=0;fixture_safe_request(1,0);pump_at(24000);assert(world_ticks==before&&!mc_game_accumulator);while(fixture_safe_step()==0){}mc_game_reset_clock();pump_at(24049);assert(world_ticks==before);pump_at(24050);assert(world_ticks==before+1);
 /* F5 onlypress: repeat/release do notcycle; whileframeactive nothing is
  * published, atboundary rear/front/first complete the source0/1/2cycle. */
 reset_ready();camera_mode=0;camera_cycles=0;mc_game_frame_active=1;event(0x3e,NARM_KEY_ACTION_PRESS);pump_at(1);assert(mc_game_camera_toggle_pending==1&&!camera_mode);mc_game_apply_camera_toggle();assert(!camera_mode);event(0x3e,2);event(0x3e,NARM_KEY_ACTION_RELEASE);pump_at(2);assert(mc_game_camera_toggle_pending==1);mc_game_frame_active=0;mc_game_apply_camera_toggle();assert(camera_mode==1&&camera_cycles==1);
 event(0x3e,NARM_KEY_ACTION_PRESS);pump_at(3);mc_game_apply_camera_toggle();assert(camera_mode==2);event(0x3e,NARM_KEY_ACTION_PRESS);pump_at(4);mc_game_apply_camera_toggle();assert(camera_mode==0);old_cycles=camera_cycles;mc_game_frame_active=1;event(0x3e,NARM_KEY_ACTION_PRESS);event(0x3e,NARM_KEY_ACTION_PRESS);event(0x3e,NARM_KEY_ACTION_PRESS);pump_at(5);mc_game_frame_active=0;mc_game_apply_camera_toggle();assert(camera_mode==0&&camera_cycles==old_cycles);
 mc_game_pause();event(0x3e,NARM_KEY_ACTION_PRESS);pump_at(6);mc_game_apply_camera_toggle();assert(camera_mode==0);
 /* F3optional instrumentation only changes atboundary and inactive paths
  * perform no optionalclockreads even thoughrequiredphysics/yieldrun. */
 reset_ready();micros_calls=0;mc_profile_begin();mc_profile_yield();pump_at(50);assert(!micros_calls&&!mc_profile_active);mc_game_frame_active=1;event(NARM_KEY_F3,NARM_KEY_ACTION_PRESS);pump_at(51);mc_game_apply_profile_toggle();assert(!mc_profile_enabled);mc_game_frame_active=0;mc_game_apply_profile_toggle();assert(mc_profile_enabled&&!stats_flags);mc_profile_begin();pump_at(101);assert(micros_calls>0);event(NARM_KEY_F3,NARM_KEY_ACTION_PRESS);pump_at(102);mc_game_apply_profile_toggle();old_micro=micros_calls;pump_at(200);mc_profile_yield();assert(!mc_profile_enabled&&!mc_profile_active&&micros_calls==old_micro&&!stats_flags);
 /* Fancy changes happen afterflush, invalidateaccess and dirty the cache.
  * A new cache/frame cannot observeoldleafopacity/texture undernewsettings. */
 reset_ready();fancy_calls=0;sequence=last_fancy_sequence=rebuild_sequence=0;mc_game_frame_active=1;mc_render_list_count=7;mc_game_pause();assert(!mc_game_frame_active&&!mc_render_list_count);hover=0;event(NARM_KEY_ENTER,NARM_KEY_ACTION_PRESS);pump_at(1);assert(options_calls==1&&mc_game_paused&&fancy_calls==1&&mc_blocks[18].opaque_cube==0&&mc_blocks[18].texture==52&&!mc_runtime_access_ok&&mc_stream_dirty&&mc_stream_status==MC_STREAM_LOADING);while(fixture_safe_step()==0){}assert(rebuild_sequence>last_fancy_sequence&&mc_stream_ready());before=fancy_calls;mc_game_apply_render_settings();assert(fancy_calls==before);mc_game_resume();mc_render_begin_frame();assert(mc_render_cull_mode==1);
 /* Failure is not player-animation success. Currentcontractadvancesone
  * independentworldtickbeforethe failed-neighbor attemptfreezeslaterpumps. */
 reset_ready();before=world_ticks;old_view=view_ticks;old_third=third_ticks;old_ui=ui_ticks;tick_fail_next=1;pump_at(50);assert(world_ticks==before+1&&mc_player_status==1&&view_ticks==old_view&&third_ticks==old_third&&ui_ticks==old_ui);pump_at(5000);assert(world_ticks==before+1&&!mc_game_accumulator);
 /* Full productionrun exercisesboundary wrappersaroundactualproviderIO;
  * noMcRegion/NBT decode orcache-rebuild step mayrunwhilegeometryislive. */
 reset_ready();mc_player_status=0;script=1;script_stage=0;clock_now=0;presents=begin_calls=inventory_calls=0;micros_calls=0;fancy_calls=0;mc_stream_generation=0;
 mc_game_run(&fixture_world);assert(script_stage==5&&inventory_calls==1&&fixture_load_calls==3&&clock_init_calls==1&&sky_init_calls==1&&seed_calls==1&&!decoded_live&&!micros_calls&&!stats_flags&&!mc_render_list_count);
 puts("PASS productionworld/game:20Hzidleclock,50ms/10tickceiling,pause/stream/inventoryfreeze,F5press/repeat/triple/boundary,F3zerooptionalreads,Fancyflush/invalidate/rebuildorder,failedtickcontract,productionrunproviderIOonlyatcompletedframes");return 0;
}

