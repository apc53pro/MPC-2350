"""Owned 1.0 Java GenLayer oracle versus actual bounded NARM module.

No proprietary files are bundled: source classes are consumed from --oracle.
Compile products/local copied oracle inputs remain in --work, not releases.
"""
from pathlib import Path
import argparse, ctypes, json, os, shutil, subprocess, time

DEFAULT = Path('C:/Users/enzob/AppData/Roaming/.minecraft/versions/1.0/minecraft/src/net/minecraft/src')
GAME = Path('C:/Users/enzob/OneDrive/Documentos/RP2350_GP_Computer/Firmware/resource_development/programs/mc10_narm')
DRIVER = r'''
package net.minecraft.src;
public class BiomeOracle {
 public static void main(String[] args) {
  long[] seeds={0L,1L,-1L,1234567890123456789L,Long.MIN_VALUE,Long.MAX_VALUE};
  int[][] places={{0,0,1,1},{-1,-1,16,16},{1234,-5678,16,11},{-200000,300000,7,13},
   {4,4,16,16},{-16384,-16385,16,16},{31,15,3,16}};
  for(long seed:seeds) {
   GenLayer[] layers=GenLayer.func_35497_a(seed);
   for(int output=0;output<4;output++)for(int[] p:places) {
    IntCache.func_35268_a(); int[] values=layers[output].func_35500_a(p[0],p[1],p[2],p[3]);
    System.out.print(seed+","+output+","+p[0]+","+p[1]+","+p[2]+","+p[3]);
    for(int j=0;j<p[2]*p[3];j++)System.out.print(","+values[j]);
    System.out.println();
   }
  }
 }
}
'''
STUB = r'''
package net.minecraft.src;
class BiomeGenBase {
 static final BiomeGenBase[] biomeList=new BiomeGenBase[256];
 static final BiomeGenBase ocean=b(0,.5F,.5F),plains=b(1,.8F,.4F),desert=b(2,2F,0F),
  hills=b(3,.2F,.3F),forest=b(4,.7F,.8F),taiga=b(5,.3F,.8F),swampland=b(6,.8F,.9F),
  river=b(7,.5F,.5F),hell=b(8,2F,0F),sky=b(9,.5F,.5F),frozenOcean=b(10,0F,.5F),
  frozenRiver=b(11,0F,.5F),icePlains=b(12,0F,.5F),iceMountains=b(13,0F,.5F),
  mushroomIsland=b(14,.9F,1F),mushroomIslandShore=b(15,.9F,1F);
 int biomeID;float temperature,rainfall;
 static BiomeGenBase b(int id,float t,float r){BiomeGenBase b=new BiomeGenBase();b.biomeID=id;b.temperature=t;b.rainfall=r;biomeList[id]=b;return b;}
 int func_35474_f(){return (int)(temperature*65536F);}
 int func_35476_e(){return (int)(rainfall*65536F);}
}
'''
def run(cmd, **kwargs):
    return subprocess.run([str(x) for x in cmd],check=True,capture_output=True,text=True,**kwargs).stdout

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--oracle',type=Path,default=DEFAULT)
    ap.add_argument('--game',type=Path,default=GAME);ap.add_argument('--module',type=Path,default=Path(__file__).parents[1]/'src/mc10_biome.tc')
    ap.add_argument('--work',type=Path,default=Path(__file__).parents[1]/'test_work');args=ap.parse_args()
    work=args.work.resolve();java=work/'java';pkg=java/'net/minecraft/src';pkg.mkdir(parents=True,exist_ok=True)
    names=['GenLayer.java','LayerIsland.java','IntCache.java']+[x.name for x in args.oracle.glob('GenLayer*.java') if x.name!='GenLayer.java']
    for name in names:shutil.copyfile(args.oracle/name,pkg/name)
    (pkg/'BiomeGenBase.java').write_text(STUB);(pkg/'BiomeOracle.java').write_text(DRIVER)
    run(['javac','-J-Xms8m','-J-Xmx64m','-d',java,*pkg.glob('*.java')])
    rows=run(['java','-Xms8m','-Xmx64m','-cp',java,'net.minecraft.src.BiomeOracle']).splitlines()
    modules=['mc10_u64.tc','mc10_java_double.tc','mc10_fp64.tc']
    code='#include <stdint.h>\ntypedef uint32_t uint;\n#define MC10_WORLD_SYSTEMS 1\nint fixture_yield_calls;int fixture_cancel_at;int mc_biome_yield(void){fixture_yield_calls++;return !fixture_cancel_at||fixture_yield_calls<fixture_cancel_at;}\n'
    for name in modules:code+=(args.game/'src'/name).read_text()+'\n'
    code+=args.module.read_text()+r'''
    extern "C" __declspec(dllexport) void seed(uint lo,uint hi){MCU64 s={lo,hi};mc_fp_init();mc_biome_seed(&s);}
    extern "C" __declspec(dllexport) int region(int output,int x,int z,int w,int h,int *out,int cap){
      static int scratch[8192]; return mc_biome_region(output,x,z,w,h,out,scratch,cap);}
    extern "C" __declspec(dllexport) int peak(){return mc_biome_work_peak;}
    extern "C" __declspec(dllexport) int query(int x,int z,MCBiomeSample *out){return mc_biome_query(x,z,out);}
    extern "C" __declspec(dllexport) int yield_calls(int reset){if(reset)fixture_yield_calls=0;return fixture_yield_calls;}
    extern "C" __declspec(dllexport) void cancel_at(int n){fixture_cancel_at=n;}
    extern "C" __declspec(dllexport) int error(){return mc_biome_error;}
    extern "C" __declspec(dllexport) uint cells(){return mc_biome_work_cells;}
    '''
    cpp=work/'biome.cpp';cpp.write_text(code);dll=work/'biome.dll'
    run(['C:/msys64/mingw64/bin/g++.exe','-std=c++17','-O2','-fwrapv','-shared',cpp,'-o',dll])
    lib=ctypes.CDLL(str(dll));lib.seed.argtypes=[ctypes.c_uint,ctypes.c_uint]
    lib.region.argtypes=[ctypes.c_int]*5+[ctypes.POINTER(ctypes.c_int),ctypes.c_int];lib.region.restype=ctypes.c_int
    maximum=0;count=0;previous=None
    for row in rows:
        values=[int(x) for x in row.split(',')];s,output,x,z,w,h=values[:6];expected=values[6:]
        if previous!=s:lib.seed(s&0xffffffff,(s>>32)&0xffffffff);previous=s
        out=(ctypes.c_int*(w*h))()
        assert lib.region(output,x,z,w,h,out,8192)==1,(s,output,x,z,w,h)
        actual=list(out)
        assert actual==expected,(s,output,x,z,w,h,[(i,a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b][:10])
        maximum=max(maximum,lib.peak());count+=len(actual)
    # Bounded failure does not publish partial output.
    untouched=(ctypes.c_int*256)(*([0x12345]*256))
    assert lib.region(1,0,0,16,16,untouched,1)==0
    assert list(untouched)==[0x12345]*256
    lib.yield_calls(1);lib.cancel_at(1)
    assert lib.region(1,0,0,16,16,untouched,8192)==0
    assert lib.error()==4
    assert list(untouched)==[0x12345]*256
    assert lib.cells()==64
    lib.cancel_at(0);lib.seed(0,0);bench=[]
    for output in range(4):
      for side in (1,16):
        out=(ctypes.c_int*(side*side))();lib.yield_calls(1);started=time.perf_counter()
        assert lib.region(output,0,0,side,side,out,8192)==1
        bench.append({'output':output,'side':side,'processed_cells':lib.cells(),
          'cooperative_yields':lib.yield_calls(0),'host_ms':(time.perf_counter()-started)*1000})
    class Sample(ctypes.Structure):_fields_=[('biome',ctypes.c_int),('temperature',ctypes.c_float),('rainfall',ctypes.c_float)]
    sample=Sample();lib.yield_calls(1);assert lib.query(0,0,ctypes.byref(sample))==1
    first_calls=lib.yield_calls(0);assert first_calls>0
    assert lib.query(15,15,ctypes.byref(sample))==1 and lib.yield_calls(0)==first_calls
    result={'cases':len(rows),'exact_cells':count,'workspace_peak_ints':maximum,'workspace_peak_bytes':maximum*4,
            'layers':54,'error_atomic_output':True,'cancel_after_64_cells':True,'cached_query_no_generation':True,'workload':bench}
    (work/'biome_audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()

