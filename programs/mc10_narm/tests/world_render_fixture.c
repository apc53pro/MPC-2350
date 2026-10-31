/* Actual production renderer with source-registry, clock, decoded meshes and
 * the shared workspace. Host I/O and presentation only are test adapters. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define MC10_WORLD_SYSTEMS 1
#define MC10_PLAYER_FOUNDATION 1
struct MCBiomeSample {int biome;float temperature;float rainfall;};
int mc_biome_query(int x,int z,struct MCBiomeSample *out){out->biome=1;out->temperature=.5f;out->rainfall=1.0f;return 1;}
#include "render_fixture.c"
char fixture_mc10_root[1024];
uint mc_asset_read_u32(char *data,int offset)
{uint a=(unsigned char)data[offset],b=(unsigned char)data[offset+1],c=(unsigned char)data[offset+2],d=(unsigned char)data[offset+3];return a|(b<<8)|(c<<16)|(d<<24);}
FILE *fixture_open(const char *path)
{char full[1200];snprintf(full,sizeof(full),"%s/%s",fixture_mc10_root,path+6);return fopen(full,"rb");}
int narm_file_read(const char *path,void *buffer,uint offset,uint count,uint storage)
{FILE *f=fixture_open(path);int got;if(!f)return -1;fseek(f,offset,SEEK_SET);got=fread(buffer,1,count,f);fclose(f);return got;}
int narm_file_size(const char *path,uint storage,uint *out)
{FILE *f=fixture_open(path);if(!f)return -1;fseek(f,0,SEEK_END);*out=(uint)ftell(f);fclose(f);return 0;}
float mc_game_partial_tick(void){return 0;}
void mc_hud_draw(void){}
void mc_viewmodel_capture_frame(float partial){}
void mc_viewmodel_draw(float partial){}
void mc_third_adjust_camera(void){}
void mc_third_capture_frame(float partial){}
void mc_third_draw(void){}
int fixture_tesr_draws;
int mc_tp_world_tesr(int id,int metadata,int x,int y,int z,float partial){fixture_tesr_draws++;return 1;}
int mc_third_current_frame_mode(void){return 0;}
float mc_third_current_frame_partial(void){return 0;}
void mc_sky_draw(float partial){}
void *fixture_geometry(void){return &mc_render_geometry[0];}
void *fixture_workspace(void){return &mc_world_workspace[0];}
int fixture_rebuild(void)
{
 int calls=0;if(!mc_block_colors_open())return -2;
 mc_block_colors_begin(mc_runtime_center_x,mc_runtime_center_z);
 while(!mc_block_colors_pump()){if(++calls>100)return -3;}
 calls=0;mc_render_rebuild_begin();while(!mc_render_rebuild_step()){if(++calls>100000)return -1;}return mc_render_rebuild_block_error();
}
int fixture_tile_status(int id){return mc_block_static_status((uint)id);}
