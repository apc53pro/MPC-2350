"""Fresh owned RenderBlocks vertices versus NARM keys, SD decoder and culling."""
from pathlib import Path
import argparse
import ctypes
import json
import random
import os
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from source_block_oracle import compile_oracle
ROOT=Path(__file__).resolve().parents[1]
JAVA=r'''
import java.io.*;import java.util.*;
class ProbeWorld extends World {
 int ox,oy,oz;int[]ids=new int[27],metas=new int[27];
 public int getBlockId(int x,int y,int z){x-=ox;y-=oy;z-=oz;if(x==0&&y==0&&z==0)return id;if(Math.abs(x)>1||Math.abs(y)>1||Math.abs(z)>1)return 0;return ids[(x+1)*9+(y+1)*3+z+1];}
 public int getBlockMetadata(int x,int y,int z){x-=ox;y-=oy;z-=oz;if(x==0&&y==0&&z==0)return metadata;if(Math.abs(x)>1||Math.abs(y)>1||Math.abs(z)>1)return 0;return metas[(x+1)*9+(y+1)*3+z+1];}
 public int getLightBrightnessForSkyBlocks(int x,int y,int z,int min){return((x-ox+2)&7)|(((y-oy+2)&7)<<3)|(((z-oz+2)&7)<<6)|(min<<9);}
}
class MeshProbe {
 static DataOutputStream out;
 static void i32(int n)throws Exception{out.writeByte(n);out.writeByte(n>>>8);out.writeByte(n>>>16);out.writeByte(n>>>24);}
 static void f32(double f)throws Exception{i32(Float.floatToRawIntBits((float)f));}
 public static void main(String[]args)throws Exception{Scanner in=new Scanner(new File(args[0]));out=new DataOutputStream(new BufferedOutputStream(new FileOutputStream(args[1])));Block.leaves.setGraphicsLevel(true);
  while(in.hasNextInt()){ProbeWorld w=new ProbeWorld();w.id=in.nextInt();w.metadata=in.nextInt();w.ox=in.nextInt();w.oy=in.nextInt();w.oz=in.nextInt();for(int i=0;i<27;i++)w.ids[i]=in.nextInt();for(int i=0;i<27;i++)w.metas[i]=in.nextInt();
   Tessellator t=Tessellator.instance;t.vertices.clear();t.tx=t.ty=t.tz=0;t.face=0;t.red=t.green=t.blue=1;
   RenderBlocks r=new RenderBlocks(w);r.renderBlockByRenderType(Block.blocksList[w.id],w.ox,w.oy,w.oz);i32(t.vertices.size()/4);
   for(int j=0;j<t.vertices.size();j+=4){double[]a=t.vertices.get(j);for(int k=0;k<4;k++){double[]v=t.vertices.get(j+k);f32(v[0]-w.ox);f32(v[1]-w.oy);f32(v[2]-w.oz);f32(v[3]);f32(v[4]);}f32(a[6]);f32(a[7]);f32(a[8]);i32((int)a[5]|(int)a[9]);}
  }out.close();
 }
}
'''
CPP=r'''
#include <cstdio>
#include <cstring>
#include <fstream>
typedef unsigned int uint;
#define MC10_WORLD_SYSTEMS 1
#define NARM_STORAGE_SD 1
char folder[1024];int ox,oy,oz,center,meta,ids[27],metas[27];
int narm_file_size(char*p,uint,uint*out){char path[1200];sprintf(path,"%s/%s",folder,strstr(p,"blocktint")?"blocktint10.dat":"blocks10.dat");std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return-1;*out=(uint)f.tellg();return 0;}
int narm_file_read(char*p,void*out,uint off,uint n,uint){char path[1200];sprintf(path,"%s/%s",folder,strstr(p,"blocktint")?"blocktint10.dat":"blocks10.dat");std::ifstream f(path,std::ios::binary);if(!f)return-1;f.seekg(off);f.read((char*)out,n);return(int)f.gcount();}
#include "mc10_types.tc"
#include "mc10_u64.tc"
int mc_runtime_try_get_block(int x,int y,int z,uint*v){x-=ox;y-=oy;z-=oz;*v=0;if(!x&&!y&&!z){*v=center;return 1;}if(x>=-1&&x<=1&&y>=-1&&y<=1&&z>=-1&&z<=1)*v=ids[(x+1)*9+(y+1)*3+z+1];return 1;}
int mc_runtime_try_get_metadata(int x,int y,int z,uint*v){x-=ox;y-=oy;z-=oz;*v=0;if(!x&&!y&&!z){*v=meta;return 1;}if(x>=-1&&x<=1&&y>=-1&&y<=1&&z>=-1&&z<=1)*v=metas[(x+1)*9+(y+1)*3+z+1];return 1;}
uint mc_asset_read_u32(char*d,int i){return(uint)(unsigned char)d[i]|((uint)(unsigned char)d[i+1]<<8)|((uint)(unsigned char)d[i+2]<<16)|((uint)(unsigned char)d[i+3]<<24);}
#include "mc10_blocks.tc"
#include "mc10_blocks_extra.tc"
#include "mc10_paths.tc"
#include "mc10_block_render_extra.tc"
uint output[48*24];int output_count;
void mc_render_source_quad(uint,int,int,int,float*v,uint desc){memcpy(&output[output_count*24],v,96);output[output_count*24+23]=desc;output_count++;}
extern "C" int init(const char*dir){strcpy(folder,dir);mc_blocks_init();mc_blocks_set_graphics_level(1);return mc_block_mesh_open();}
extern "C" int run(int*row){center=row[0];meta=row[1];ox=row[2];oy=row[3];oz=row[4];memcpy(ids,row+5,sizeof(ids));memcpy(metas,row+32,sizeof(metas));output_count=0;int status=mc_block_mesh_render(center,meta,ox,oy,oz);return status<0?-mc_block_mesh_error:output_count;}
extern "C" uint*data(){return output;}
extern "C" uint key(){return mc_block_mesh_key(center,meta,ox,oy,oz);}
'''

def normalise(record,tints,id):
    v=list(struct.unpack('<23fI',record));rgb=tuple(v[20:23]);f32=lambda n:struct.unpack('<f',struct.pack('<f',n))[0]
    shades={f32(.5):0,f32(1):1,f32(.8):2,f32(.6):4};desc=v[23]
    if id==34 and((desc>>13)&7)==0:desc|=131072
    if rgb[0]==rgb[1]==rgb[2]and rgb[0]in shades:desc|=shades[rgb[0]]<<24
    else:
        tile=int((min(v[i+3]for i in range(0,20,5))+.0001)//16)+16*int((min(v[i+4]for i in range(0,20,5))+.0001)//16)
        slot=tints[(tile,rgb)];desc|=slot<<18|1<<24
        for i in range(0,20,5):v[i+3]-=(tile&15)*16;v[i+4]-=(tile>>4)*16
    v[23]=desc
    return struct.pack('<23fI',*v)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--game',type=Path,required=True);ap.add_argument('--pack',type=Path,default=ROOT/'converted/blocks10.dat');ap.add_argument('--output',type=Path,default=ROOT/'mesh_test_temp');a=ap.parse_args()
    out=a.output;out.mkdir(parents=True,exist_ok=True);rng=random.Random(10029)
    ids=[26,27,28,29,33,34,51,55,66,69,75,76,81,85,93,94,101,102,104,105,106,107,111,113,117,118,120,122]
    neighbors=[0,0,0,1,5,18,20,28,44,55,69,70,72,75,76,77,85,86,93,94,101,102,103,106,107,113]
    cases=[]
    for id in ids:
        for m in range(16):
            if id in(29,33,34)and(m&7)>5:continue
            for pattern in range(3):
                xyz=(rng.randrange(-8,9),rng.choice([1,64,126,127]),rng.randrange(-8,9))
                n=[0]*27 if pattern==0 else[rng.choice(neighbors)for _ in range(27)]
                nm=[rng.randrange(4)for _ in range(27)]
                cases.append([id,m,*xyz,*n,*nm])
    # Every pane connectivity pattern and fire texture parity pair.
    for id in(101,102):
        for mask in range(16):
            n=[0]*27
            for d,index in enumerate((4,22,12,14)):
                if mask&(1<<d):n[index]=id
            cases.append([id,0,0,64,0,*n,*([0]*27)])
    casefile=out/'cases.txt';expectedfile=out/'expected.raw';casefile.write_text('\n'.join(' '.join(map(str,c))for c in cases))
    compile_oracle(a.source,out,True,{'MeshProbe.java':JAVA})
    subprocess.run(['java','-Xms8m','-Xmx96m','-XX:ReservedCodeCacheSize=16m','-XX:CompressedClassSpaceSize=32m','-Xss256k','-cp',str(out),'MeshProbe',str(casefile),str(expectedfile)],check=True)
    (out/'probe.cpp').write_text(CPP)
    subprocess.run(['g++','-std=c++17','-O0','-fwrapv','-shared','-I',str(ROOT/'src'),'-I',str(a.game/'src'),str(out/'probe.cpp'),'-o',str(out/'probe.dll')],check=True)
    runtime=os.add_dll_directory(r'C:\msys64\mingw64\bin')if os.name=='nt'else None
    dll=ctypes.CDLL(str((out/'probe.dll').resolve()));dll.init.argtypes=[ctypes.c_char_p];dll.run.argtypes=[ctypes.POINTER(ctypes.c_int)];dll.data.restype=ctypes.POINTER(ctypes.c_uint32);dll.key.restype=ctypes.c_uint32
    assert dll.init(str(a.pack.parent.resolve()).encode())==1
    ta=json.loads(a.pack.with_name('blocktint10.json').read_text())
    tints={(v['tile'],tuple(v['rgb'])):v['slot']for v in ta['slots']}
    errors=[];raw=memoryview(expectedfile.read_bytes());offset=0
    for index,row in enumerate(cases):
        count=struct.unpack_from('<I',raw,offset)[0];offset+=4
        expected=[normalise(bytes(raw[offset+i*96:offset+(i+1)*96]),tints,row[0])for i in range(count)];offset+=count*96
        n=dll.run((ctypes.c_int*len(row))(*row));actual=ctypes.string_at(dll.data(),max(0,n)*96)
        got=[actual[i*96:(i+1)*96]for i in range(max(0,n))]
        same=len(got)==len(expected)
        if same:
            for gv,ev in zip(got,expected):
                g=struct.unpack('<23fI',gv);e=struct.unpack('<23fI',ev)
                if any(abs(g[k]-e[k])>0.0001 if k<20 and k%5<3 else g[k]!=e[k] for k in range(24)):same=False;break
        if not same:
            errors.append((index,row[:5],hex(dll.key()),n,count))
            if len(errors)<=3:
                for q,(gv,ev)in enumerate(zip(got,expected)):
                    if gv!=ev:print('quad',q,[(k,g,e)for k,(g,e)in enumerate(zip(struct.unpack('<23fI',gv),struct.unpack('<23fI',ev)))if g!=e]);break
    if errors:print(errors[:30]);raise AssertionError(f'{len(errors)} source mesh mismatches')
    print('Owned RenderBlocks mesh/key/SD decoder/face predicates PASS',len(cases),'cases',len(ids),'IDs')
if __name__=='__main__':main()

