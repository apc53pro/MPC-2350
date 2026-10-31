#include <stdio.h>
#include <math.h>
typedef unsigned int uint;
#define MC10_WORLD_SYSTEMS 1
#define NARM_STORAGE_SD 0
char fixture_sky_path[1024];
int narm_file_read(char *name,void *data,int offset,int size,int storage)
{FILE *f=fopen(fixture_sky_path,"rb");int result;if(!f)return -1;fseek(f,offset,SEEK_SET);result=fread(data,1,size,f);fclose(f);return result;}
int narm_half_blit(int x,int y,int width,int height,const uint *pixels){return 0;}
float narm_sin(float v){return sinf(v);}float narm_cos(float v){return cosf(v);}
#include "../src/mc10_u64.tc"
#include "../src/mc10_java_double.tc"
#include "../src/mc10_fp64.tc"
#include "../src/mc10_math.tc"
struct SkyInfo {uint world_time_lo;uint world_time_hi;int raining;int thundering;};
struct SkyInfo sky_info;
struct {struct SkyInfo *info;} mc_runtime_world;
struct {int anaglyph;} mc_options;
struct {int block_x;int block_z;struct MCJDouble pos_y;} mc_runtime_player;
struct MCBiomeSample {int biome;float temperature;float rainfall;};
float fixture_temperature;
int mc_biome_query(int x,int z,struct MCBiomeSample *sample){sample->temperature=fixture_temperature;return 1;}
float mc_render_camera[18];uint mc_asset_pixels[19200];
float mc_render_double_to_float(struct MCJDouble *v){return mc_fp_to_f32(mc_fp_bits(v->lo,v->hi));}
int mc_third_interpolated_eye_y(float partial){return mc_fp_bits(mc_runtime_player.pos_y.lo,mc_runtime_player.pos_y.hi);}
#include "../src/mc10_cos_celestial.tc"
#include "../src/mc10_world_clock.tc"
#include "../src/mc10_sky.tc"
void fixture_init(uint lo,uint hi,float temperature,float rain,float thunder)
{mc_runtime_world.info=&sky_info;sky_info.world_time_lo=lo;sky_info.world_time_hi=hi;fixture_temperature=temperature;mc_world_rain_strength=rain;mc_world_thunder_strength=thunder;mc_fp_init();mc_sky_angle=mc_world_celestial_angle(0);}
void fixture_triangle(float *values,uint color,int additive)
{int i;for(i=0;i<3;i++){mc_sky_projected[i].x=values[i*3];mc_sky_projected[i].y=values[i*3+1];mc_sky_projected[i].z=1;mc_sky_projected[i].a=values[i*3+2];}mc_sky_triangle(0,1,2,-1,color,additive);}
