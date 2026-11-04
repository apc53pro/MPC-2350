/* Independent synthetic video tests: custom M1 opcodes, ROM glyph access,
   HALT/IRQ and VSYNC framing. Does not need copyrighted ZX80 ROM. */
#include <stdio.h>
#include <stdlib.h>
#include "../source/z80_cpu.tc"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"CUSTOM VIDEO FAIL at line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
 for(int i=0;i<8;i++)zx_keys[i]=31;
 z80_reset();
 /* Set a custom ROM glyph with a deliberately asymmetric bitmap. */
 zi=0x0e; zx_rom[0xe00 + (5<<3)+3]=0x89;
 zx_vline=56+3;zx_vline_3=3;zx_last_irq_clock=3000;zx_clock_ticks=3000+75;
 zx_video_m1(0xc123,0x05);
 CHECK(zx_vchars==1);
 CHECK(zx_vpixels[0][3*64]==zx_vwords[0x8]);
 CHECK(zx_vpixels[0][3*64+1]==zx_vwords[0x9]);
 /* A forced NOP only occurs in the >=0x8000 opcode mirror. */
 zx_video_m1(0x4000,0x05);
 CHECK(zx_vchars==1);
 /* A custom display loop can change glyph codes between M1 fetches. */
 zx_clock_ticks+=4;zx_video_m1(0xc124,0x85);
 CHECK(zx_vchars==2);
 CHECK(zx_vpixels[0][3*64+2]==zx_vwords[0x7]);
 CHECK(zx_vpixels[0][3*64+3]==zx_vwords[0x6]);
 /* Bit six inhibits NOP generation / character output, as for HALT. */
 zx_clock_ticks+=4;zx_video_m1(0xc125,0x76);CHECK(zx_vchars==2);
 /* Refresh (I:R) addressing for RAM-backed high-resolution bytes. */
 zi=0x40;zr=0x2d;wb(0x402d,0xf1);
 zx_clock_ticks+=4;zx_video_m1(0xc126,0x07);
 CHECK(zx_vchars==3);
 CHECK(zx_vpixels[0][3*64+6]==zx_vwords[0xf]);
 CHECK(zx_vpixels[0][3*64+7]==zx_vwords[0x1]);
 /* OUT ends VSYNC and primes a new video field; 250 acknowledged lines
    followed by a new VSYNC commits the completed frame. */
 zx_vline=250;zx_virq_count=250;
 zx_video_in();zx_video_out();CHECK(zx_vvalid);
 CHECK(zx_vframes==1);CHECK(zx_vready==0);
 CHECK(zx_vpixels[1][0]==0xffffffffu);
 CHECK(zx_vline==0);CHECK(zx_vline_3==0);
 zx_video_irq(); CHECK(zx_vline==1 && zx_vline_3==1);
 puts("Synthetic custom ZX80 raster tests: PASS");
 return 0;
}
