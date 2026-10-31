/* Test-only NARM services and UI stubs; production game loop is unchanged. */
#include <assert.h>
#include <string.h>
#include "narm.tch"
typedef unsigned int uint;
struct MCMenuButton {int id,x_position,y_position,width,height,enabled,draw_button;char *display_string;};
struct MCWorldInfo {int unused;};
struct MCSettings {int key_codes[13];int occlusion_culling;};
struct MCSettings mc_options;
int mc_player_status,mc_render_terrain_ready,mc_render_cache_ready,fixture_translations;
uint clock_now;int held[256],next_key,next_action,system_event,hover,options_calls,tick_calls,look_calls,tick_ok=1;
int fixture_micros_calls,fixture_stats_calls;uint fixture_stats_flags;
int mc_render_cull_mode,mc_render_cull_diagnostics;
int mc_render_list_count,mc_render_list_enabled,mc_render_list_error;
int fixture_flush_calls,fixture_flush_mode,fixture_flush_pending;
void mc_render_list_flush(void)
{fixture_flush_calls++;fixture_flush_mode=mc_render_cull_mode;fixture_flush_pending=mc_render_list_count;mc_render_list_count=0;}
float last_forward,last_strafe,last_dx,last_dy;int last_jump,last_sneak;
uint narm_millis(void){return clock_now;}
uint narm_micros(void){fixture_micros_calls++;return clock_now*1000;}
void renderStats(uint flags){fixture_stats_calls++;fixture_stats_flags=flags;}
void mc_render_fps_reset(void){}
int narm_key_down(uint key){return held[key];}
int narm_next_key_event(uint *key,uint *action,uint *mods,uint *character)
{if(!next_key)return 0;*key=next_key;*action=next_action;*mods=0;*character=0;next_key=0;return 1;}
int narm_poll_system_event(void){int e=system_event;system_event=0;return e;}
int narm_half_present(void){return 0;}
int narm_sleep_ms(uint ms){clock_now+=ms;return 0;}
int narm_yield(void){return 0;}
void mc_options_draw_background(void){assert(!mc_render_list_count);}
void mc_menu_draw_centered_string(char *s,int x,int y,uint c){}
void mc_menu_draw_button(struct MCMenuButton *b){}
void mc_menu_draw_pointer(void){}
int mc_menu_button_hovered(struct MCMenuButton *b){return hover==b->id;}
int mc_menu_update_pointer_held(uint now){return 0;}
void mc_options_run(void){options_calls++;clock_now+=5000;mc_options.occlusion_culling=1;}
int mc_player_tick(float s,float f,int j,int sn){tick_calls++;last_strafe=s;last_forward=f;last_jump=j;last_sneak=sn;return tick_ok;}
void mc_player_look(float dx,float dy){look_calls++;last_dx=dx;last_dy=dy;}
void mc_player_init(void){}
int mc_math_init(void){return 1;}
int mc_render_prepare_assets(void){return 1;}
int mc_render_build_cache(void){return 1;}
void mc_render_begin_frame(void){}
int mc_render_step_frame(void){return 1;}
void mc_ui_draw_status(char *s){}
void mc_wait_for_return(void){}
#include "key_map.tc"
#include "../src/mc10_profile.tc"
#include "../src/mc10_game.tc"
void event(int key){next_key=key;next_action=NARM_KEY_ACTION_PRESS;}
int main(void)
{
 int calls,looks,translations;
 /* No setting change may reinterpret already prepared batch commands. */
 mc_render_cull_mode=3;mc_render_cull_diagnostics=1;mc_render_list_count=7;
 mc_options.occlusion_culling=0;mc_game_apply_render_settings();
#ifdef MC10_TILE_PROFILE
 assert(fixture_flush_pending==7&&fixture_flush_mode==3&&!mc_render_list_count);
#else
 mc_render_list_count=0;
#endif
 assert(mc_render_cull_mode==1&&!mc_render_cull_diagnostics);
 mc_options.key_codes[2]=17;mc_options.key_codes[3]=30;mc_options.key_codes[4]=31;mc_options.key_codes[5]=32;
 mc_options.key_codes[6]=57;mc_options.key_codes[7]=42;
 mc_game_build_pause();mc_game_clear_input();
 held[NARM_KEY_W]=1;held[NARM_KEY_A]=1;held[NARM_KEY_SPACE]=1;held[NARM_KEY_LEFT_SHIFT]=1;
 clock_now=49;mc_game_pump();assert(tick_calls==0);
 clock_now=50;mc_game_pump();assert(tick_calls==1&&last_forward==1&&last_strafe==1&&last_jump==1&&last_sneak==1);
 translations=fixture_translations;
 held[NARM_KEY_SPACE]=0;held[0x39]=1;clock_now=100;mc_game_pump();assert(last_jump==0&&fixture_translations==translations);
 held[NARM_KEY_RIGHT]=1;clock_now=108;mc_game_pump();assert(last_dx==4&&last_dy==0);
 mc_profile_begin();
#ifdef MC10_TILE_PROFILE
 mc_render_list_count=4;
#endif
 event(NARM_KEY_ESCAPE);mc_game_pump();assert(mc_game_paused&&!mc_profile_active&&!mc_render_list_count);
 calls=tick_calls;clock_now=10108;mc_game_pump();assert(tick_calls==calls);
 hover=0;event(NARM_KEY_ENTER);mc_game_pump();assert(options_calls==1&&mc_game_paused&&tick_calls==calls);
 assert(mc_render_cull_mode==3&&!mc_render_cull_diagnostics);
 mc_options.occlusion_culling=0;
 event(NARM_KEY_ESCAPE);mc_game_pump();assert(!mc_game_paused&&tick_calls==calls);
 assert(mc_render_cull_mode==1&&!mc_render_cull_diagnostics);
 looks=look_calls;clock_now+=50;mc_game_pump();assert(last_forward==0&&last_strafe==0&&last_sneak==0&&look_calls==looks);
 held[NARM_KEY_W]=0;clock_now+=50;mc_game_pump();held[NARM_KEY_W]=1;clock_now+=50;mc_game_pump();assert(last_forward==1);
 /* Rebound forward is live, not a hard-coded WASD check. */
 mc_options.key_codes[2]=18;held[NARM_KEY_W]=0;held[NARM_KEY_E]=1;clock_now+=50;mc_game_pump();assert(last_forward==1);
 calls=tick_calls;clock_now+=2000;mc_game_pump();assert(tick_calls-calls==10);
 system_event=NARM_EVENT_FOCUS_LOST;mc_game_pump();assert(mc_game_paused);
 event(NARM_KEY_ESCAPE);mc_game_pump();tick_ok=0;clock_now+=50;mc_game_pump();assert(mc_game_paused);
 tick_ok=1;event(NARM_KEY_ESCAPE);mc_game_pump();system_event=NARM_EVENT_CLOSE_REQUESTED;mc_game_pump();assert(mc_game_exit);
 mc_game_exit=0;mc_game_pause();hover=1;event(NARM_KEY_ENTER);mc_game_pump();assert(mc_game_exit);
 /* F3 ignores repeat and applies only at a completed frame boundary. */
 mc_game_exit=0;mc_game_paused=0;mc_game_frame_active=1;
 event(NARM_KEY_F3);mc_game_pump();mc_game_apply_profile_toggle();
 assert(mc_game_profile_toggle_pending&&mc_profile_enabled);
 next_key=NARM_KEY_F3;next_action=NARM_KEY_ACTION_REPEAT;mc_game_pump();
 assert(mc_game_profile_toggle_pending);
 mc_game_frame_active=0;mc_game_apply_profile_toggle();
 assert(!mc_profile_enabled&&!mc_profile_active&&!mc_profile_valid&&!mc_game_profile_toggle_pending);
 assert(fixture_stats_calls==1&&!fixture_stats_flags);
 calls=fixture_micros_calls;translations=tick_calls;
 mc_profile_begin();clock_now+=50;mc_game_pump();mc_profile_yield();
 assert(fixture_micros_calls==calls&&tick_calls>translations&&!mc_profile_active);
 event(NARM_KEY_F3);mc_game_pump();mc_game_apply_profile_toggle();
 assert(mc_profile_enabled&&!mc_profile_active&&!mc_profile_valid&&fixture_stats_calls==2);
 /* Interactive launches start at FPS-only; profiler builds retain their HUD.
  * A queued close event exits immediately without inventing a world frame. */
 next_key=0;system_event=NARM_EVENT_CLOSE_REQUESTED;mc_options.occlusion_culling=1;
 mc_render_cull_mode=0;mc_render_cull_diagnostics=1;mc_game_run(0);
 assert(mc_render_cull_mode==3&&!mc_render_cull_diagnostics);
#ifdef MC10_INTERACTIVE
 assert(!mc_profile_enabled&&!mc_profile_active);
#else
 assert(mc_profile_enabled&&!mc_profile_active);
#endif
 return 0;
}
