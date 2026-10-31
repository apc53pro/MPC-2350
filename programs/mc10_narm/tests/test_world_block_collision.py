"""Actual binary64 NARM collision module versus owned subclass Java methods."""
from pathlib import Path
import argparse
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from source_block_oracle import compile_oracle
ROOT=Path(__file__).resolve().parents[1]
JAVA='''
import java.util.*;
class CollisionOracle {
 public static void main(String[]args){World w=new World();AxisAlignedBB q=AxisAlignedBB.getBoundingBoxFromPool(-100,-100,-100,100,100,100);
  for(int id=1;id<=122;id++){if(id==36)continue;Block b=Block.blocksList[id];w.id=id;for(int m=0;m<16;m++){if(id==34&&(m&7)>5||id==65&&(m<2||m>5))continue;w.metadata=m;b.setBlockBoundsBasedOnState(w,0,0,0);ArrayList<AxisAlignedBB>list=new ArrayList<>();b.getCollidingBoundingBoxes(w,0,0,0,q,list);System.out.print(id+","+m+","+list.size());for(AxisAlignedBB v:list){double[]a={v.minX,v.minY,v.minZ,v.maxX,v.maxY,v.maxZ};for(double d:a)System.out.printf(",%016x",Double.doubleToRawLongBits(d));}System.out.println();}}
 }
}
'''
CPP='''
#include <cstdio>
#include <cstdint>
typedef unsigned int uint;
#define MC10_WORLD_SYSTEMS 1
int narm_file_read(const char*,void*,uint,uint,uint){return -1;}
#define NARM_STORAGE_SD 1
#include "mc10_types.tc"
#include "mc10_u64.tc"
#include "mc10_java_double.tc"
#include "mc10_fp64.tc"
int mc_runtime_try_get_block(int x,int y,int z,uint*v){*v=0;return 1;}
int mc_runtime_try_get_metadata(int x,int y,int z,uint*v){*v=0;return 1;}
int mc_runtime_chunk_exists(int,int){return 1;}
int mc_region_floor_div(int n,int d){int q=n/d;return n<0&&n%d?q-1:q;}
#include "mc10_blocks.tc"
#include "mc10_blocks_extra.tc"
#include "mc10_collision.tc"
void emit(int v){printf(",%08x%08x",mc_fp_values[v].hi,mc_fp_values[v].lo);}
int main(){mc_blocks_init();mc_fp_init();for(uint id=1;id<=122;id++){if(id==36)continue;for(uint m=0;m<16;m++){if(id==34&&(m&7)>5||id==65&&(m<2||m>5))continue;mc_fp_reset();MCAABB q;for(int a=0;a<3;a++){q.low[a]=mc_fp_i32(-100);q.high[a]=mc_fp_i32(100);}mc_collision_count=0;mc_collision_error=0;mc_collision_shape(&q,id,m,0,0,0);if(mc_fp_error||mc_collision_error)return 2;printf("%u,%u,%d",id,m,mc_collision_count);for(int i=0;i<mc_collision_count;i++){for(int a=0;a<3;a++)emit(mc_collision_boxes[i].low[a]);for(int a=0;a<3;a++)emit(mc_collision_boxes[i].high[a]);}printf("\\n");}}}
'''
def main():
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--game',type=Path,required=True);p.add_argument('--output',type=Path,default=ROOT/'collision_oracle_temp');a=p.parse_args()
    compile_oracle(a.source,a.output,True,{'CollisionOracle.java':JAVA})
    j=subprocess.run(['java','-Xms8m','-Xmx96m','-XX:ReservedCodeCacheSize=16m','-XX:CompressedClassSpaceSize=32m','-Xss256k','-cp',str(a.output),'CollisionOracle'],capture_output=True,text=True,check=True).stdout
    (a.output/'collision.cpp').write_text(CPP)
    subprocess.run(['g++','-std=c++17','-O0','-I',str(ROOT/'src'),'-I',str(a.game/'src'),str(a.output/'collision.cpp'),'-o',str(a.output/'collision.exe')],check=True)
    c=subprocess.run([str(a.output/'collision.exe')],capture_output=True,text=True,check=True).stdout
    jd={tuple(v[:2]):v for line in j.splitlines()if(v:=line.split(','))};cd={tuple(v[:2]):v for line in c.splitlines()if(v:=line.split(','))}
    errors=[(key,cd.get(key),val)for key,val in jd.items()if cd.get(key)!=val]
    if errors:print('\n'.join(map(str,errors[:30])));raise AssertionError(f'{len(errors)} collision mismatches')
    print('Owned 1.0 physical collision parity PASS',len(jd),'ID/metadata records')
if __name__=='__main__':main()

