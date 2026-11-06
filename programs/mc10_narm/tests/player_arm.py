"""Execute source-oracle trajectories as actual NARM Thumb instructions.

This is an isolated CPU test, not the GP-PICO emulator. Requires unicorn.
The normal host suite can run without it; use test_player.py --arm-compiler.
"""
import math,struct,subprocess
from pathlib import Path

def execute(path):
 from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
 from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPEXC
 image=path.read_bytes();u32=lambda o:struct.unpack_from('<I',image,o)[0]
 align=lambda n,a:(n+a-1)&~(a-1)
 text,arena,ctx,stack,stop=0x100000,0x2000000,0x3000000,0x4000000,0x3001000
 uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB)
 uc.mem_map(text,align(max(u32(0x20),4096),4096));uc.mem_map(arena,0x800000);uc.mem_map(ctx,0x2000);uc.mem_map(stack,0x10000)
 uc.mem_write(text,image[u32(0x1c):u32(0x1c)+u32(0x20)])
 size=u32(0x2c);data=align(size,u32(0x3c))
 if size:uc.mem_write(arena,image[u32(0x28):u32(0x28)+size])
 if u32(0x38):uc.mem_write(arena+data,image[u32(0x34):u32(0x34)+u32(0x38)])
 for i in range(u32(0x54)):
  patch,target,kind,_=struct.unpack_from('<IIHH',image,u32(0x50)+12*i)
  if not kind:continue
  value=arena+target if kind==1 else (text+target)|(1 if kind==3 else 0) if kind in (2,3) else ctx
  uc.mem_write(arena+patch,struct.pack('<I',value))
 uc.mem_write(stop,b'\x70\x47');uc.reg_write(UC_ARM_REG_R9,arena);uc.reg_write(UC_ARM_REG_R10,ctx)
 imports=[struct.unpack_from('<I',image,u32(0x58)+i*8)[0] for i in range(u32(0x5c))]
 if 0x12 in imports:
  from native_numeric_probe import install
  install(uc,ctx)
 uc.reg_write(UC_ARM_REG_SP,stack+0xf000);uc.reg_write(UC_ARM_REG_LR,stop|1);uc.reg_write(UC_ARM_REG_FPEXC,1<<30)
 uc.hook_add(UC_HOOK_CODE,lambda u,a,s,d:u.emu_stop(),begin=stop,end=stop)
 uc.emu_start((text+u32(0x18))|1,stop,count=100000000)
 assert uc.reg_read(UC_ARM_REG_PC)==stop,'ARM fixture exhausted instruction budget'
 return uc.reg_read(UC_ARM_REG_R0)

def run_case(project,tmp,compiler,name,blocks,metadata,pos,yaw,controls,oracle):
 f32=lambda v:struct.unpack('<f',struct.pack('<f',v))[0]
 bits=lambda v:struct.unpack('<I',struct.pack('<f',v))[0]
 words=[]
 for row in oracle[:8]:
  for value in row[:12]:words.extend([value&0xffffffff,value>>32])
  words.extend(row[12:])
 code=['#include "player_fixture.c"','uint expected[]={'+','.join(hex(v)for v in words)+'};',
       'int main(void){int x,z,t,i;uint *state;']
 code.append('for(x=0;x<48;x=x+1)for(z=0;z<48;z=z+1)fixture_blocks[(x*48+z)*128+63]=1;')
 for i,(block,meta)in enumerate(zip(blocks,metadata)):
  baseline=1 if i%128==63 else 0
  if block!=baseline:code.append(f'fixture_blocks[{i}]={block};')
  if meta:code.append(f'fixture_metadata[{i}]={meta};')
 table=(project/'MC10/math10.dat').read_bytes()[16:];indices=set()
 normalized=math.fmod(yaw,360.0)
 for a in [f32(f32(normalized*f32(3.141592741))/180),f32(normalized*f32(f32(3.141592741)/180))]:
  indices.update([int(f32(a*f32(10430.378)))&65535,int(f32(f32(a*f32(10430.378))+16384))&65535])
 for i in indices:code.append(f'mc_math_sine[{i}]=0x{struct.unpack_from("<I",table,i*4)[0]:08x};')
 for field,value in zip(['pos_x','pos_y','pos_z','motion_x','motion_y','motion_z'],[*pos,0,-.0784000015258789,0]):
  lo,hi=struct.unpack('<II',struct.pack('<d',value));code.append(f'mc_runtime_player.{field}.lo=0x{lo:08x};mc_runtime_player.{field}.hi=0x{hi:08x};')
 code.extend([f'mc_runtime_player.yaw_bits=0x{bits(yaw):08x};',
              f'mc_runtime_player.present=1;mc_runtime_player.health=20;mc_runtime_player.food=20;mc_runtime_player.mayfly={int(name=="flight")};mc_fixture_init();'])
 for tick,c in enumerate(controls[:8]):
  a,b,jump,sneak=c
  code.append(f'if(!mc_player_tick({float(a)}f,{float(b)}f,{jump},{sneak}))return {9000+tick};')
  code.append(f'for(i=0;i<12;i=i+1){{if(mc_fp_values[i].lo!=expected[{tick*28}+i*2]||mc_fp_values[i].hi!=expected[{tick*28+1}+i*2])return {tick*32+1}+i;}}')
  code.append(f'if(mc_player.grounded!=expected[{tick*28+24}]||mc_player.horizontal!=expected[{tick*28+25}])return {tick*32+25};')
  code.append(f'if(*((uint *)&mc_player.y_size)!=expected[{tick*28+26}]||*((uint *)&mc_player.fall_distance)!=expected[{tick*28+27}])return {tick*32+27};')
 code.append('return 0;}')
 source=tmp/(name+'-arm.tc');source.write_text('\n'.join(code),encoding='utf-8');output=source.with_suffix('.narm')
 result=subprocess.run([str(compiler),str(source),'-I',str(project/'tests'),'-O3','--legacy-float-literals','--stack-bytes','32768','-o',str(output)],capture_output=True,text=True)
 assert result.returncode==0,result.stderr+result.stdout
 result=execute(output);assert result==0,(name,'actual ARM trajectory mismatch',result)
 print('NARM ARM trajectory:',name,'PASS',min(8,len(controls)),'ticks',flush=True)
