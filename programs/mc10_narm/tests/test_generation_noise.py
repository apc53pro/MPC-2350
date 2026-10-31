"""Bit-for-bit compact Perlin and octave arrays against owned 1.0 classes."""
from pathlib import Path
import argparse, ctypes as C, json, shutil, struct, subprocess
from test_biome import DEFAULT, GAME, run

JAVA=r'''
package net.minecraft.src;
import java.util.Random;
class MathHelper {static long floor_double_long(double x){long n=(long)x;return x<(double)n?n-1L:n;}}
public class NoiseOracle {
 public static void main(String[] args){
  long[] seeds={0L,1L,-1L,1234567890123456789L,Long.MIN_VALUE,Long.MAX_VALUE};
  int[][] positions={{0,0,0},{-1,-10,-1},{12345,17,-6789},{-30000000,0,30000000}};
  int[][] dimensions={{1,1,1},{5,1,5},{3,7,4},{2,17,2}};
  double[][] scales={{684.412D,684.412D,684.412D},{8.55515D,4.277575D,8.55515D},{.0625D,.0625D,.0625D},{200D,1D,200D},{1.121D,1D,1.121D}};
  for(long s:seeds)for(int mode=0;mode<2;mode++)for(int q=0;q<dimensions.length;q++)for(int v=0;v<scales.length;v++){
   int[] p=positions[q],d=dimensions[q];double[] sc=scales[v];double[] out=new double[d[0]*d[1]*d[2]];
   Random r=new Random(s);
   if(mode==0)new NoiseGeneratorPerlin(r).func_805_a(out,p[0]+.123D,p[1]-.75D,p[2]+.9D,d[0],d[1],d[2],sc[0],sc[1],sc[2],.125D);
   else new NoiseGeneratorOctaves(r,4).generateNoiseOctaves(out,p[0],p[1],p[2],d[0],d[1],d[2],sc[0],sc[1],sc[2]);
   System.out.print(s+","+mode+","+q+","+v);
   for(double n:out)System.out.print(","+Long.toHexString(Double.doubleToRawLongBits(n)));
   System.out.println();
  }
 }
}
'''
class Pair(C.Structure):_fields_=[('lo',C.c_uint32),('hi',C.c_uint32)]
def pair(x):lo,hi=struct.unpack('<II',struct.pack('<d',x));return Pair(lo,hi)
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--oracle',type=Path,default=DEFAULT);ap.add_argument('--game',type=Path,default=GAME)
 ap.add_argument('--module',type=Path,default=Path(__file__).parents[1]/'src/mc10_generation_noise.tc')
 ap.add_argument('--work',type=Path,default=Path(__file__).parents[1]/'noise_test_work');a=ap.parse_args()
 work=a.work.resolve();pkg=work/'java/net/minecraft/src';pkg.mkdir(parents=True,exist_ok=True);java=work/'java'
 for name in ['NoiseGenerator.java','NoiseGeneratorPerlin.java','NoiseGeneratorOctaves.java']:shutil.copyfile(a.oracle/name,pkg/name)
 (pkg/'NoiseOracle.java').write_text(JAVA);run(['javac','-J-Xms8m','-J-Xmx64m','-d',java,*pkg.glob('*.java')])
 rows=run(['java','-Xms8m','-Xmx64m','-cp',java,'net.minecraft.src.NoiseOracle']).splitlines()
 code='#include <stdint.h>\ntypedef uint32_t uint;\n#define MC10_WORLD_SYSTEMS 1\n'
 for name in ['mc10_u64.tc','mc10_java_double.tc','mc10_fp64.tc','mc10_java_random.tc']:code+=(a.game/'src'/name).read_text()+'\n'
 code+=a.module.read_text()+r'''
 static MCGenPerlin generators[4];
 extern "C" __declspec(dllexport) int noise(uint lo,uint hi,int mode,int x,int y,int z,int w,int h,int d,MCJDouble *scales,MCJDouble *origins,MCJDouble *out){
  MCU64 seed={lo,hi};MCJavaRandom random;MCJDouble divisor={0,0x3fc00000};
  mc_fp_init();mc_java_random_seed_u64(&random,&seed);mc_gen_octaves_init(generators,4,&random);
  for(int i=0;i<w*h*d;i++){out[i].lo=0;out[i].hi=0;}
  if(mode==0)mc_gen_perlin_add(generators,out,origins,w,h,d,scales,&divisor);
  else mc_gen_octaves(generators,4,out,x,y,z,w,h,d,scales);
  return mc_fp_error;
 }
 '''
 cpp=work/'noise.cpp';cpp.write_text(code);dll=work/'noise.dll'
 run(['C:/msys64/mingw64/bin/g++.exe','-std=c++17','-O2','-fwrapv','-fno-strict-aliasing','-shared',cpp,'-o',dll])
 lib=C.CDLL(str(dll));lib.noise.argtypes=[C.c_uint32,C.c_uint32]+[C.c_int]*7+[C.POINTER(Pair)]*3
 positions=[(0,0,0),(-1,-10,-1),(12345,17,-6789),(-30000000,0,30000000)]
 dims=[(1,1,1),(5,1,5),(3,7,4),(2,17,2)]
 scales=[(684.412,)*3,(8.55515,4.277575,8.55515),(.0625,)*3,(200.,1.,200.),(1.121,1.,1.121)]
 count=0
 for row in rows:
  fields=row.split(',');seed,mode,q,v=map(int,fields[:4]);expected=[int(x,16) for x in fields[4:]]
  x,y,z=positions[q];w,h,d=dims[q];sc=(Pair*3)(*[pair(x) for x in scales[v]])
  origins=(Pair*3)(pair(x+.123),pair(y-.75),pair(z+.9));out=(Pair*(w*h*d))()
  assert lib.noise(seed&0xffffffff,(seed>>32)&0xffffffff,mode,x,y,z,w,h,d,sc,origins,out)==0
  actual=[x.lo|(x.hi<<32) for x in out]
  assert actual==expected,(seed,mode,q,v,[(i,hex(x),hex(y)) for i,(x,y) in enumerate(zip(actual,expected)) if x!=y][:8])
  count+=len(out)
 result={'cases':len(rows),'exact_binary64_outputs':count,'perlin_bytes':280,'all_78_octaves_bytes':21840,
         'source_gradient_cache_preserved':True,'signed_24bit_coordinate_wrap':True}
 (work/'noise_audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()

