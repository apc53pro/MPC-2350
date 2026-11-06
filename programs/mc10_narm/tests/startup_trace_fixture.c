/* Diagnostic I/O tests only: no Minecraft/firmware behavior is stubbed into
 * the production loader. The host files below are in-memory test storage. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "narm.tch"
typedef unsigned int uint;
#define MC10_STARTUP_DIAGNOSTIC 1
#define MC_COLOR_BLACK 0
#define FIXTURE_PROFILES "/Games/Diagnostics/profiles"
static char files[8][32768];
static uint sizes[8];
static int exists[8],creates,max_write,write_calls,read_calls,screen_calls;
static int size_error,corrupt,short_read,zero_write,all_present,reported_size_delta;
void mc_world_copy(char *out,int cap,char *s){snprintf(out,cap,"%s",s);}
void mc_world_append(char *out,int cap,char *s){snprintf(out+strlen(out),cap-strlen(out),"%s",s);}
void mc_world_append_char(char *out,int cap,char c){int n=(int)strlen(out);if(n+1<cap){out[n]=c;out[n+1]=0;}}
void mc_world_append_uint(char *out,int cap,uint v){snprintf(out+strlen(out),cap-strlen(out),"%u",v);}
void mc_world_append_int(char *out,int cap,int v){snprintf(out+strlen(out),cap-strlen(out),"%d",v);}
int mc_world_strlen(char *s){return (int)strlen(s);}
static int id(const char *p,uint storage){int n=0;assert(storage==NARM_STORAGE_FLASH&&sscanf(p,FIXTURE_PROFILES "/s16_%4d.log",&n)==1);return n;}
int narm_storage_mount(uint s){assert(s==NARM_STORAGE_FLASH);return 1;}
int narm_dir_make(const char *p,uint s){assert(s==NARM_STORAGE_FLASH&&!strcmp(p,FIXTURE_PROFILES));return 1;}
int narm_file_exists(const char *p,uint s){int n=id(p,s);return all_present||(n<8&&exists[n]);}
int narm_file_size(const char *p,uint s,uint *out){
 int n=id(p,s);if(size_error)return -5;
 if(all_present){*out=1;return 0;}
 if(n>=8||!exists[n])return -3;*out=sizes[n]+reported_size_delta;return 0;
}
int narm_file_create(const char *p,uint s){int n=id(p,s);assert(n>0&&n<8&&!exists[n]);exists[n]=1;creates++;sizes[n]=0;return 1;}
int narm_file_write(const char *p,const void *data,uint off,uint n,uint s){
 int k=id(p,s);write_calls++;if(zero_write)return 0;
 if(n>(uint)max_write)n=(uint)max_write;
 assert(k<8&&exists[k]&&off+n<sizeof(files[k]));memcpy(files[k]+off,data,n);
 if(sizes[k]<off+n)sizes[k]=off+n;files[k][sizes[k]]=0;return (int)n;
}
int narm_file_read(const char *p,void *out,uint off,uint n,uint s){
 int k=id(p,s);read_calls++;assert(off+n<=sizes[k]);memcpy(out,files[k]+off,n);
 if(corrupt&&n)((char *)out)[0]^=1;return short_read?(int)n-1:(int)n;
}
int narm_half_fill_rect(int x,int y,int x1,int y1,uint c){screen_calls++;return 0;}
int narm_half_present(void){screen_calls++;return 0;}
void mc_menu_draw_centered_string(char *s,int x,int y,uint c){screen_calls++;}
#include "../src/mc10_paths.tc"
#include "../src/mc10_startup_trace.tc"
static void reset(void){
 strcpy(mc_game_profiles_path,FIXTURE_PROFILES);mc_game_storage=NARM_STORAGE_FLASH;
 memset(files,0,sizeof(files));memset(sizes,0,sizeof(sizes));memset(exists,0,sizeof(exists));
 creates=write_calls=read_calls=screen_calls=size_error=corrupt=short_read=zero_write=all_present=reported_size_delta=0;
 max_write=7;exists[1]=1;strcpy(files[1],"previous run must survive");sizes[1]=(uint)strlen(files[1]);
}
int main(void){
 int calls;
 reset();assert(mc_startup_trace_begin());assert(creates==1&&!strcmp(mc_startup_path,FIXTURE_PROFILES "/s16_0002.log"));
 assert(!strcmp(files[1],"previous run must survive")&&screen_calls==0);
 assert(write_calls>3&&read_calls==3&&mc_startup_offset==sizes[2]);
 assert(strstr(files[2],"sequence,chunk_1based,x,z,status_or_bytes,stage\n"));
 mc_startup_chunk=8;mc_startup_x=-10;mc_startup_z=22;
 assert(mc_startup_checkpoint("payload_read_begin",4543));
 assert(strstr(files[2],"1,8,-10,22,4543,payload_read_begin\n"));
 assert(mc_startup_sequence==1&&screen_calls==0);
 mc_startup_screen_ready=1;assert(mc_startup_checkpoint("inflate_done",0));assert(screen_calls>0);
 zero_write=1;assert(!mc_startup_checkpoint("must_fail",0)&&mc_startup_error==1);
 calls=write_calls;assert(!mc_startup_checkpoint("must_not_continue",0)&&write_calls==calls);
 reset();corrupt=1;assert(!mc_startup_trace_begin()&&mc_startup_error==4);
 reset();short_read=1;assert(!mc_startup_trace_begin()&&mc_startup_error==3);
 reset();size_error=1;assert(!mc_startup_trace_begin()&&creates==0);
 reset();all_present=1;assert(!mc_startup_trace_begin()&&creates==0);
 reset();assert(mc_startup_trace_begin());reported_size_delta=1;assert(!mc_startup_checkpoint("wrong_size",0)&&mc_startup_error==2);
 puts("Startup trace: PASS unique non-overwriting paths, checked short writes, size/readback, errors stop, chunk coordinates, sequencing, no UI before font initialization");
 return 0;
}
