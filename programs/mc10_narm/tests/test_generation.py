"""Exact base terrain/biome surface phase, never a complete-chunk claim.

Reads the user's source methods into a local-only Java harness; compares raw
binary64 density and both complete 32,768-byte intermediate block arrays.
"""
from pathlib import Path
import argparse, ctypes as C, hashlib, json, re, shutil, struct
from test_biome import DEFAULT,GAME,STUB,run
from test_generation_noise import Pair

def method(text,name):
 m=re.search(r'(?m)^\s*(?:public|private|protected)\s+[^\n]*\b'+name+r'\(',text);assert m,name
 start=m.start();end=text.index('{',start)+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]

HARNESS=r'''
package net.minecraft.src;
import java.util.Random;
import java.security.MessageDigest;
class MathHelper {
 static long floor_double_long(double x){long n=(long)x;return x<(double)n?n-1L:n;}
 static float sqrt_float(float x){return (float)Math.sqrt((double)x);}
}
class Block {
 int blockID;Block(int id){blockID=id;}
 static Block grass=new Block(2),dirt=new Block(3),stone=new Block(1),waterStill=new Block(9),
 bedrock=new Block(7),ice=new Block(79),sand=new Block(12),sandStone=new Block(24);
}
class WorldChunkManager {
 GenLayer[] layers;WorldChunkManager(long seed){layers=GenLayer.func_35497_a(seed);}
 BiomeGenBase[] biomes(int mode,int x,int z,int w,int h){IntCache.func_35268_a();int[] n=layers[mode].func_35500_a(x,z,w,h);BiomeGenBase[] b=new BiomeGenBase[w*h];for(int i=0;i<b.length;i++)b[i]=BiomeGenBase.biomeList[n[i]];return b;}
 BiomeGenBase[] func_35557_b(BiomeGenBase[] old,int x,int z,int w,int h){return biomes(0,x,z,w,h);}
 BiomeGenBase[] loadBlockGeneratorData(BiomeGenBase[] old,int x,int z,int w,int h){return biomes(1,x,z,w,h);}
 float[] func_40539_b(int x,int z,int w,int h){IntCache.func_35268_a();int[] n=layers[2].func_35500_a(x,z,w,h);float[] a=new float[w*h];for(int i=0;i<a.length;i++)a[i]=Math.min(1F,(float)n[i]/65536F);return a;}
}
class World {
 int field_35472_c=128,field_35470_e=63,field_35469_d=127,field_35471_b=11,field_35473_a=7;
 WorldChunkManager manager;World(long seed){manager=new WorldChunkManager(seed);}WorldChunkManager getWorldChunkManager(){return manager;}
}
class ChunkProviderGenerate {
 Random rand;World worldObj;boolean mapFeaturesEnabled;
 NoiseGeneratorOctaves noiseGen1,noiseGen2,noiseGen3,noiseGen4,noiseGen5,noiseGen6,mobSpawnerNoise;
 double[] field_4180_q,noise1,noise2,noise3,noise5,noise6;double[] stoneNoise=new double[256];
 float[] field_35388_l;BiomeGenBase[] biomesForGeneration;
 __METHODS__
}
public class GenerationOracle {
 static String hash(byte[] bytes)throws Exception {byte[] h=MessageDigest.getInstance("SHA-256").digest(bytes);String s="";for(byte b:h)s+=String.format("%02x",b&255);return s;}
 static byte[] bits(double[] values){byte[] b=new byte[values.length*8];for(int i=0;i<values.length;i++){long v=Double.doubleToRawLongBits(values[i]);for(int j=0;j<8;j++)b[i*8+j]=(byte)(v>>>(j*8));}return b;}
 public static void main(String[] args)throws Exception {
  long[] seeds={0L,1L,-1L,1234567890123456789L,Long.MIN_VALUE,Long.MAX_VALUE};
  int[][] positions={{0,0},{-1,-1},{13,-27},{-1875000,1875000}};
  for(long seed:seeds)for(int[] p:positions){
   World world=new World(seed);ChunkProviderGenerate gen=new ChunkProviderGenerate(world,seed,true);
   gen.rand.setSeed((long)p[0]*341873128712L+(long)p[1]*132897987541L);
   byte[] blocks=new byte[32768];gen.generateTerrain(p[0],p[1],blocks);
   String base=hash(blocks),density=hash(bits(gen.field_4180_q));
   gen.biomesForGeneration=world.manager.loadBlockGeneratorData(null,p[0]*16,p[1]*16,16,16);
   gen.replaceBlocksForBiome(p[0],p[1],blocks,gen.biomesForGeneration);
   System.out.println(seed+","+p[0]+","+p[1]+","+density+","+base+","+hash(blocks));
  }
 }
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--oracle',type=Path,default=DEFAULT);ap.add_argument('--game',type=Path,default=GAME)
 ap.add_argument('--modules',type=Path,default=Path(__file__).parents[1]/'src');ap.add_argument('--work',type=Path,default=Path(__file__).parents[1]/'generation_test_work');a=ap.parse_args()
 work=a.work.resolve();pkg=work/'java/net/minecraft/src';pkg.mkdir(parents=True,exist_ok=True);java=work/'java'
 names=['GenLayer.java','LayerIsland.java','IntCache.java','NoiseGenerator.java','NoiseGeneratorPerlin.java','NoiseGeneratorOctaves.java']
 names+=[x.name for x in a.oracle.glob('GenLayer*.java') if x.name!='GenLayer.java']
 for name in names:shutil.copyfile(a.oracle/name,pkg/name)
 stub=STUB.replace('int biomeID;float temperature,rainfall;','int biomeID;float temperature,rainfall;float minHeight,maxHeight;byte topBlock=2,fillerBlock=3;')
 stub=stub.replace('b.rainfall=r;biomeList[id]=b;',
   'b.rainfall=r;float[] lo={-1F,.1F,.1F,.2F,.1F,.1F,-.2F,-.5F,.1F,.1F,-1F,-.5F,.1F,.2F,.2F,-1F};'
   'float[] hi={.4F,.3F,.2F,1.8F,.3F,.4F,.1F,0F,.3F,.3F,.5F,0F,.3F,1.8F,1F,.1F};'
   'b.minHeight=lo[id];b.maxHeight=hi[id];if(id==2)b.topBlock=b.fillerBlock=12;if(id==14||id==15)b.topBlock=110;biomeList[id]=b;')
 (pkg/'BiomeGenBase.java').write_text(stub)
 source=(a.oracle/'ChunkProviderGenerate.java').read_text()
 methods='\n'.join(method(source,n)for n in ['ChunkProviderGenerate','generateTerrain','replaceBlocksForBiome','initializeNoiseField'])
 (pkg/'GenerationOracle.java').write_text(HARNESS.replace('__METHODS__',methods))
 run(['javac','-J-Xms8m','-J-Xmx64m','-d',java,*pkg.glob('*.java')])
 rows=run(['java','-Xms8m','-Xmx64m','-cp',java,'net.minecraft.src.GenerationOracle']).splitlines()
 code='#include <stdint.h>\ntypedef uint32_t uint;\n#define MC10_WORLD_SYSTEMS 1\nint mc_biome_yield(void){return 1;}\n'
 for name in ['mc10_u64.tc','mc10_java_double.tc','mc10_fp64.tc','mc10_java_random.tc']:code+=(a.game/'src'/name).read_text()+'\n'
 for name in ['mc10_biome.tc','mc10_generation_noise.tc','mc10_generation.tc']:code+=(a.modules/name).read_text()+'\n'
 code+=r'''
 extern "C" __declspec(dllexport) int phase(uint lo,uint hi,int x,int z,char *blocks,MCJDouble *density){
  MCU64 seed={lo,hi};mc_fp_init();mc_gen_initialize(&seed);
  if(!mc_gen_density_field(x,z))return -1;
  for(int i=0;i<425;i++)density[i]=mc_gen_density[i];
  mc_gen_terrain_blocks(blocks);return mc_fp_error;
 }
 extern "C" __declspec(dllexport) int surface(int x,int z,char *blocks){return mc_gen_surface_blocks(x,z,blocks);}
 '''
 cpp=work/'generation.cpp';cpp.write_text(code);dll=work/'generation.dll'
 run(['C:/msys64/mingw64/bin/g++.exe','-std=c++17','-O2','-fwrapv','-fno-strict-aliasing','-shared',cpp,'-o',dll])
 lib=C.CDLL(str(dll));lib.phase.argtypes=[C.c_uint32,C.c_uint32,C.c_int,C.c_int,C.c_void_p,C.c_void_p]
 lib.surface.argtypes=[C.c_int,C.c_int,C.c_void_p];results=[]
 for row in rows:
  seed,x,z,density_expected,base_expected,surface_expected=row.split(',');seed,x,z=int(seed),int(x),int(z)
  blocks=(C.c_ubyte*32768)();density=(Pair*425)()
  assert lib.phase(seed&0xffffffff,(seed>>32)&0xffffffff,x,z,blocks,density)==0,(seed,x,z,'density operation')
  density_actual=hashlib.sha256(bytes(density)).hexdigest();base_actual=hashlib.sha256(bytes(blocks)).hexdigest()
  assert density_actual==density_expected,(seed,x,z,'density',density_actual,density_expected)
  assert base_actual==base_expected,(seed,x,z,'base',base_actual,base_expected)
  assert lib.surface(x,z,blocks)==1
  surface_actual=hashlib.sha256(bytes(blocks)).hexdigest()
  assert surface_actual==surface_expected,(seed,x,z,'surface',surface_actual,surface_expected)
  results.append({'seed':str(seed),'x':x,'z':z,'density_sha256':density_actual,'base_sha256':base_actual,'surface_sha256':surface_actual})
 result={'cases':len(rows),'density_binary64_values':425*len(rows),'block_bytes_compared':65536*len(rows),
         'complete_chunks':False,'missing_phases':['caves','ravines','structures','population','lighting','persistence'],'vectors':results}
 (work/'generation_audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items()if k!='vectors'}))
if __name__=='__main__':main()

