/* End-to-end desktop integration regression against a user-supplied ZX80 ROM.
 * Build from package root:
 * gcc -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections \
 *     tests/rom_integration.c -o tests/rom_integration
 * Usage: tests/rom_integration zx80.rom
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define main zx_narm_app_entry
#include "../source/zx80_narm.tc"
#undef main
static int blits, cursor_x, cursor_y, pixel_calls;
int narm_half_clear(unsigned color){return color==255?0:-1;}
int narm_half_blit(int x,int y,int width,int height,const unsigned*data){
 if(width!=8||height!=8||!data){fprintf(stderr,"bad blit\n");exit(1);}
 blits++;cursor_x=x;cursor_y=y;return 0;
}
int narm_half_pixel(unsigned i,unsigned c){(void)i;(void)c;pixel_calls++;return 0;}
int narm_half_fill_rect(int a,int b,int c,int d,unsigned color){(void)a;(void)b;(void)c;(void)d;(void)color;return 0;}
int narm_key_down(unsigned key){(void)key;return 0;}
int narm_half_present(void){return 0;}
static void frame(void){int budget=65000;while(budget>0)budget-=z80_step();}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"ROM test FAIL at line %d: %s. PC=%04X DFILE=%04X chars=%d\n",__LINE__,#x,zpc,rw(0x400c),blits);exit(1);}}while(0)
int main(int argc,char**argv){
 if(argc<2){fprintf(stderr,"Usage: %s /path/to/zx80.rom\n",argv[0]);return 2;}
 FILE*f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 2;}
 CHECK(fread(zx_rom,1,4096,f)==4096);CHECK(fgetc(f)==EOF);fclose(f);
 CHECK(crc32_rom()==0x4c7fc597u);
 for(int i=0;i<8;i++)zx_keys[i]=31;
 zx_ram_bytes=1024;clear_zx_ram();build_zx80_glyphs();z80_reset();
 for(int i=0;i<20;i++)frame();
 CHECK(rw(0x400c)>=0x4000 && rw(0x400c)<0x4400);
 CHECK(rb(rw(0x400c))==0x76);
 blits=0;render_screen();
 CHECK(zx_has_display);CHECK(zx_rendered_chars==1);CHECK(blits==1);
 CHECK(cursor_x==32&&cursor_y==208);
 /* Alternate pixel-by-pixel renderer bypasses firmware blit entirely. */
 zx_pixel_mode=1;pixel_calls=0;blits=0;render_screen();
 CHECK(zx_rendered_chars==1);CHECK(pixel_calls==64);CHECK(blits==0);
 zx_pixel_mode=0;
 /* ZX80 starts with the inverse 'K' cursor in the bottom-left corner. */
 unsigned df=rw(0x400c);int pos=df+1;
 for(int i=0;i<23;i++){CHECK(rb(pos)==0x76);pos++;}
 CHECK(rb(pos)==0xb0);CHECK(rb(pos+1)==0x76);
 /* Holding the A key in K (keyword) mode inserts the LIST keyword. */
 zx_keys[1]&=~1u;for(int i=0;i<8;i++)frame();zx_keys[1]=31;
 blits=0;render_screen();
 CHECK(zx_rendered_chars>=4);CHECK(rb(rw(0x400c)+24)==0x31); /* L */
 CHECK(zx_djnz_seen>=22);CHECK(zx_djnz_taken>=21);CHECK(zx_djnz_wrong==0);
 run_selftest();
 CHECK(zx_test_ram_bad==0);CHECK(zx_test_djnz_bad==0);CHECK(zx_test_djnz_halt==0);
 CHECK(crc32_rom()==0x4c7fc597u);
 for(int i=0;i<20;i++)frame();
 CHECK(rb(rw(0x400c))==0x76);
 printf("ZX80 real-ROM boot + inverse-K cursor + LIST keyboard + selftest: PASS\n");
 printf("DFILE=%04X, displayed chars=%d, IRQ=%d, PC=%04X\n",rw(0x400c),zx_rendered_chars,zirq_counter,zpc);
 return 0;
}
