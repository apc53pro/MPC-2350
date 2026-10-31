"""Owned MapGenBase/MapGenRavine phase versus exact NARM-oriented carver."""
from pathlib import Path
import argparse,ctypes as C,hashlib,json,shutil
from test_biome import DEFAULT,GAME,STUB,run
from test_generation import HARNESS,method

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--oracle',type=Path,default=DEFAULT);ap.add_argument('--game',type=Path,default=GAME)
 ap.add_argument('--modules',type=Path,default=Path(__file__).parents[1]/'src');ap.add_argument('--carver',choices=['ravine','cave','carved'],default='ravine')
 ap.add_argument('--work',type=Path);ap.add_argument('--fixtures-root',type=Path,default=Path(__file__).parents[1]);a=ap.parse_args()
 if a.work is None:a.work=a.fixtures_root/(a.carver+'_test_work')
 work=a.work.resolve();pkg=work/'java/net/minecraft/src';pkg.mkdir(parents=True,exist_ok=True);java=work/'java'
 names=['GenLayer.java','LayerIsland.java','IntCache.java','NoiseGenerator.java','NoiseGeneratorPerlin.java','NoiseGeneratorOctaves.java','MapGenBase.java','MapGenRavine.java','MapGenCaves.java']
 names+=[x.name for x in a.oracle.glob('GenLayer*.java') if x.name!='GenLayer.java']
 for name in names:shutil.copyfile(a.oracle/name,pkg/name)
 # Reuse the independently checked biome fixture created by generation test.
 shutil.copyfile(a.fixtures_root/'generation_test_work/java/net/minecraft/src/BiomeGenBase.java',pkg/'BiomeGenBase.java')
 source=(a.oracle/'ChunkProviderGenerate.java').read_text();methods='\n'.join(method(source,n)for n in ['ChunkProviderGenerate','generateTerrain','replaceBlocksForBiome','initializeNoiseField'])
 java_source=HARNESS.replace('__METHODS__',methods)
 java_source=java_source.replace('class MathHelper {','interface IChunkProvider {}\nclass MathHelper {\n static int floor_double(double x){int n=(int)x;return x<(double)n?n-1:n;}\n static float[] sine=new float[65536];static float sin(float x){return sine[(int)(x*10430.378F)&65535];}static float cos(float x){return sine[(int)(x*10430.378F+16384F)&65535];}')
 java_source=java_source.replace('bedrock=new Block(7),ice=', 'waterMoving=new Block(8),lavaMoving=new Block(10),bedrock=new Block(7),ice=')
 java_source=java_source.replace('BiomeGenBase[] loadBlockGeneratorData','BiomeGenBase getBiomeGenAt(int x,int z){return biomes(1,x,z,1,1)[0];}\n BiomeGenBase[] loadBlockGeneratorData')
 java_source=java_source.replace('WorldChunkManager manager;World(long seed){manager=new WorldChunkManager(seed);}',
   'long seed;long getWorldSeed(){return seed;}WorldChunkManager manager;World(long seed){this.seed=seed;manager=new WorldChunkManager(seed);}')
 java_source=java_source.replace('long[] seeds={0L,',
   'byte[] math=java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(args[0]));java.nio.ByteBuffer mb=java.nio.ByteBuffer.wrap(math).order(java.nio.ByteOrder.LITTLE_ENDIAN);for(int i=0;i<65536;i++)MathHelper.sine[i]=mb.getFloat(16+i*4);\n long[] seeds={0L,')
 java_source=java_source.replace('System.out.println(seed+', 'String surface=hash(blocks);new MapGenRavine().generate(null,world,p[0],p[1],blocks);\nSystem.out.println(seed+')
 java_source=java_source.replace('+density+","+base+","+hash(blocks)', '+surface+","+hash(blocks)')
 java_source=java_source.replace('public class GenerationOracle','public class RavineOracle')
 java_source=java_source.replace('for(long seed:seeds)for(int[] p:positions){','for(long seed:seeds)for(int[] p:positions)for(int mode=0;mode<2;mode++){')
 java_source=java_source.replace('String surface=hash(blocks);','if(mode==1)java.util.Arrays.fill(blocks,(byte)1);String surface=hash(blocks);')
 java_source=java_source.replace('+p[1]+","+surface', '+p[1]+","+mode+","+surface')
 insertion=r'''
  for(long seed:seeds)for(int mode=2;mode<5;mode++){
   World world=new World(seed);byte[] blocks=new byte[32768];java.util.Arrays.fill(blocks,(byte)1);
   if(mode==3)for(int x=0;x<16;x++)for(int z=0;z<16;z++)blocks[(x*16+z)*128+64]=9;
   if(mode==4)for(int x=0;x<16;x++)for(int z=0;z<16;z++){blocks[(x*16+z)*128+70]=2;blocks[(x*16+z)*128+69]=3;}
   String before=hash(blocks);MapGenRavine ravine=new MapGenRavine();ravine.worldObj=world;
   ravine.func_35626_a(seed,0,0,blocks,8D,64D,8D,3F,0F,0F,0,0,3D);
   System.out.println(seed+",0,0,"+mode+","+before+","+hash(blocks));
  }
  World scanWorld=new World(0L);int found=0;
  for(int x=0;x<16 && found<4;x++)for(int z=0;z<16 && found<4;z++){
   byte[] blocks=new byte[32768];java.util.Arrays.fill(blocks,(byte)1);String before=hash(blocks);
   new MapGenRavine().generate(null,scanWorld,x,z,blocks);String after=hash(blocks);
   if(!before.equals(after)){System.out.println("0,"+x+","+z+",1,"+before+","+after);found++;}
  }
 '''
 if a.carver!='carved':
  at=java_source.rfind('\n }');java_source=java_source[:at]+insertion+java_source[at:]
 if a.carver=='cave':
  java_source=java_source.replace('MapGenRavine','MapGenCaves').replace('func_35626_a','generateCaveNode')
  java_source=java_source.replace('3F,0F,0F,0,0,3D','3F,0F,0F,0,0,1D')
 if a.carver=='carved':java_source=java_source.replace('new MapGenRavine().generate(null,world,p[0],p[1],blocks);','new MapGenCaves().generate(null,world,p[0],p[1],blocks);new MapGenRavine().generate(null,world,p[0],p[1],blocks);')
 (pkg/'RavineOracle.java').write_text(java_source);run(['javac','-J-Xms8m','-J-Xmx64m','-d',java,*pkg.glob('*.java')])
 rows=run(['java','-Xms8m','-Xmx64m','-cp',java,'net.minecraft.src.RavineOracle',a.game/'MC10/math10.dat']).splitlines()
 code='#include <stdint.h>\ntypedef uint32_t uint;\n#define MC10_WORLD_SYSTEMS 1\n#define MC10_GENERATION_CARVERS 1\nint mc_biome_yield(void){return 1;}\n'
 for name in ['mc10_u64.tc','mc10_java_double.tc','mc10_fp64.tc','mc10_java_random.tc']:code+=(a.game/'src'/name).read_text()+'\n'
 for name in ['mc10_biome.tc','mc10_generation_noise.tc','mc10_generation.tc','mc10_generation_ravine.tc','mc10_generation_cave.tc']:code+=(a.modules/name).read_text()+'\n'
 code+=r'''
 uint sine_table[65536];
 float mc_math_sin(float value){uint bits=sine_table[(int)(value*10430.378f)&65535];return *((float *)&bits);}
 float mc_math_cos(float value){uint bits=sine_table[(int)(value*10430.378f+16384.0f)&65535];return *((float *)&bits);}
 extern "C" __declspec(dllexport) void math_table(char *data){for(int i=0;i<262144;i++)((char *)sine_table)[i]=data[i];}
 extern "C" __declspec(dllexport) int base(uint lo,uint hi,int x,int z,char *blocks){MCU64 seed={lo,hi};mc_fp_init();mc_gen_initialize(&seed);return mc_gen_base_surface(x,z,blocks);}
 extern "C" __declspec(dllexport) int ravine(uint lo,uint hi,int x,int z,char *blocks){MCU64 seed={lo,hi};return mc_gen_ravines(&seed,x,z,blocks);}
 extern "C" __declspec(dllexport) int node(uint lo,uint hi,char *blocks){MCU64 seed={lo,hi};MCJDouble p[3]={{0,0x40200000},{0,0x40500000},{0,0x40200000}};return mc_gen_ravine_node(&seed,0,0,blocks,p,3.f,0.f,0.f);}
 extern "C" __declspec(dllexport) int carved(uint lo,uint hi,int x,int z,char *blocks){MCU64 seed={lo,hi};mc_fp_init();mc_gen_initialize(&seed);return mc_gen_base_carved(&seed,x,z,blocks);}
 extern "C" __declspec(dllexport) int ready_mask(){return mc_gen_capabilities();}
 extern "C" __declspec(dllexport) int play_ready(){return mc_gen_ready_for_play();}
 '''
 if a.carver=='cave':
  code=code.replace('return mc_gen_ravines(&seed,x,z,blocks);','return mc_gen_caves(&seed,x,z,blocks);')
  code=code.replace('return mc_gen_ravine_node(&seed,0,0,blocks,p,3.f,0.f,0.f);','MCJDouble ratio={0,0x3ff00000};return mc_gen_cave_node(&seed,0,0,blocks,p,3.f,0.f,0.f,0,0,&ratio);')
 if a.carver=='carved':code=code.replace('return mc_gen_ravines(&seed,x,z,blocks);','return mc_gen_caves(&seed,x,z,blocks)&&mc_gen_ravines(&seed,x,z,blocks);')
 cpp=work/'ravine.cpp';cpp.write_text(code);dll=work/'ravine.dll'
 run(['C:/msys64/mingw64/bin/g++.exe','-std=c++17','-O2','-fwrapv','-fno-strict-aliasing','-shared',cpp,'-o',dll])
 lib=C.CDLL(str(dll));lib.base.argtypes=lib.ravine.argtypes=[C.c_uint32,C.c_uint32,C.c_int,C.c_int,C.c_void_p]
 lib.carved.argtypes=lib.base.argtypes;assert lib.ready_mask()==63 and lib.play_ready()==0
 lib.node.argtypes=[C.c_uint32,C.c_uint32,C.c_void_p]
 table=(a.game/'MC10/math10.dat').read_bytes()[16:];lib.math_table(table);changed=0;records=[]
 for row in rows:
  seed,x,z,mode,surface_expected,ravine_expected=row.split(',');seed,x,z,mode=int(seed),int(x),int(z),int(mode);blocks=(C.c_ubyte*32768)()
  assert lib.base(seed&0xffffffff,(seed>>32)&0xffffffff,x,z,blocks)==1
  if mode:C.memset(blocks,1,32768)
  if mode==3:
   for xx in range(16):
    for zz in range(16):blocks[(xx*16+zz)*128+64]=9
  if mode==4:
   for xx in range(16):
    for zz in range(16):blocks[(xx*16+zz)*128+70]=2;blocks[(xx*16+zz)*128+69]=3
  surface_actual=hashlib.sha256(bytes(blocks)).hexdigest();assert surface_actual==surface_expected
  if a.carver=='carved' and mode==0:
   assert lib.carved(seed&0xffffffff,(seed>>32)&0xffffffff,x,z,blocks)==1
  else:assert (lib.node(seed&0xffffffff,(seed>>32)&0xffffffff,blocks)if mode>=2 else lib.ravine(seed&0xffffffff,(seed>>32)&0xffffffff,x,z,blocks))==1
  actual=hashlib.sha256(bytes(blocks)).hexdigest();assert actual==ravine_expected,(seed,x,z,actual,ravine_expected)
  changed+=actual!=surface_actual;records.append({'seed':str(seed),'x':x,'z':z,'fixture':mode,'sha256':actual,'modified':actual!=surface_actual})
 assert changed>=12
 result={'carver':a.carver,'cases':len(rows),'modified_outputs':changed,'block_bytes_compared':len(rows)*32768,'complete_chunks':False,'vectors':records}
 (work/(a.carver+'_audit.json')).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items()if k!='vectors'}))
if __name__=='__main__':main()

