/* Tests the real NARM display-file renderer with dummy firmware graphics. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define main zx_app_main
#include "../source/zx80_narm.tc"
#undef main
static int draws, last_x,last_y,clear_color,last_key,stored_first_word;
int narm_half_clear(unsigned color){clear_color=(int)color;return 0;}
int narm_half_blit(int x,int y,int w,int h,const unsigned* t){draws++;last_x=x;last_y=y;if(w!=8||h!=8)abort();stored_first_word=t[0];return 0;}
int narm_key_down(unsigned k){return k==(unsigned)last_key;}
int narm_half_pixel(unsigned i,unsigned c){(void)i;(void)c;return 0;}
int narm_half_fill_rect(int a,int b,int c,int d,unsigned col){(void)a;(void)b;(void)c;(void)d;(void)col;return 0;}
int narm_half_present(void){return 0;}
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
int main(void){
 memset(zx_rom,0,sizeof(zx_rom));
 memset(zx_ram,0,sizeof(zx_ram));
 zx_ram_bytes=1024;
 zx_rom[0xe08]=0x80; /* first pixel of glyph 1 black */
 build_zx80_glyphs();
 CHECK(zx_glyphs[1][0]==0xffffff00u);
 CHECK(zx_glyphs[65][0]==0x000000ffu);
 ww(0x400c,0x4050);wb(0x4050,0x76);wb(0x4051,1);wb(0x4052,0x76);
 for(int row=1;row<24;row++)wb(0x4052+row,0x76);
 render_screen();
 CHECK(clear_color==255);CHECK(draws==1);CHECK(last_x==32);CHECK(last_y==24);
 last_key=NARM_KEY_A;update_keyboard();CHECK((in_port(0xfdfe)&1)==0);CHECK((in_port(0xfefe)&1)==1);
 last_key=NARM_KEY_LEFT;update_keyboard();CHECK((zx_keys[0]&1)==0);CHECK((zx_keys[3]&(1<<4))==0);
 puts("ZX80 NARM display and keyboard tests: PASS");
 return 0;
}
