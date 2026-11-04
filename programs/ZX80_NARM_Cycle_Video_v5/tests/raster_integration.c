#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../source/z80_cpu.tc"
#define ASSERT(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s, fields=%d irq=%d fetch=%d vchars=%d lock=%d pc=%04x t=%u\n",__LINE__,#x,zx_vframes,zx_vlast_irqs,zx_vlast_fetches,zx_vlast_chars,zx_video_raster_locked(),zpc,zx_clock_ticks);return 1;}}while(0)
static void run(int frames){for(int f=0;f<frames;f++){int budget=65000;while(budget>0){int v=z80_step();if(v<=0){fprintf(stderr,"zero ticks!\n");exit(3);}budget-=v;}}}
static int dark_pixels(void){int n=0;for(int i=0;i<ZX_VWORDS;i++)if(zx_vpixels[zx_vready][i]!=0xffffffffu)n++;return n;}
static void write_ppm(const char*filename){FILE*f=fopen(filename,"wb");if(!f)return;fprintf(f,"P6\n256 192\n255\n");for(int y=0;y<192;y++)for(int x=0;x<256;x++){unsigned word=zx_vpixels[zx_vready][y*64+x/4];int c=(word>>((x&3)*8))&255;unsigned char p[3]={c,c,c};fwrite(p,1,3,f);}fclose(f);}
int main(int argc,char**argv){if(argc<2){fprintf(stderr,"provide 4096-byte ROM path\n");return 2;}FILE*f=fopen(argv[1],"rb");ASSERT(f!=NULL);ASSERT(fread(zx_rom,1,4096,f)==4096);fclose(f);
 for(int i=0;i<8;i++)zx_keys[i]=31;
 z80_reset();run(20);ASSERT(zx_vvalid);ASSERT(zx_vframes>=5);ASSERT(zx_video_raster_locked());
 ASSERT(zx_vlast_irqs>=295 && zx_vlast_irqs<=330);int before=dark_pixels();fprintf(stderr,"START: frames=%d IRQs=%d M1=%d chars=%d darkwords=%d total=%u PC=%04x\n",zx_vframes,zx_vlast_irqs,zx_vlast_fetches,zx_vlast_chars,before,zx_clock_ticks,zpc);
 ASSERT(before>1 && before<250);write_ppm("raster_start.ppm");
 zx_keys[1]&=~1u;run(6);zx_keys[1]|=1u;run(10);ASSERT(zx_vvalid);ASSERT(zx_video_raster_locked());int after=dark_pixels();fprintf(stderr,"TYPED A: frames=%d chars=%d darkwords=%d\n",zx_vframes,zx_vlast_chars,after);ASSERT(after>=before);write_ppm("raster_A.ppm");
 z80_reset();zx_rom[0]=0xf3;zx_rom[1]=0x18;zx_rom[2]=0xfe;run(5);ASSERT(!zx_video_raster_locked());printf("Raster integration PASS\n");return 0;}
