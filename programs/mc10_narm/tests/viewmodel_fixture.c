#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
typedef unsigned int uint;
#define MC10_PLAYER_FOUNDATION 1
#define NARM_STORAGE_SD 1
char fixture_asset_path[1024];
int narm_storage_mount(int storage){return 1;}
int narm_file_size(const char *path,int storage,uint *size)
{FILE *f=fopen(fixture_asset_path,"rb");if(!f)return -1;fseek(f,0,SEEK_END);*size=(uint)ftell(f);fclose(f);return 0;}
int narm_file_read(const char *path,void *out,uint offset,uint bytes,int storage)
{FILE *f=fopen(fixture_asset_path,"rb");if(!f)return -1;fseek(f,offset,SEEK_SET);int result=(int)fread(out,1,bytes,f);fclose(f);return result;}
int narm_half_blit(int x,int y,int w,int h,const uint *p){return 0;}
int narm_half_blit_masked(int x,int y,int w,int h,const uint *p,const uint *m){return 0;}
#include "src/mc10_u64.tc"
#include "src/mc10_java_double.tc"
#include "src/mc10_fp64.tc"
#include "src/mc10_math.tc"
#include "src/mc10_assets.tc"
#include "mc10_atan.tc"
struct MCItemStack{int id,count,damage,tag_offset,tag_bytes;};
struct MCItemStack mc_inventory_slots[40];int mc_inventory_selected;
struct MCItemStack *mc_inventory_current(void){return &mc_inventory_slots[mc_inventory_selected];}
void mc_inventory_empty(struct MCItemStack *s){memset(s,0,sizeof(*s));}
void mc_inventory_copy(struct MCItemStack *a,struct MCItemStack *b){memcpy(a,b,sizeof(*a));}
struct MCPhysicalPlayer{float yaw,pitch;int grounded,sneak,water;};
struct MCPhysicalPlayer mc_player;
struct MCPhysicalRuntime{int health,block_x,block_y,block_z;};
struct MCPhysicalRuntime mc_runtime_player;
struct MCSettings{int view_bobbing,render_distance,fancy;};struct MCSettings mc_options;
int mc_render_sky_subtracted,mc_hud_underwater;uint mc_render_grass_color=0x3c,mc_render_light_lookup[128];
int mc_render_origin_x,mc_render_origin_z;float mc_render_view_x,mc_render_view_y,mc_render_view_z,mc_render_camera[18];
uint fixture_water_id,fixture_water_metadata,fixture_water_above;int fixture_water_y,fixture_world_unknown;
int mc_runtime_try_get_block(int x,int y,int z,uint *out){*out=y==fixture_water_y?fixture_water_id:fixture_water_above;return !fixture_world_unknown;}
int mc_runtime_try_get_metadata(int x,int y,int z,uint *out){*out=fixture_water_metadata;return !fixture_world_unknown;}
int mc_runtime_try_get_saved_light(int sky,int x,int y,int z,uint *out){*out=sky?15:0;return 1;}
uint fixture_depth_protocol=0x44430001;int fixture_clears;
uint renderStat(uint index){return index==76?fixture_depth_protocol:0;}
int narm_half_clear_depth(void){fixture_clears++;return 0;}
float narm_sin(float x){return sinf(x);}float narm_cos(float x){return cosf(x);}
uint fixture_item_pixels[64],fixture_item_mask[8];
int mc_player_ui_icon(struct MCItemStack *s,uint **p,uint **m){*p=fixture_item_pixels;*m=fixture_item_mask;return s->count>0&&s->id>=256;}
int mc_item_flat_block_icon(int id,int damage){return -1;}
struct FixtureFace{float vertices[20],transform[18];uint width,height,flags;uint pixels[512],mask[64];};
struct FixtureFace fixture_faces[128];int fixture_face_count;
void fillQuad3DTexZMask(float x0,float y0,float z0,float u0,float v0,float x1,float y1,float z1,float u1,float v1,
 float x2,float y2,float z2,float u2,float v2,float x3,float y3,float z3,float u3,float v3,
 const uint *texture,uint width,uint height,uint flags,const float *transform,const uint *mask)
{
 assert(fixture_face_count<128&&width*height<=2048);
 struct FixtureFace *face=&fixture_faces[fixture_face_count++];
 float v[20]={x0,y0,z0,u0,v0,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2,x3,y3,z3,u3,v3};
 memcpy(face->vertices,v,sizeof(v));memcpy(face->transform,transform,72);
 face->width=width;face->height=height;face->flags=flags;
 memcpy(face->pixels,texture,width*height);memcpy(face->mask,mask,(width*height+31)/32*4);
}
#include "mc10_viewmodel.tc"
void fixture_init(void)
{
 mc_fp_init();memset(mc_inventory_slots,0,sizeof(mc_inventory_slots));mc_inventory_selected=0;
 memset(&mc_player,0,sizeof(mc_player));mc_player.grounded=1;mc_runtime_player.health=20;
 mc_options.view_bobbing=0;mc_options.render_distance=0;fixture_face_count=fixture_clears=0;
 for(int i=0;i<128;++i)mc_render_light_lookup[i]=i>>3;
 for(int i=0;i<64;++i)fixture_item_pixels[i]=0xe0e0e0e0;
 for(int i=0;i<8;++i)fixture_item_mask[i]=0xffffffff;
 mc_viewmodel_init();
}
void fixture_draw(float partial){fixture_face_count=fixture_clears=0;mc_viewmodel_draw(partial);}
