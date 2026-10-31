"""Source-validated generation primitives executed as actual NARM Thumb.

Isolated CPU only; no RP2350, firmware service, game emulator or save writes.
Run the two Java-oracle host tests first, to produce their validated fixtures.
"""
from pathlib import Path
import argparse,ctypes as C,json,os,struct,subprocess,sys
from test_biome import GAME
from test_generation_noise import Pair,pair
sys.path.insert(0,'C:/Users/enzob/Documents/Codex/2026-10-13/co/work/arm_tools')
sys.path.insert(0,str(GAME/'tests'))
def execute(path,math_bytes,ravine_bytes,cave_bytes):
 from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
 from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPEXC
 image=path.read_bytes();u32=lambda o:struct.unpack_from('<I',image,o)[0];align=lambda n,a:(n+a-1)&~(a-1)
 text,arena,ctx,stack,stop=0x100000,0x2000000,0x3000000,0x4000000,0x3001000
 uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB);uc.mem_map(text,align(max(u32(0x20),4096),4096))
 uc.mem_map(arena,0x800000);uc.mem_map(ctx,0x2000);uc.mem_map(stack,0x10000)
 uc.mem_write(text,image[u32(0x1c):u32(0x1c)+u32(0x20)]);size=u32(0x2c);data=align(size,u32(0x3c))
 if size:uc.mem_write(arena,image[u32(0x28):u32(0x28)+size])
 if u32(0x38):uc.mem_write(arena+data,image[u32(0x34):u32(0x34)+u32(0x38)])
 for i in range(u32(0x54)):
  patch,target,kind,_=struct.unpack_from('<IIHH',image,u32(0x50)+12*i)
  if not kind:continue
  value=arena+target if kind==1 else (text+target)|(kind==3) if kind in(2,3) else ctx
  uc.mem_write(arena+patch,struct.pack('<I',value))
 uc.mem_write(stop,b'\x70\x47');uc.mem_write(stack,b'\x5a'*64)
 uc.reg_write(UC_ARM_REG_R9,arena);uc.reg_write(UC_ARM_REG_R10,ctx);uc.reg_write(UC_ARM_REG_SP,stack+32752)
 uc.reg_write(UC_ARM_REG_LR,stop|1);uc.reg_write(UC_ARM_REG_FPEXC,1<<30)
 uc.hook_add(UC_HOOK_CODE,lambda u,a,s,d:u.emu_stop(),begin=stop,end=stop)
 uc.emu_start((text+u32(0x18))|1,stop,count=100000)
 packet=uc.reg_read(UC_ARM_REG_R0);table,expected,cave_expected=struct.unpack('<III',bytes(uc.mem_read(packet,12)))
 uc.mem_write(table,math_bytes);uc.mem_write(expected,ravine_bytes);uc.mem_write(cave_expected,cave_bytes)
 uc.reg_write(UC_ARM_REG_SP,stack+32752);uc.reg_write(UC_ARM_REG_LR,stop|1)
 uc.emu_start((text+u32(0x18))|1,stop,count=1500000000)
 if uc.reg_read(UC_ARM_REG_PC)!=stop:
  trace=data+image.find(struct.pack('<I',0xface0129),u32(0x34),u32(0x34)+u32(0x38))-u32(0x34)
  progress=struct.unpack('<I',bytes(uc.mem_read(arena+trace,4)))[0]
  raise AssertionError(('Thumb probe exhausted1.5B instruction budget',progress,hex(uc.reg_read(UC_ARM_REG_PC))))
 assert bytes(uc.mem_read(stack,64))==b'\x5a'*64,'32KiB stack guard damaged'
 return uc.reg_read(UC_ARM_REG_R0)

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--game',type=Path,default=GAME)
 ap.add_argument('--compiler',type=Path,default=GAME.parents[3]/'Compiler_For_NARM/narmcc.exe')
 ap.add_argument('--work',type=Path);ap.add_argument('--fixtures-root',type=Path,default=Path(__file__).parents[1]);a=ap.parse_args()
 if a.work is None:a.work=a.fixtures_root/'arm_test_work'
 work=a.work.resolve();work.mkdir(parents=True,exist_ok=True);root=a.fixtures_root.resolve()
 host=C.CDLL(str((root/'test_work/biome.dll').resolve()));host.seed.argtypes=[C.c_uint32,C.c_uint32]
 host.region.argtypes=[C.c_int]*5+[C.POINTER(C.c_int),C.c_int]
 source=['#define MC10_WORLD_SYSTEMS 1','#define MC10_GENERATION_CARVERS 1','int generation_trace=0xface0129;','int mc_biome_yield(void){return 1;}']
 for n in ['mc10_u64.tc','mc10_java_double.tc','mc10_fp64.tc','mc10_java_random.tc']:
  source.append('#include "'+(a.game/'src'/n).as_posix()+'"')
 for n in ['mc10_biome.tc','mc10_generation_noise.tc','mc10_generation.tc','mc10_generation_ravine.tc','mc10_generation_cave.tc']:
  source.append('#include "'+(a.game/'src'/n).as_posix()+'"')
 source+=['uint arm_sine[65536];int arm_boot_stage;uint arm_packet[3];char ravine_expected[32768];char cave_expected[32768];char ravine_blocks[32768];',
 'float mc_math_sin(float value){uint bits=arm_sine[(int)(value*10430.378f)&65535];return *((float *)&bits);}',
 'float mc_math_cos(float value){uint bits=arm_sine[(int)(value*10430.378f+16384.0f)&65535];return *((float *)&bits);}']
 expected=[];seeds=[0,-1,1234567890123456789]
 for seed in seeds:
  host.seed(seed&0xffffffff,(seed>>32)&0xffffffff)
  for output in range(4):
   out=(C.c_int*4)();assert host.region(output,-13,27,2,2,out,8192)==1;expected.extend(out)
 source.append('int expected_biomes[]={'+','.join(map(str,expected))+'};')
 noise=C.CDLL(str((root/'noise_test_work/noise.dll').resolve()))
 noise.noise.argtypes=[C.c_uint32,C.c_uint32]+[C.c_int]*7+[C.POINTER(Pair)]*3
 sc=(Pair*3)(pair(.0625),pair(.125),pair(.25));origins=(Pair*3)(pair(-.877),pair(-10.75),pair(-.1))
 values=(Pair*28)();assert noise.noise(0xffffffff,0xffffffff,1,-1,-10,-1,2,7,2,sc,origins,values)==0
 words=[word for v in values for word in (v.lo,v.hi)]
 source.append('uint expected_noise[]={'+','.join(hex(x) for x in words)+'};')
 source+=['int output_biomes[4];struct MCGenPerlin octave_probe[4];struct MCJDouble noise_probe[28];',
 'int main(void){struct MCU64 seed;struct MCJavaRandom random;struct MCJDouble scales[3];struct MCJDouble ratio;int i,j,s;',
 'if(!arm_boot_stage){arm_boot_stage=1;arm_packet[0]=(uint)&arm_sine[0];arm_packet[1]=(uint)&ravine_expected[0];arm_packet[2]=(uint)&cave_expected[0];return (int)&arm_packet[0];}mc_fp_init();',
 'if(mc_gen_capabilities()!=63 || mc_gen_ready_for_play()!=0)return 900;mc_u64_set(&seed,0,0);if(mc_gen_base_carved(&seed,0,0,ravine_blocks)!=0)return 901;']
 for k,seed in enumerate(seeds):
  source.append('mc_u64_set(&seed,'+hex(seed&0xffffffff)+','+hex((seed>>32)&0xffffffff)+');mc_biome_seed(&seed);')
  for output in range(4):
   source.append(f'generation_trace={100+k*10+output};if(!mc_biome_region({output},-13,27,2,2,output_biomes,mc_biome_scratch,3072))return {100+k*10+output};')
   source.append(f'for(i=0;i<4;i=i+1)if(output_biomes[i]!=expected_biomes[{k*16+output*4}+i])return {200+k*10+output};')
 source+=['mc_u64_set(&seed,0xffffffff,0xffffffff);mc_java_random_seed_u64(&random,&seed);mc_gen_octaves_init(octave_probe,4,&random);']
 for i,v in enumerate(sc):source.append(f'scales[{i}].lo={hex(v.lo)};scales[{i}].hi={hex(v.hi)};')
 source+=['generation_trace=400;mc_gen_octaves(octave_probe,4,noise_probe,-1,-10,-1,2,7,2,scales);',
 'if(mc_fp_error)return 400;for(i=0;i<28;i=i+1){if(noise_probe[i].lo!=expected_noise[i*2]||noise_probe[i].hi!=expected_noise[i*2+1])return 500+i;}',
 'for(i=0;i<32768;i=i+1)ravine_blocks[i]=1;mc_u64_set(&seed,0,0);scales[0].lo=0;scales[0].hi=0x40200000;scales[1].lo=0;scales[1].hi=0x40500000;scales[2].lo=0;scales[2].hi=0x40200000;',
 'generation_trace=600;if(!mc_gen_ravine_node(&seed,0,0,ravine_blocks,scales,3.0f,0.0f,0.0f))return 600;',
 'for(i=0;i<32768;i=i+1)if(ravine_blocks[i]!=ravine_expected[i])return 700+i;',
 'for(i=0;i<32768;i=i+1)ravine_blocks[i]=1;scales[0].lo=0;scales[0].hi=0x40200000;scales[1].lo=0;scales[1].hi=0x40500000;scales[2].lo=0;scales[2].hi=0x40200000;ratio.lo=0;ratio.hi=0x3ff00000;',
 'generation_trace=800;if(!mc_gen_cave_node(&seed,0,0,ravine_blocks,scales,3.0f,0.0f,0.0f,0,0,&ratio))return 800;',
 'for(i=0;i<32768;i=i+1)if(ravine_blocks[i]!=cave_expected[i])return 40000+i;return 0;}']
 path=work/'generation_arm.tc';path.write_text('\n'.join(source));image=path.with_suffix('.narm')
 result=subprocess.run([str(a.compiler),str(path),'-O3','--stack-bytes','32768','-o',str(image)],capture_output=True,text=True)
 assert result.returncode==0,result.stdout+result.stderr
 table=(a.game/'MC10/math10.dat').read_bytes()[16:];host_ravine=C.CDLL(str((root/'ravine_test_work/ravine.dll').resolve()))
 host_ravine.math_table(table);host_ravine.node.argtypes=[C.c_uint32,C.c_uint32,C.c_void_p]
 blocks=(C.c_ubyte*32768)();C.memset(blocks,1,32768);assert host_ravine.node(0,0,blocks)==1
 host_cave=C.CDLL(str((root/'cave_test_work/ravine.dll').resolve()));host_cave.math_table(table);host_cave.node.argtypes=[C.c_uint32,C.c_uint32,C.c_void_p]
 cave_blocks=(C.c_ubyte*32768)();C.memset(cave_blocks,1,32768);assert host_cave.node(0,0,cave_blocks)==1
 print(result.stdout.strip(),flush=True);status=execute(image,table,bytes(blocks),bytes(cave_blocks));assert status==0,('NARM Thumb mismatch',status)
 audit={'biome_climate_cells':48,'binary64_3d_noise_outputs':28,'stack_guard_bytes':32768,
   'ravine_node_block_bytes':32768,'cave_node_block_bytes':32768,'firmware_service_calls':0,'instruction_budget':1500000000,'passed':True}
 (work/'generation_arm_audit.json').write_text(json.dumps(audit,indent=2)+'\n')
 print('PASS NARM Thumb: 48 biome/climate cells, 28 exact 3D octave binary64 outputs, complete32768-byte ravine and cave node arrays; 32 KiB stack guard; no firmware service calls')
if __name__=='__main__':main()

