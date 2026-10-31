#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
typedef unsigned int uint;
#define MC10_PLAYER_FOUNDATION 1
#define MC10_WORLD_SYSTEMS 1
#define NARM_STORAGE_SD 1
char fixture_asset_path[1024];
int narm_storage_mount(int s){return 1;}
int narm_file_size(const char *p,int s,uint *n){FILE*f=fopen(fixture_asset_path,"rb");if(!f)return -1;fseek(f,0,SEEK_END);*n=ftell(f);fclose(f);return 0;}
int narm_file_read(const char*p,void*out,uint o,uint n,int s){FILE*f=fopen(fixture_asset_path,"rb");if(!f)return -1;fseek(f,o,SEEK_SET);int r=fread(out,1,n,f);fclose(f);return r;}
int narm_half_blit(int x,int y,int w,int h,const uint*p){return 0;}
int narm_half_blit_masked(int x,int y,int w,int h,const uint*p,const uint*m){return 0;}
uint fixture_screen[320*240];
int narm_half_pixel(uint index,uint color){assert(index<320*240);fixture_screen[index]=color;return 0;}
void mc_world_append(char*out,int n,char*text){strncat(out,text,n-strlen(out)-1);}
#include "src/mc10_u64.tc"
#include "src/mc10_java_double.tc"
#include "src/mc10_fp64.tc"
#include "src/mc10_math.tc"
#include "src/mc10_assets.tc"
#include "src/mc10_atan.tc"
#include "src/mc10_items.tc"
struct MCItemStack mc_inventory_slots[40];int mc_inventory_selected;
struct MCItemStack*mc_inventory_current(void){return &mc_inventory_slots[mc_inventory_selected];}
void mc_inventory_empty(struct MCItemStack*s){memset(s,0,sizeof(*s));}
void mc_inventory_copy(struct MCItemStack*a,struct MCItemStack*b){if(b)memcpy(a,b,sizeof(*a));else memset(a,0,sizeof(*a));}
struct TestPlayer{float yaw,pitch;int grounded,sneak,water;};struct TestPlayer mc_player;
struct TestRuntime{int health,block_x,block_y,block_z;uint yaw_bits,pitch_bits;struct MCJDouble pos_x,pos_y,pos_z;};struct TestRuntime mc_runtime_player;
struct TestSettings{int view_bobbing,render_distance,fancy_graphics;};struct TestSettings mc_options;
int mc_render_sky_subtracted,mc_hud_underwater;uint mc_render_grass_color=0x3c,mc_render_light_lookup[128];
int mc_render_origin_x,mc_render_origin_z;float mc_render_view_x,mc_render_view_y,mc_render_view_z,mc_render_camera[18];
float mc_render_float_from_bits(uint bits){float f;memcpy(&f,&bits,4);return f;}
float mc_render_double_to_float(struct MCJDouble*v){uint64_t bits=(uint64_t)v->lo|((uint64_t)v->hi<<32);double d;memcpy(&d,&bits,8);return (float)d;}
int fixture_block_xyz[3],fixture_id=1;float fixture_bounds[6]={0,0,0,1,1,1};
uint fixture_neighbors[6];
int mc_runtime_try_get_block(int x,int y,int z,uint*out)
{int dx=x-fixture_block_xyz[0],dy=y-fixture_block_xyz[1],dz=z-fixture_block_xyz[2];*out=dx==0&&dy==0&&dz==0?fixture_id:0;if(dy==0&&dz==0&&dx==-1)*out=fixture_neighbors[0];if(dy==0&&dz==0&&dx==1)*out=fixture_neighbors[1];if(dx==0&&dy==0&&dz==-1)*out=fixture_neighbors[2];if(dx==0&&dy==0&&dz==1)*out=fixture_neighbors[3];return 1;}
int mc_runtime_try_get_metadata(int x,int y,int z,uint*out){*out=0;return 1;}
int mc_runtime_try_get_saved_light(int sky,int x,int y,int z,uint*out){*out=sky?15:0;return 1;}
int mc_block_ray_collidable(int id,int metadata,int liquids){return id!=0&&id!=8&&id!=9&&id!=10&&id!=11&&id!=51;}
int mc_block_is_opaque(uint id){return id==1;}
void mc_block_bounds_at(uint id,uint metadata,int x,int y,int z,float*x0,float*y0,float*z0,float*x1,float*y1,float*z1)
{*x0=fixture_bounds[0];*y0=fixture_bounds[1];*z0=fixture_bounds[2];*x1=fixture_bounds[3];*y1=fixture_bounds[4];*z1=fixture_bounds[5];}
uint renderStat(uint i){return i==76?0x44430001:0;}int fixture_clears;
int narm_half_clear_depth(void){fixture_clears++;return 0;}
float narm_sin(float x){return sinf(x);}float narm_cos(float x){return cosf(x);}
uint fixture_item_pixels[64],fixture_item_mask[8];
#define MC_UI_ICON_COUNT 48
uint mc_ui_icon_pixels[48*64],mc_ui_icon_masks[48*8];int mc_ui_icon_ids[48],mc_ui_icon_damage[48],mc_ui_icon_used;
int mc_player_ui_icon(struct MCItemStack*s,uint**p,uint**m){*p=fixture_item_pixels;*m=fixture_item_mask;return s->count>0&&s->id>=256;}
struct FixtureFace{float vertices[20],transform[18];uint width,height,flags;uint pixels[512],mask[64];};
struct FixtureFace fixture_faces[256];int fixture_face_count;
void fillQuad3DTexZMask(float x0,float y0,float z0,float u0,float v0,float x1,float y1,float z1,float u1,float v1,float x2,float y2,float z2,float u2,float v2,float x3,float y3,float z3,float u3,float v3,const uint*t,uint w,uint h,uint flags,const float*transform,const uint*mask)
{
 assert(fixture_face_count<256&&w*h<=2048);struct FixtureFace*f=&fixture_faces[fixture_face_count++];
 float v[20]={x0,y0,z0,u0,v0,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2,x3,y3,z3,u3,v3};memcpy(f->vertices,v,80);memcpy(f->transform,transform,72);f->width=w;f->height=h;f->flags=flags;memcpy(f->pixels,t,w*h);memcpy(f->mask,mask,(w*h+31)/32*4);
}
#include "mc10_viewmodel.tc"
#include "mc10_third_person.tc"
void fixture_init(void)
{
 mc_fp_init();memset(mc_inventory_slots,0,sizeof(mc_inventory_slots));mc_inventory_selected=0;memset(&mc_player,0,sizeof(mc_player));memset(&mc_runtime_player,0,sizeof(mc_runtime_player));mc_player.grounded=1;mc_runtime_player.health=20;fixture_face_count=fixture_clears=0;
 memset(fixture_neighbors,0,sizeof(fixture_neighbors));
 for(int i=0;i<128;i++)mc_render_light_lookup[i]=i>>3;for(int i=0;i<64;i++)fixture_item_pixels[i]=0xe0e0e0e0;for(int i=0;i<8;i++)fixture_item_mask[i]=0xffffffff;
 mc_viewmodel_init();mc_third_init();
}
void fixture_box(int part,float inflate){fixture_face_count=0;mc_vm_identity();for(int i=0;i<16;i++)mc_tp_base[i]=mc_vm_matrix[i];mc_tp_model_box(part,inflate,mc_vm_skin,mc_vm_skin_mask);}
int fixture_ray(double sx,double sy,double sz,double ex,double ey,double ez,double*out)
{
 double in[6]={sx,sy,sz,ex,ey,ez};int start[3],end[3],hit[3];mc_fp_reset();for(int i=0;i<6;i++){uint64_t b;memcpy(&b,&in[i],8);int r=mc_fp_bits((uint)b,(uint)(b>>32));if(i<3)start[i]=r;else end[i-3]=r;}
 int found=mc_tp_ray(start,end,hit);if(found)for(int i=0;i<3;i++){uint64_t b=(uint64_t)mc_fp_values[hit[i]].lo|((uint64_t)mc_fp_values[hit[i]].hi<<32);memcpy(out+i,&b,8);}return found;
}

