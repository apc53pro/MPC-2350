#include <stdint.h>
typedef unsigned int uint;
#define MC10_WORLD_SYSTEMS 1
#define NARM_STORAGE_SD 0
int narm_file_read(char *name,void *data,int offset,int size,int storage){return -1;}
#include "../src/mc10_u64.tc"
#include "../src/mc10_java_double.tc"
#include "../src/mc10_fp64.tc"
#include "../src/mc10_paths.tc"
#include "../src/mc10_math.tc"
struct ClockInfo {uint world_time_lo,world_time_hi;int raining,thundering;};
struct ClockInfo clock_info;
struct {struct ClockInfo *info;} mc_runtime_world;
#include "../src/mc10_cos_celestial.tc"
#include "../src/mc10_world_clock.tc"
void clock_set(uint lo,uint hi,float rain,float thunder)
{mc_runtime_world.info=&clock_info;clock_info.world_time_lo=lo;clock_info.world_time_hi=hi;mc_world_rain_strength=rain;mc_world_thunder_strength=thunder;mc_fp_init();}
uint clock_low(void){return clock_info.world_time_lo;}
uint clock_high(void){return clock_info.world_time_hi;}
uint clock_cos_low(uint lo,uint hi)
{mc_fp_reset();int value=mc_fp_cos_celestial(mc_fp_bits(lo,hi));return mc_fp_values[value].lo;}
uint clock_cos_high(void){return mc_fp_values[mc_fp_next-1].hi;}
