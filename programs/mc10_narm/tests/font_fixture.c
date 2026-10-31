#include <stdint.h>
#include <string.h>
#include "narm.tch"
typedef unsigned int uint;
#define MC_SCREEN_WIDTH 320
#define MC_SCREEN_HEIGHT 240
#define MC_COLOR_BLACK 0
struct MCJavaRandom {uint unused;};
struct MCU64 {uint lo,hi;};
unsigned char fixture_pixels[320*240];
int fixture_native_calls,fixture_pixel_calls,fixture_glyph_error;
uint fixture_pixel_indices[32768],fixture_pixel_colors[32768];
int narm_half_clear(uint c){memset(fixture_pixels,(unsigned char)c,sizeof(fixture_pixels));return 0;}
int narm_half_pixel(uint index,uint c)
{if(fixture_pixel_calls<32768){fixture_pixel_indices[fixture_pixel_calls]=index;fixture_pixel_colors[fixture_pixel_calls]=c;}fixture_pixel_calls++;if(index<sizeof(fixture_pixels))fixture_pixels[index]=(unsigned char)c;return 0;}
int narm_half_glyph(int x,int y,int width,int height,const uint *mask,uint color)
{
 int row,column,index;fixture_native_calls++;
 if(fixture_glyph_error)return -1;
 for(row=0;row<height;row++)for(column=0;column<width;column++)
 {
  index=row*width+column;
  if(x+column>=0&&x+column<320&&y+row>=0&&y+row<240&&
     (mask[index>>5]&((uint)1<<(31-(index&31)))))fixture_pixels[(y+row)*320+x+column]=(unsigned char)color;
 }
 return 0;
}
#include "menu_text_under_test.tc"
void fixture_font_reset(void)
{int i;for(i=0;i<320*240;i++)fixture_pixels[i]=(unsigned char)((i*13+i/320)&255);fixture_native_calls=0;fixture_pixel_calls=0;}
