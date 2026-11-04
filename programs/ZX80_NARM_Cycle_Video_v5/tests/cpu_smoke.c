/* Host-only checks of the exact Z80 source compiled for NARM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../source/z80_cpu.tc"
static int pass=0;
#define CHECK(expr) do {if(!(expr)){fprintf(stderr,"FAIL at %s:%d (%s) A=%02X F=%02X BC=%04X HL=%04X PC=%04X\n",__FILE__,__LINE__,#expr,za,zf,(unsigned)bc(),(unsigned)hl(),zpc);exit(1);}else pass++;}while(0)
static void program(const unsigned char *bytes,int count){memset(zx_rom,0,sizeof(zx_rom));memcpy(zx_rom,bytes,count);memset(zx_ram,0,sizeof(zx_ram));zx_ram_bytes=1024;z80_reset();}
static void steps(int n){while(n--)z80_step();}
int main(void){
 {const unsigned char p[]={0x3e,0x7f,0xc6,0x01,0x32,0x00,0x40,0x76};program(p,sizeof(p));steps(3);CHECK(za==128);CHECK((zf&(F_P|F_S))==(F_P|F_S));CHECK(rb(0x4000)==128);CHECK(rb(0xc000)==128);CHECK(rb(0x4400)==128);}
 {const unsigned char p[]={0x06,0x03,0x10,0xfe,0x3e,0x55};program(p,sizeof(p));steps(4);CHECK(zb==0);CHECK(zpc==4);steps(1);CHECK(za==0x55);}
 {const unsigned char p[]={0x21,0x00,0x40,0x11,0x10,0x40,0x01,0x03,0x00,0xed,0xb0,0x76};program(p,sizeof(p));wb(0x4000,10);wb(0x4001,20);wb(0x4002,30);steps(6);CHECK(rb(0x4010)==10);CHECK(rb(0x4011)==20);CHECK(rb(0x4012)==30);CHECK(bc()==0);CHECK(hl()==0x4003);}
 {const unsigned char p[]={0xdd,0x21,0x00,0x40,0xdd,0x36,0x05,0x42,0xdd,0x7e,0x05,0xdd,0xcb,0x05,0xc6,0x76};program(p,sizeof(p));steps(3);CHECK(zix==0x4000);CHECK(rb(0x4005)==0x42);CHECK(za==0x42);steps(1);CHECK(rb(0x4005)==0x43);}
 {const unsigned char p[]={0x3e,0xff,0xcb,0x7f,0xcb,0x3f,0xcb,0xc7,0x76};program(p,sizeof(p));steps(2);CHECK(!(zf&F_Z));steps(1);CHECK(za==127);CHECK(zf&F_C);steps(1);CHECK(za==127);}
 {const unsigned char p[]={0x21,0x01,0x00,0x37,0xed,0x42,0xed,0x4a,0x76};program(p,sizeof(p));steps(3);CHECK(hl()==0);CHECK(zf&F_N);steps(1);CHECK(hl()==0);CHECK(!(zf&F_N));}
 {const unsigned char p[]={0xfb,0x76,0x00,0x00};program(p,sizeof(p));zr=0xfe;z80_step();CHECK(zpc==1);z80_step();CHECK(zhalt==0);CHECK(zpc==0x38);CHECK(zirq_counter==1);}
 {const unsigned char p[]={0x3e,0x81,0xed,0x44,0x76};program(p,sizeof(p));steps(2);CHECK(za==0x7f);CHECK(zf&F_N);}
 puts("ZX80 Z80 smoke tests: PASS");printf("Checks: %d\n",pass);return 0;
}
