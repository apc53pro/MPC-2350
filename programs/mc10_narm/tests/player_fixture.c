/* Test-only read-only world adapter. Production player/collision/math modules
 * are included unchanged; synthetic worlds isolate collision and locomotion. */
typedef unsigned int uint;
#define NARM_STORAGE_SD 1
int narm_file_read(const char *p,void *out,uint off,uint bytes,uint storage){return -1;}
#include "../src/mc10_types.tc"
#include "../src/mc10_u64.tc"
#include "../src/mc10_java_double.tc"
#include "../src/mc10_fp64.tc"
struct MCWorldInfo {int game_type;};
struct MCWorldRuntime {struct MCWorldInfo *info;};
struct MCPlayerState {
 int present;struct MCJDouble pos_x;struct MCJDouble pos_y;struct MCJDouble pos_z;
 struct MCJDouble motion_x;struct MCJDouble motion_y;struct MCJDouble motion_z;
 uint yaw_bits;uint pitch_bits;uint fall_distance_bits;
 int dimension;int fire;int air;int on_ground;int health;int block_x;int block_y;int block_z;int food;int flying;int mayfly;
};
struct MCGameSettings {float mouse_sensitivity;int invert_mouse;};
struct MCWorldInfo fixture_info;
struct MCWorldRuntime mc_runtime_world;
struct MCPlayerState mc_runtime_player;
struct MCGameSettings mc_options;
char fixture_blocks[48*128*48];
char fixture_metadata[48*128*48];
int fixture_x=-24,fixture_z=-24;
int fixture_missing;
int mc_region_floor_div(int value,int divisor)
{int q=value/divisor;return value<0&&value%divisor?q-1:q;}
int mc_runtime_chunk_exists(int x,int z)
{return !fixture_missing&&x>=mc_region_floor_div(fixture_x,16)&&x<=mc_region_floor_div(fixture_x+47,16)&&z>=mc_region_floor_div(fixture_z,16)&&z<=mc_region_floor_div(fixture_z+47,16);}
int mc_runtime_try_get_block(int x,int y,int z,uint *out)
{*out=0;if(y<0||y>=128)return 1;if(x<fixture_x||x>=fixture_x+48||z<fixture_z||z>=fixture_z+48||fixture_missing)return 0;*out=(uint)fixture_blocks[((x-fixture_x)*48+(z-fixture_z))*128+y]&255;return 1;}
int mc_runtime_try_get_metadata(int x,int y,int z,uint *out)
{uint id;*out=0;if(!mc_runtime_try_get_block(x,y,z,&id))return 0;if(y<0||y>=128)return 1;*out=(uint)fixture_metadata[((x-fixture_x)*48+(z-fixture_z))*128+y]&255;return 1;}
#include "../src/mc10_blocks.tc"
#include "../src/mc10_paths.tc"
#include "../src/mc10_math.tc"
#include "../src/mc10_collision.tc"
#include "../src/mc10_player.tc"
void mc_fixture_init(void)
{mc_runtime_world.info=&fixture_info;mc_blocks_init();mc_player_init();}
