"""Neighbor-dependent physical shapes and original far-coordinate float order."""
from pathlib import Path
import argparse
import subprocess
import sys
from test_world_block_collision import CPP,ROOT
sys.path.insert(0,str(ROOT/'tools'))
from source_block_oracle import compile_oracle
JAVA=r'''
import java.util.*;
class EdgeWorld extends World {public int getBlockMetadata(int x,int y,int z){return metadata;}}
class CollisionEdges {
 static void emit(String key,Block b,World w,int x,int z){ArrayList<AxisAlignedBB>list=new ArrayList<>();b.setBlockBoundsBasedOnState(w,x,64,z);b.getCollidingBoundingBoxes(w,x,64,z,AxisAlignedBB.getBoundingBoxFromPool(-1e9,-100,-1e9,1e9,1000,1e9),list);System.out.print(key+","+list.size());for(AxisAlignedBB v:list){double[]a={v.minX,v.minY,v.minZ,v.maxX,v.maxY,v.maxZ};for(double d:a)System.out.printf(",%016x",Double.doubleToRawLongBits(d));}System.out.println();}
 public static void main(String[]args){EdgeWorld w=new EdgeWorld();
  for(int id:new int[]{78,81,85,88,92,107,113})for(int m:new int[]{0,3,7})for(int x:new int[]{-29999999,-16777217,-8388607,8388607,16777217,29999999}){w.id=id;w.metadata=m;emit("F,"+id+","+m+","+x,Block.blocksList[id],w,x,-x);}
  for(int id:new int[]{85,113,101,102})for(int n:new int[]{1,18,20,85,101,102,107,113})for(int mask=0;mask<16;mask++){World q=new World();q.id=id;int[]at={4,22,12,14};for(int d=0;d<4;d++)if((mask&(1<<d))!=0)q.neighbors[at[d]]=n;Block b=Block.blocksList[id];ArrayList<AxisAlignedBB>list=new ArrayList<>();b.setBlockBoundsBasedOnState(q,0,0,0);b.getCollidingBoundingBoxes(q,0,0,0,AxisAlignedBB.getBoundingBoxFromPool(-100,-100,-100,100,100,100),list);System.out.print("N,"+id+","+n+","+mask+","+list.size());for(AxisAlignedBB v:list){double[]a={v.minX,v.minY,v.minZ,v.maxX,v.maxY,v.maxZ};for(double d:a)System.out.printf(",%016x",Double.doubleToRawLongBits(d));}System.out.println();}
 }
}
'''
MAIN=r'''
void edges(int id,int m,int x,int y,int z){mc_fp_reset();MCAABB q;for(int a=0;a<3;a++){q.low[a]=mc_fp_i32(-1000000000);q.high[a]=mc_fp_i32(1000000000);}mc_collision_count=0;mc_collision_error=0;mc_collision_shape(&q,id,m,x,y,z);printf(",%d",mc_collision_count);for(int i=0;i<mc_collision_count;i++){for(int a=0;a<3;a++)emit(mc_collision_boxes[i].low[a]);for(int a=0;a<3;a++)emit(mc_collision_boxes[i].high[a]);}printf("\n");}
int main(){mc_blocks_init();mc_fp_init();int ids[]={78,81,85,88,92,107,113};int xs[]={-29999999,-16777217,-8388607,8388607,16777217,29999999};for(int id:ids)for(int m:{0,3,7})for(int x:xs){printf("F,%d,%d,%d",id,m,x);edges(id,m,x,64,-x);}int ns[]={1,18,20,85,101,102,107,113};int at[]={4,22,12,14};for(int id:{85,113,101,102})for(int n:ns)for(int mask=0;mask<16;mask++){for(int i=0;i<27;i++)neighbors[i]=0;for(int d=0;d<4;d++)if(mask&(1<<d))neighbors[at[d]]=n;printf("N,%d,%d,%d",id,n,mask);edges(id,0,0,0,0);}}
'''
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--game',type=Path,required=True);ap.add_argument('--output',type=Path,default=ROOT/'collision_edges_temp');a=ap.parse_args()
    compile_oracle(a.source,a.output,True,{'CollisionEdges.java':JAVA})
    java=subprocess.run(['java','-Xms8m','-Xmx96m','-XX:ReservedCodeCacheSize=16m','-XX:CompressedClassSpaceSize=32m','-Xss256k','-cp',str(a.output),'CollisionEdges'],capture_output=True,text=True,check=True).stdout
    cpp='#include <initializer_list>\n'+CPP[:CPP.index('int main()')].replace('int mc_runtime_try_get_block(int x,int y,int z,uint*v){*v=0;return 1;}','uint neighbors[27];int mc_runtime_try_get_block(int x,int y,int z,uint*v){*v=0;if(x>=-1&&x<=1&&y>=-1&&y<=1&&z>=-1&&z<=1)*v=neighbors[(x+1)*9+(y+1)*3+z+1];return 1;}')+MAIN
    (a.output/'edges.cpp').write_text(cpp)
    subprocess.run(['g++','-std=c++17','-O0','-fwrapv','-I',str(ROOT/'src'),'-I',str(a.game/'src'),str(a.output/'edges.cpp'),'-o',str(a.output/'edges.exe')],check=True)
    host=subprocess.run([str(a.output/'edges.exe')],capture_output=True,text=True,check=True).stdout
    jd={tuple(v[:4]):v for line in java.splitlines()if(v:=line.split(','))};cd={tuple(v[:4]):v for line in host.splitlines()if(v:=line.split(','))}
    errors=[(k,cd.get(k),v)for k,v in jd.items()if cd.get(k)!=v]
    if errors:print(errors[:20]);raise AssertionError(f'{len(errors)} source collision edge mismatches')
    (a.output/'vectors.txt').write_text(java)
    print('Owned physical shape edges PASS',len(jd),'connected panes/fences and far-coordinate float-order records')
if __name__=='__main__':main()

