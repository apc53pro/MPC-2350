#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "narm.tch"
typedef unsigned int uint;
#define MC10_PLAYER_FOUNDATION
#define MC10_LEGACY_FONT
#define MC_SCREEN_WIDTH 320
#define MC_SCREEN_HEIGHT 240
#define MC_COLOR_BLACK 0
struct MCU64 {uint lo,hi;};
unsigned char fixture_pixels[76800];
uint fixture_depth[76800];
unsigned char *fixture_pack;uint fixture_pack_size;
int fixture_writes,fixture_held[256],fixture_event_key,fixture_event_action;
int fixture_script_keys[32],fixture_script_actions[32],fixture_script_count,fixture_script_position;
uint fixture_clock;
void fixture_set_pack(unsigned char *data,uint size){fixture_pack=data;fixture_pack_size=size;}
int narm_storage_mount(uint s){return 1;}
int narm_file_exists(const char *p,uint s){return strstr(p,"assets.mc10")!=0;}
int narm_file_size(const char *p,uint s,uint *size){if(strstr(p,"assets.mc10")){*size=fixture_pack_size;return 0;}*size=0;return -1;}
int narm_file_read(const char *p,void *out,uint offset,uint size,uint s)
{if(!strstr(p,"assets.mc10")||offset>fixture_pack_size||size>fixture_pack_size-offset)return -1;memcpy(out,fixture_pack+offset,size);return (int)size;}
int narm_file_create(const char *p,uint s){fixture_writes++;return -1;}
int narm_file_delete(const char *p,uint s){fixture_writes++;return -1;}
int narm_file_rename(const char *p,const char *q,uint s){fixture_writes++;return -1;}
int narm_file_write(const char *p,const void *out,uint offset,uint size,uint s){fixture_writes++;return -1;}
int narm_yield(void){return 0;}
int narm_half_clear(uint color){memset(fixture_pixels,(unsigned char)color,sizeof(fixture_pixels));return 0;}
int narm_half_pixel(uint index,uint color){if(index<76800)fixture_pixels[index]=(unsigned char)color;return 0;}
int narm_half_fill_rect(int x0,int y0,int x1,int y1,uint color)
{int x,y;for(y=y0;y<=y1;y++)for(x=x0;x<=x1;x++)if(x>=0&&x<320&&y>=0&&y<240)fixture_pixels[y*320+x]=(unsigned char)color;return 0;}
int narm_half_blit(int x,int y,int w,int h,const uint *data)
{int r,c;const unsigned char *p=(const unsigned char*)data;for(r=0;r<h;r++)for(c=0;c<w;c++)if(x+c>=0&&x+c<320&&y+r>=0&&y+r<240)fixture_pixels[(y+r)*320+x+c]=p[r*((w+3)&~3)+c];return 0;}
int narm_half_blit_masked(int x,int y,int w,int h,const uint *data,const uint *mask)
{int r,c,bit;const unsigned char *p=(const unsigned char*)data;for(r=0;r<h;r++)for(c=0;c<w;c++){bit=r*w+c;if(x+c>=0&&x+c<320&&y+r>=0&&y+r<240&&(mask[bit>>5]&((uint)1<<(31-(bit&31)))))fixture_pixels[(y+r)*320+x+c]=p[r*((w+3)&~3)+c];}return 0;}
int narm_half_line(int x0,int y0,int x1,int y1,uint color)
{int steps=x0==x1?y1-y0:x1-x0,i;if(steps<0)steps=-steps;for(i=0;i<=steps;i++)narm_half_pixel((uint)((y0+(steps?(y1-y0)*i/steps:0))*320+x0+(steps?(x1-x0)*i/steps:0)),color);return 0;}
int narm_half_present(void){return 0;}
int narm_key_down(uint key){return fixture_held[key];}
uint narm_millis(void){return fixture_clock;}
int narm_sleep_ms(uint ms)
{fixture_clock+=ms;if(fixture_script_position<fixture_script_count){fixture_event_key=fixture_script_keys[fixture_script_position];fixture_event_action=fixture_script_actions[fixture_script_position++];fixture_held[fixture_event_key]=fixture_event_action!=NARM_KEY_ACTION_RELEASE;}return 0;}
int narm_next_key_event(uint *k,uint *a,uint *m,uint *c)
{if(!fixture_event_key)return 0;*k=(uint)fixture_event_key;*a=(uint)fixture_event_action;*m=0;*c=0;fixture_event_key=0;return 1;}
int narm_poll_system_event(void){return fixture_clock>2000?NARM_EVENT_CLOSE_REQUESTED:0;}
void mc_world_append(char *d,int cap,char *s){int n=(int)strlen(d),i=0;while(s[i]&&n<cap-1)d[n++]=s[i++];d[n]=0;}
void mc_world_append_int(char *d,int cap,int value){char text[32];snprintf(text,sizeof(text),"%d",value);mc_world_append(d,cap,text);}
#include "nbt_under_test.tc"
#include "random_under_test.tc"
#include "assets_under_test.tc"
#include "menu_under_test.tc"
struct MCFixtureOptions {int key_codes[13];};
struct MCFixtureOptions mc_options;
void mc_options_draw_background(void){narm_half_clear(36);}
#include "key_map_under_test.tc"
void mc_menu_draw_pointer(void){}
void mc_ui_draw_status(char *s){}
void mc_wait_for_return(void){}
#include "items_under_test.tc"
#include "inventory_under_test.tc"
#include "player_ui_under_test.tc"
int fixture_load_player(unsigned char *data,uint size)
{int root,player;int result=mc_nbt_parse(data,size,&root);if(result<0)return result;player=mc_nbt_compound_find(root,"Player");if(player<0)player=root;return mc_inventory_load_player_nbt(player);}
void fixture_reset_pixels(void)
{int i;for(i=0;i<76800;i++){fixture_pixels[i]=(unsigned char)((i*13+i/320)&255);fixture_depth[i]=0x01000000u+(uint)i;}}
void fixture_script_reset(void)
{memset(fixture_held,0,sizeof(fixture_held));fixture_event_key=0;fixture_clock=0;fixture_script_position=0;mc_options.key_codes[9]=18;}
