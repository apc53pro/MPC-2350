/* Actual menu FontRenderer/GuiButton + options/pause draw functions, with
 * headless 2D NARM services. Depth is deliberately live and never cleared by
 * these services, matching the firmware's depth-independent 2D writes. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "narm.tch"
typedef unsigned int uint;
#define MC_SCREEN_WIDTH 320
#define MC_SCREEN_HEIGHT 240
#define MC_COLOR_BLACK 0
#define MC_COLOR_WHITE 255
#define MC_COLOR_DARK 36
struct MCJavaRandom {uint unused;};
struct MCU64 {uint lo,hi;};
unsigned char fixture_pixels[320*240];
uint fixture_depth[320*240];
int fixture_text_enabled=1,fixture_text_pixels,fixture_held[256];
int mc_player_status;
uint mc_asset_pixels[19200],mc_asset_loaded_width,mc_asset_loaded_height;
uint fixture_clock;
int narm_half_clear(uint c){memset(fixture_pixels,c,sizeof(fixture_pixels));return 0;}
int narm_half_pixel(uint index,uint c)
{fixture_text_pixels++;if(fixture_text_enabled&&index<sizeof(fixture_pixels))fixture_pixels[index]=(unsigned char)c;return 0;}
int narm_half_glyph(int x,int y,int w,int h,const uint *mask,uint color)
{int r,c;for(r=0;r<h;r++)for(c=0;c<w;c++)if(mask[(r*w+c)>>5]&((uint)1<<(31-((r*w+c)&31))))
 if(x+c>=0&&x+c<320&&y+r>=0&&y+r<240)narm_half_pixel((uint)((y+r)*320+x+c),color);return 0;}
int narm_half_blit(int x,int y,int w,int h,const uint *data)
{int r,c,stride=(w+3)&~3;const unsigned char *p=(const unsigned char*)data;for(r=0;r<h;r++)for(c=0;c<w;c++)
 if(x+c>=0&&x+c<320&&y+r>=0&&y+r<240)fixture_pixels[(y+r)*320+x+c]=p[r*stride+c];return 0;}
int narm_half_present(void){return 0;}
int narm_key_down(uint key){return fixture_held[key];}
uint narm_millis(void){return fixture_clock;}
int narm_sleep_ms(uint ms){fixture_clock+=ms;return 0;}
int narm_next_key_event(uint *k,uint *a,uint *m,uint *c){return 0;}
int narm_poll_system_event(void){return 0;}
int narm_file_exists(const char *p,uint s){return 0;}
int narm_file_delete(const char *p,uint s){return 0;}
int narm_file_create(const char *p,uint s){return 1;}
int narm_file_size(const char *p,uint s,uint *z){*z=0;return -1;}
int narm_file_read(const char *p,void *d,uint o,uint n,uint s){return -1;}
int narm_file_write(const char *p,const void *d,uint o,uint n,uint s){return (int)n;}
int mc_asset_read_payload(char *p,void *d,uint c,uint *b){*b=0;return 0;}
int mc_asset_load_image(char *p){return 0;}
void mc_world_append(char *d,int cap,char *s)
{int n=(int)strlen(d),i=0;while(s[i]&&n<cap-1)d[n++]=s[i++];d[n]=0;}
void mc_world_append_int(char *d,int cap,int value)
{char text[32];snprintf(text,sizeof(text),"%d",value);mc_world_append(d,cap,text);}
#include "menu_ui_under_test.tc"
/* Pointer shape is orthogonal to text; keep it out of the text oracle. */
void mc_menu_draw_pointer(void){}
#include "../src/mc10_paths.tc"
#include "options_ui_under_test.tc"
#include "pause_ui_under_test.tc"
void fixture_reset(int text_enabled)
{int i;fixture_text_enabled=text_enabled;fixture_text_pixels=0;for(i=0;i<320*240;i++)
 {fixture_pixels[i]=(unsigned char)((i*13+i/320)&255);fixture_depth[i]=0x01000000u+(uint)i;}}
void fixture_draw_options(int screen,int text_enabled)
{fixture_reset(text_enabled);mc_options_build_screen(screen);mc_options_draw();}
void fixture_draw_pause(int text_enabled)
{fixture_reset(text_enabled);mc_game_build_pause();mc_game_draw_pause();}
char *fixture_button_text(int index){return mc_options_buttons[index].display_string;}
int fixture_button_word(int index,int word)
{struct MCMenuButton *b=&mc_options_buttons[index];if(word==0)return b->x_position;if(word==1)return b->y_position;
 if(word==2)return b->width;if(word==3)return b->enabled;return mc_options_button_kind[index];}
