/* Host-only services: exercise the unchanged benchmark orchestrator/logger. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "narm.tch"
typedef unsigned int uint;
#define MC10_AUTOPROFILE 1
#define MC_WORLD_FOLDER_CAP 64
#define MC_RUNTIME_OK 0
#define MC_CHUNK_BLOCK_COUNT 32768
#define MC_CHUNK_NIBBLE_BYTES 16384
#define MC_COLOR_BLACK 0
struct MCJDouble {uint lo,hi;};
struct MCWorldInfo {
 char folder[64],level_name[80];uint random_seed_hi,random_seed_lo,world_time_hi,world_time_lo,last_played_hi,last_played_lo;
};
struct MCPlayerState {uint yaw_bits,pitch_bits;int present;struct MCJDouble pos_x,pos_y,pos_z;};
struct MCSettings {int fancy_graphics,render_distance,ambient_occlusion;float gamma_setting,fov_setting;};
struct MCResidentChunk {struct {int x_position,z_position;} chunk;uint timestamp;};
struct MCResidentChunk mc_runtime_chunks[9];
char mc_runtime_blocks[9*MC_CHUNK_BLOCK_COUNT];
char mc_runtime_metadata[9*MC_CHUNK_NIBBLE_BYTES],mc_runtime_skylight[9*MC_CHUNK_NIBBLE_BYTES],mc_runtime_blocklight[9*MC_CHUNK_NIBBLE_BYTES];
struct MCWorldInfo mc_world_list[1];int mc_world_count=1;
struct MCPlayerState mc_runtime_player;
struct MCSettings mc_options;
int mc_runtime_loaded=9,mc_runtime_missing,mc_runtime_invalid,mc_runtime_access_ok=1,mc_runtime_status,mc_player_status;
uint mc_asset_source_crc32=123,mc_render_cache_build_ms=400;
int mc_render_cache_group_count=7642,mc_render_geometry_count=12288,mc_render_geometry_blocks=6287;
int mc_render_last_visited,mc_render_last_skipped,mc_render_show_hud=1,mc_render_benchmark_running;
int mc_render_cull_mode,mc_render_cull_diagnostics,mc_render_cull_backfaces,mc_render_cull_eligible;
uint mc_menu_glyph_fallbacks;
#ifdef MC10_CULL_PROFILE
#define MC_FIXTURE_FRAME_CAP 96
#else
#define MC_FIXTURE_FRAME_CAP 64
#endif
uint fixture_us,frame_yaw[MC_FIXTURE_FRAME_CAP],frame_pitch[MC_FIXTURE_FRAME_CAP],frame_hud[MC_FIXTURE_FRAME_CAP];
#if defined(MC10_SAMPLER_PROFILE) || defined(MC10_TILE_PROFILE)
uint fixture_flags,frame_flags[MC_FIXTURE_FRAME_CAP];int fixture_old_firmware,fixture_bad_mode;
#ifdef MC10_STAGE_PROFILE
int fixture_old_stage_firmware;
#endif
#ifdef MC10_GEOMETRY_PROFILE
int fixture_old_geometry_firmware,mc_render_submit_optimized=1;
#ifdef MC10_PREPARATION_PROFILE
int fixture_old_preparation_firmware;
#endif
uint frame_submit[MC_FIXTURE_FRAME_CAP];
#ifdef MC10_CULL_PROFILE
int fixture_old_culling_firmware;
uint frame_cull[MC_FIXTURE_FRAME_CAP],frame_cull_diagnostics[MC_FIXTURE_FRAME_CAP];
#endif
#endif
#ifdef MC10_TILE_PROFILE
extern int mc_profile_active;
int mc_render_list_enabled=1,mc_render_list_error,mc_render_list_count;
int fixture_service_failure=-1;uint frame_mode[MC_FIXTURE_FRAME_CAP];
void mc_render_list_flush(void){assert(!mc_render_list_count);}
#endif
void renderStats(uint flags){fixture_flags=flags;}
uint renderStat(uint index)
{
 if(index==19)return fixture_old_firmware?0:0x53500001;
 if(index==24)return fixture_old_firmware?0:0x54420001;
#ifdef MC10_STAGE_PROFILE
 if(index==34)return (fixture_old_firmware||fixture_old_stage_firmware)?0:0x44530001;
#endif
#ifdef MC10_GEOMETRY_PROFILE
 if(index==65)return fixture_old_geometry_firmware?0:0x47500001;
#ifdef MC10_CULL_PROFILE
 if(index==70)return fixture_old_culling_firmware==2?0x4d530001:(fixture_old_culling_firmware?0:0x43550001);
 if(index>=71&&index<=75)
 {
  assert(!mc_profile_active);
  if(!(fixture_flags&512)||!(mc_render_cull_mode&2))return 0;
  if(index==71)return 40;if(index==72)return 10;if(index==73)return 80;
  if(index==74)return 20;return 1;
 }
#endif
#ifdef MC10_PREPARATION_PROFILE
 if(index==66)return fixture_old_preparation_firmware?0:0x50500001;
#endif
#endif
 if(index==20)return fixture_bad_mode?0:fixture_flags;
 if(index==22)return 408000000;if(index==23)return 102000000;
 if(index==9)return fixture_flags==257?100:0;
 if(index==10)return (fixture_flags&1)&&!(fixture_flags&256)?
#ifdef MC10_CULL_PROFILE
    (mc_render_cull_mode==3?80:(mc_render_cull_mode?90:100)):0;
#else
    100:0;
#endif
 if(index==11)return 2;if(index==12)return 4;
 if(index==13)return fixture_flags==257?1000:0;
 if(index==14)return (fixture_flags&1)&&!(fixture_flags&256)?
#ifdef MC10_CULL_PROFILE
    (mc_render_cull_mode==3?800:(mc_render_cull_mode?900:1000)):0;
#else
    1000:0;
#endif
 if(index==15)return 20;if(index==16)return 40;
 if(index==17)return 100;if(index==18)return 1000;
#ifdef MC10_TILE_PROFILE
 if(index>=25&&index<=33)
 {
  assert(!mc_profile_active);
  if(index==31)return 15360;
  if(index==32)return 123456+(mc_render_show_hud?fixture_us:0);
  if(index==33)return mc_runtime_player.pitch_bits==0xc2b40000?345678:234567;
  if(!mc_render_list_enabled)return 0;
  if(index==25)
  {
#ifdef MC10_GEOMETRY_PROFILE
#ifdef MC10_CULL_PROFILE
   return mc_render_cull_mode==3?3:(mc_render_cull_mode?4:5);
#else
   return mc_render_submit_optimized?4:5;
#endif
#else
   return 5;
#endif
  }
  if(index==26)return
#ifdef MC10_CULL_PROFILE
    mc_render_cull_mode==3?80:(mc_render_cull_mode?90:100);
#else
    100;
#endif
  if(index==27)return
#ifdef MC10_CULL_PROFILE
    mc_render_cull_mode==3?860:(mc_render_cull_mode?960:1060);
#else
    1060;
#endif
  if(!(fixture_flags&512))return 0;
  if(index==28)return 200;
  if(index==29)return 40;if(index==30)return
#ifdef MC10_CULL_PROFILE
    mc_render_cull_mode==3?600:(mc_render_cull_mode?700:800);
#else
    800;
#endif
 }
#ifdef MC10_STAGE_PROFILE
 if(index>=35&&index<=59)
 {
  assert(!mc_profile_active);
  if(!(fixture_flags&1024))return 0;
#ifdef MC10_GEOMETRY_PROFILE
#ifdef MC10_CULL_PROFILE
  if(mc_render_cull_mode==1)
  {
   if(index==35)return 950;if(index==36)return 75;if(index==37)return 225;
   if(index==38)return 550;if(index==39)return 25;
   if(index==40||index==44)return 100;
   if(index==41||index==45)return 260;
   if(index==42||index==46)return 80;
   if(index==43||index==47)return 460;
   if(index==48)return 2;
   if(!(fixture_flags&512))return 0;
   if(index==49)return 10;if(index==50)return 30;if(index==51)return 30*176;
   if(index==52)return 1280;if(index==53)return 300;
   if(index==54)return 200;if(index==55)return 100;
   if(index==56)return 4*60*72;if(index==57)return 6*60*72;
   if(index==58)return 4;if(index==59)return 6;
  }
  if(mc_render_cull_mode)
#else
  if(mc_render_submit_optimized)
#endif
  {
   if(index==35)return 800;if(index==36)return 50;if(index==37)return 150;
   if(index==38)return 500;if(index==39)return 25;
   if(index==40||index==44)return 100;
   if(index==41||index==45)return 250;
   if(index==42||index==46)return 75;
   if(index==43||index==47)return 450;
   if(index==48)return 2;
   if(!(fixture_flags&512))return 0;
   if(index==49)return 10;if(index==50)return 30;if(index==51)return 30*176;
   if(index==52)return 1280;if(index==53)return 300;
   if(index==54)return 200;if(index==55)return 100;
#ifdef MC10_CULL_PROFILE
   if(index==56)return 3*60*72;if(index==57)return 5*60*72;
#else
   if(index==56)return 4*60*72;if(index==57)return 6*60*72;
#endif
   if(index==58)return 4;if(index==59)return 6;
  }
#endif
  if(index==35)return 1200;if(index==36)return 100;if(index==37)return 300;
  if(index==38)return 700;if(index==39)return 50;
  if(index==40||index==44)return 100;
  if(index==41||index==45)return 300;
  if(index==42||index==46)return 100;
  if(index==43||index==47)return 550;
  if(index==48)return 2;
  if(!(fixture_flags&512))return 0;
  if(index==49)return 10;if(index==50)return 30;if(index==51)return 30*176;
  if(index==52)return 1280;if(index==53)return 300;
  if(index==54)return 200;if(index==55)return 100;
  if(index==56)return 5*60*72;if(index==57)return 7*60*72;
  if(index==58)return 4;if(index==59)return 6;
 }
#endif
#endif
 return 0;
}
#endif
int frames,batch,cancel_after=-1,game_calls,wait_calls,io_writes,creates,corrupt_read,write_zero;
int max_write=100000,result_pending,init_failure;
uint result_key=NARM_KEY_ESCAPE;
int fixture_allow_game,fixture_game_entered,fixture_expected_hud;
int fixture_game_writes,fixture_game_creates;
uint fixture_verified_bytes,fixture_game_length,fixture_game_hash;
struct MCPlayerState fixture_expected_player;
int forced_event;
char *override_content;
#ifdef MC10_CULL_PROFILE
char file_data[98304],created_path[64];uint file_length;
#elif defined(MC10_STAGE_PROFILE)
char file_data[65536],created_path[64];uint file_length;
#else
char file_data[32768],created_path[64];uint file_length;
#endif
char file_presence[10000]={0,1};
#ifdef MC10_CULL_PROFILE
#define FIXTURE_LOG_PATTERN "/MC10/profiles/b26_%4d.csv"
#define FIXTURE_SECOND_PATH "/MC10/profiles/b26_0002.csv"
#elif defined(MC10_PREPARATION_PROFILE)
#define FIXTURE_LOG_PATTERN "/MC10/profiles/b24_%4d.csv"
#define FIXTURE_SECOND_PATH "/MC10/profiles/b24_0002.csv"
#elif defined(MC10_GEOMETRY_PROFILE)
#define FIXTURE_LOG_PATTERN "/MC10/profiles/b23_%4d.csv"
#define FIXTURE_SECOND_PATH "/MC10/profiles/b23_0002.csv"
#elif defined(MC10_STAGE_PROFILE)
#define FIXTURE_LOG_PATTERN "/MC10/profiles/b22_%4d.csv"
#define FIXTURE_SECOND_PATH "/MC10/profiles/b22_0002.csv"
#elif defined(MC10_TILE_PROFILE)
#define FIXTURE_LOG_PATTERN "/MC10/profiles/b21_%4d.csv"
#define FIXTURE_SECOND_PATH "/MC10/profiles/b21_0002.csv"
#elif defined(MC10_SAMPLER_PROFILE)
#define FIXTURE_LOG_PATTERN "/MC10/profiles/b20_%4d.csv"
#define FIXTURE_SECOND_PATH "/MC10/profiles/b20_0002.csv"
#elif defined(MC10_LEGACY_FONT)
#define FIXTURE_LOG_PATTERN "/MC10/profiles/b17_%4d.csv"
#define FIXTURE_SECOND_PATH "/MC10/profiles/b17_0002.csv"
#else
#define FIXTURE_LOG_PATTERN "/MC10/profiles/b15_%4d.csv"
#define FIXTURE_SECOND_PATH "/MC10/profiles/b15_0002.csv"
#endif
static int file_id(const char *path){int id=0;if(sscanf(path,FIXTURE_LOG_PATTERN,&id)!=1||id<1||id>9999)return 0;return id;}
uint narm_micros(void){return fixture_us;}
uint narm_millis(void){return fixture_us/1000;}
int narm_yield(void){fixture_us+=10;return 0;}
int narm_sleep_ms(uint ms){fixture_us+=ms*1000;return 0;}
int narm_next_key_event(uint *key,uint *action,uint *mods,uint *character)
{
 if(result_pending){result_pending=0;*key=result_key;*action=NARM_KEY_ACTION_PRESS;return 1;}
 if(cancel_after>=0&&frames>=cancel_after){cancel_after=-1;*key=NARM_KEY_ESCAPE;*action=NARM_KEY_ACTION_PRESS;return 1;}
 return 0;
}
int narm_poll_system_event(void){int event=forced_event;forced_event=0;return event;}
int narm_half_clear(uint c){return 0;}
int narm_half_present(void){return 0;}
int fixture_preparation_title_seen,fixture_cull_title_seen,fixture_cull_completed_title_seen;
void mc_menu_draw_centered_string(char *s,int x,int y,uint c)
{if(!strcmp(s,"Samples: 60/60  (72 frames, 3 modes)"))fixture_cull_completed_title_seen=1;if(!strcmp(s,"BUILD 26 VISIBILITY CULLING"))fixture_cull_title_seen=1;if(!strcmp(s,"BUILD 24 PREPARATION FIXES"))fixture_preparation_title_seen=1;if(!strcmp(s,"PROFILE SAVED AND VERIFIED")||!strcmp(s,"PROFILE FAILED"))result_pending=1;}
void mc_world_copy(char *out,int cap,char *s){snprintf(out,cap,"%s",s);}
void mc_world_append(char *out,int cap,char *s){snprintf(out+strlen(out),cap-strlen(out),"%s",s);}
void mc_world_append_char(char *out,int cap,char c){int n=strlen(out);if(n+1<cap){out[n]=c;out[n+1]=0;}}
void mc_world_append_uint(char *out,int cap,uint v){snprintf(out+strlen(out),cap-strlen(out),"%u",v);}
void mc_world_append_int(char *out,int cap,int v){snprintf(out+strlen(out),cap-strlen(out),"%d",v);}
void mc_world_append_hex8(char *out,int cap,uint v){snprintf(out+strlen(out),cap-strlen(out),"%08x",v);}
int mc_menu_text_equals(char *a,char *b){return !strcmp(a,b);}
void mc_blocks_init(void){}
int mc_asset_open(void){return !init_failure;}
int mc_menu_init(void){return 1;}
int mc_options_init(void){mc_options.fancy_graphics=1;mc_options.gamma_setting=.5f;return 1;}
int mc_world_init(void){return 1;}
int mc_world_read_info(char *folder,struct MCWorldInfo *w)
{mc_world_copy(w->folder,64,folder);mc_world_copy(w->level_name,80,"A \"quoted\", world");return 1;}
int fixture_world_bootstraps;
int mc_runtime_bootstrap(struct MCWorldInfo *w){fixture_world_bootstraps++;return 0;}
int mc_math_init(void){return 1;}
void mc_player_init(void){}
int mc_render_prepare_assets(void){return 1;}
int mc_render_build_cache(void){return 1;}
void mc_game_run(struct MCWorldInfo *w);
void mc_wait_for_return(void){wait_calls++;}
#include "../src/mc10_profile.tc"
void mc_render_begin_frame(void)
{
 assert(frames<MC_FIXTURE_FRAME_CAP);frame_yaw[frames]=mc_runtime_player.yaw_bits;frame_pitch[frames]=mc_runtime_player.pitch_bits;
#ifndef MC10_CULL_PROFILE
 assert(!mc_render_cull_mode&&!mc_render_cull_diagnostics);
#endif
#if defined(MC10_SAMPLER_PROFILE) || defined(MC10_TILE_PROFILE)
 frame_flags[frames]=fixture_flags;
#endif
#ifdef MC10_TILE_PROFILE
 frame_mode[frames]=(uint)mc_render_list_enabled;
#endif
#ifdef MC10_GEOMETRY_PROFILE
 frame_submit[frames]=(uint)mc_render_submit_optimized;
#ifdef MC10_CULL_PROFILE
 frame_cull[frames]=(uint)mc_render_cull_mode;frame_cull_diagnostics[frames]=(uint)mc_render_cull_diagnostics;
 assert(mc_render_submit_optimized==1&&!(fixture_flags&2048));
 assert(mc_render_cull_diagnostics==((fixture_flags&512)&&mc_render_cull_mode?1:0));
 mc_render_cull_backfaces=0;mc_render_cull_eligible=0;
 if(mc_render_cull_diagnostics){mc_render_cull_backfaces=6;mc_render_cull_eligible=20;}
#else
 assert(!!(fixture_flags&2048)==!mc_render_submit_optimized);
#endif
#endif
 frame_hud[frames]=mc_render_show_hud;
 batch=0;fixture_us+=200;mc_profile_setup_us+=200;
}
int mc_render_step_frame(void)
{
 uint cost=1000+frames*10,draw=700;
#ifdef MC10_GEOMETRY_PROFILE
#ifdef MC10_CULL_PROFILE
 if(mc_render_cull_mode==1){cost=750+frames*10;draw=550;}
 if(mc_render_cull_mode==3){cost=650+frames*10;draw=450;}
#else
 if(mc_render_submit_optimized){cost=650+frames*10;draw=450;}
#endif
#endif
 assert(mc_render_benchmark_running);
#ifdef MC10_TILE_PROFILE
 if(frames==fixture_service_failure){mc_render_list_error=-4;return -1;}
#endif
 fixture_us+=cost;mc_profile_render_us+=cost;mc_profile_draw_us+=draw;
 if(batch++==0)return 0;
 fixture_us+=20+(mc_render_show_hud?80:0);mc_profile_ui_us+=20+(mc_render_show_hud?80:0);
 mc_profile_hud_us+=mc_render_show_hud?80:0;mc_profile_present_us+=20;
 mc_render_last_visited=mc_runtime_player.pitch_bits==0xc2b40000?752:4000;
 mc_render_last_skipped=7642-mc_render_last_visited;
 mc_profile_finish((mc_runtime_player.pitch_bits==0xc2b40000?16:50)
#ifdef MC10_CULL_PROFILE
                  -(mc_render_cull_mode?6:0)
#endif
                  ,2,0,0);frames++;return 1;
}
/* These stubs reject any attempt to perform file I/O inside timed frames. */
int narm_file_exists(const char *path,uint storage)
{assert(!mc_profile_active);if(!strcmp(path,"/MC10/profile_world.txt"))return override_content!=NULL;return file_presence[file_id(path)];}
int narm_dir_make(const char *path,uint storage){assert(!mc_profile_active);return 1;}
int narm_file_create(const char *path,uint storage)
{assert(!fixture_game_entered&&!mc_profile_active);assert(file_id(path)&&!file_presence[file_id(path)]);file_presence[file_id(path)]=1;creates++;snprintf(created_path,64,"%s",path);file_length=0;fixture_verified_bytes=0;return 1;}
int narm_file_write(const char *path,const void *data,uint off,uint n,uint storage)
{
 assert(!fixture_game_entered&&!mc_profile_active);assert(!strcmp(path,created_path));io_writes++;
 if(write_zero)return 0;if(n>(uint)max_write)n=max_write;
 assert(off+n<=sizeof(file_data));memcpy(file_data+off,data,n);if(off+n>file_length)file_length=off+n;return n;
}
int narm_file_size(const char *path,uint storage,uint *n)
{assert(!mc_profile_active);if(!strcmp(path,"/MC10/profile_world.txt")){*n=(uint)strlen(override_content);return 0;}if(!file_presence[file_id(path)])return -3;*n=!strcmp(path,created_path)?file_length:0;return 0;}
int narm_file_read(const char *path,void *out,uint off,uint n,uint storage)
{
 assert(!mc_profile_active);
 if(!strcmp(path,"/MC10/profile_world.txt")){memcpy(out,override_content+off,n);return n;}
 assert(off+n<=file_length);memcpy(out,file_data+off,n);if(corrupt_read)((char *)out)[0]^=1;
 if(!corrupt_read)fixture_verified_bytes+=n;return n;
}
#include "../src/mc10_benchmark.tc"

void mc_game_run(struct MCWorldInfo *w)
{
 assert(fixture_allow_game&&!fixture_game_entered&&w==&mc_bench_world);
 assert(!memcmp(&fixture_expected_player,&mc_runtime_player,sizeof(fixture_expected_player)));
 assert(mc_render_show_hud==fixture_expected_hud&&!mc_profile_active&&!mc_render_benchmark_running);
 assert(mc_render_cull_mode==3&&!mc_render_cull_diagnostics);
 assert(!mc_bench_cancelled&&file_length&&fixture_verified_bytes==file_length);
 assert(file_length==mc_bench_length&&!memcmp(file_data,mc_bench_log,file_length));
#if defined(MC10_SAMPLER_PROFILE) || defined(MC10_TILE_PROFILE)
 assert(!fixture_flags);
#endif
#ifdef MC10_TILE_PROFILE
 assert(mc_render_list_enabled==1&&!mc_render_list_count&&!mc_render_list_error);
#endif
#ifdef MC10_GEOMETRY_PROFILE
 assert(mc_render_submit_optimized==1);
#endif
 fixture_game_writes=io_writes;fixture_game_creates=creates;fixture_game_length=file_length;
 fixture_game_hash=mc_bench_fnv(file_data,(int)file_length);
 fixture_game_entered=1;game_calls++;
}

static void reset_view(void)
{frames=0;batch=0;cancel_after=-1;mc_render_show_hud=1;mc_profile_cancel();mc_runtime_player.yaw_bits=0x41200000;mc_runtime_player.pitch_bits=0x41a00000;mc_runtime_player.present=1;}

/* Exercise the same result-screen handoff in every profiler configuration.
 * The old fixtures always pressed Escape and never covered the advertised
 * Enter action. Keep this separate from the final report-producing run. */
static void fixture_test_result_handoff(int expected_frames,int expected_samples)
{
 int hud,before_creates,saved_wait_calls=wait_calls;uint saved_us=fixture_us;
 result_key=NARM_KEY_ENTER;max_write=100000;
 for(hud=0;hud<2;hud++)
 {
  reset_view();mc_render_show_hud=hud;fixture_expected_hud=hud;
#ifndef MC10_CULL_PROFILE
  /* Main must force historical benchmark state even after culling was on. */
  mc_render_cull_mode=3;mc_render_cull_diagnostics=1;
#endif
  fixture_expected_player=mc_runtime_player;fixture_allow_game=1;game_calls=0;
  assert(mc_benchmark_main()==0&&frames==expected_frames&&mc_bench_completed==expected_samples);
  assert(game_calls==1&&fixture_game_entered&&!mc_render_benchmark_running);
  assert(io_writes==fixture_game_writes&&creates==fixture_game_creates&&file_length==fixture_game_length);
  assert(mc_bench_fnv(file_data,(int)file_length)==fixture_game_hash);
  assert(!memcmp(file_data,mc_bench_log,file_length));
  fixture_game_entered=0;fixture_allow_game=0;
 }
 /* Enter must not start gameplay if CSV creation/readback did not succeed. */
 reset_view();game_calls=0;write_zero=1;
 assert(mc_benchmark_main()==1&&!game_calls&&!fixture_game_entered&&!mc_render_benchmark_running);
 write_zero=0;
 reset_view();corrupt_read=1;
 assert(mc_benchmark_main()==1&&!game_calls&&!fixture_game_entered&&!mc_render_benchmark_running);
 corrupt_read=0;
 /* Cancellation must return without creating a partial CSV or handing off. */
 reset_view();before_creates=creates;cancel_after=2;
 assert(mc_benchmark_main()==2&&!game_calls&&creates==before_creates&&!mc_render_benchmark_running);
 /* A successful run still allows Escape to exit without entering gameplay. */
 reset_view();result_key=NARM_KEY_ESCAPE;
 assert(mc_benchmark_main()==0&&!game_calls&&!fixture_game_entered&&!mc_render_benchmark_running);
 fixture_us=saved_us;wait_calls=saved_wait_calls;reset_view();
}
#ifdef MC10_CULL_PROFILE
#include "cull_fixture_main.c"
#elif defined(MC10_GEOMETRY_PROFILE)
#include "geometry_fixture_main.c"
#elif defined(MC10_STAGE_PROFILE)
#include "stage_fixture_main.c"
#elif defined(MC10_TILE_PROFILE)
#include "tile_fixture_main.c"
#elif defined(MC10_SAMPLER_PROFILE)
#include "sampler_fixture_main.c"
#else
int main(void)
{
 int i;uint yaw,pitch;struct MCPlayerState saved;
 reset_view();mc_runtime_player.pos_x.hi=0xc0620000;mc_runtime_player.pos_x.lo=123;
 saved=mc_runtime_player;yaw=saved.yaw_bits;pitch=saved.pitch_bits;
 assert(mc_bench_collect()==1&&frames==24&&mc_bench_completed==20);
 assert(!memcmp(&saved,&mc_runtime_player,sizeof(saved))&&!mc_profile_active&&mc_render_show_hud==1);
 for(i=0;i<24;i++){assert(frame_yaw[i]==yaw);assert(frame_pitch[i]==(i<12?pitch:0xc2b40000));assert(frame_hud[i]==((i/6)%2==0));}
 assert(mc_bench_samples[0].frame.geometry_us==620&&mc_bench_samples[4].frame.geometry_us==700);
 assert(mc_bench_samples[5].frame.geometry_us==740&&mc_bench_samples[9].frame.geometry_us==820);
 for(i=0;i<20;i++)assert(mc_bench_samples[i].frame.tick_us==0&&mc_bench_samples[i].frame.ticks==0);
 reset_view();saved=mc_runtime_player;forced_event=NARM_EVENT_FOCUS_LOST;
 assert(!mc_bench_collect()&&!memcmp(&saved,&mc_runtime_player,sizeof(saved)));
 mc_world_copy(mc_world_list[0].folder,64,"New World");override_content=NULL;
 assert(mc_bench_choose_world()&&!strcmp(mc_bench_world.folder,"New World"));
 override_content="Pinned World\r\n";assert(mc_bench_choose_world()&&!strcmp(mc_bench_world.folder,"Pinned World"));
 override_content="../world";assert(!mc_bench_choose_world());override_content="bad\nsecond";assert(!mc_bench_choose_world());
 override_content=NULL;assert(mc_bench_choose_world());
 mc_bench_format_log();assert(!mc_bench_full&&mc_bench_length>0);
 max_write=17;assert(mc_bench_write_log());assert(!strcmp(created_path,FIXTURE_SECOND_PATH));
 assert(io_writes>1&&file_length==mc_bench_length&&!memcmp(file_data,mc_bench_log,file_length));
 corrupt_read=1;assert(!mc_bench_write_log());corrupt_read=0;
 write_zero=1;assert(!mc_bench_write_log());write_zero=0;
 reset_view();saved=mc_runtime_player;cancel_after=2;
 assert(!mc_bench_collect()&&mc_bench_cancelled&&mc_bench_completed==1);
 assert(!memcmp(&saved,&mc_runtime_player,sizeof(saved))&&!mc_profile_active);
 reset_view();cancel_after=8;assert(!mc_bench_collect()&&mc_render_show_hud==1);
 fixture_test_result_handoff(24,20);
 /* Main startup bypasses menu/game loops and completes automatically. */
 reset_view();mc_bench_path[0]=0;max_write=100000;assert(mc_benchmark_main()==0);
 assert(frames==24&&game_calls==0&&mc_bench_completed==20);
 init_failure=1;assert(mc_benchmark_main()==1&&wait_calls==1);init_failure=0;
 /* Overflow is detected before any write; original report can be rebuilt. */
 mc_bench_length=MC_BENCH_LOG_CAP-1;mc_bench_char('X');assert(mc_bench_full&&!mc_bench_write_log());
 mc_bench_format_log();fwrite(mc_bench_log,1,mc_bench_length,stdout);
 return 0;
}
#endif
