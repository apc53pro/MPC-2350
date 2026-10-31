"""Original Colorizer double arithmetic and bounded RGB888 column loader."""
from pathlib import Path
import argparse
import ctypes
import os
import random
import struct
import subprocess
import re
ROOT=Path(__file__).resolve().parents[1]
JAVA=r'''
import java.io.*;import java.util.*;
class Colors {
 static int u(byte b){return b&255;}
 public static void main(String[]args)throws Exception{byte[]data=java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(args[0]));int[]g=new int[65536],f=new int[65536],idx=new int[65536];for(int i=0;i<65536;i++){int a=32+i*6;g[i]=u(data[a])<<16|u(data[a+1])<<8|u(data[a+2]);f[i]=u(data[a+3])<<16|u(data[a+4])<<8|u(data[a+5]);idx[i]=i;}
  Scanner in=new Scanner(new File(args[1]));while(in.hasNextInt()){int t=in.nextInt(),r=in.nextInt(),swamp=in.nextInt();float temp=(float)t/65536f,rain=(float)r/65536f;if(temp>1)temp=1;if(rain>1)rain=1;ColorizerGrass.setGrassBiomeColorizer(idx);int index=ColorizerGrass.getGrassColor((double)temp,(double)rain);ColorizerGrass.setGrassBiomeColorizer(g);ColorizerFoliage.getFoilageBiomeColorizer(f);int grass=ColorizerGrass.getGrassColor((double)temp,(double)rain),foliage=ColorizerFoliage.getFoliageColor((double)temp,(double)rain);if(swamp!=0){grass=((grass&16711422)+5115470)/2;foliage=((foliage&16711422)+5115470)/2;}System.out.println(t+","+r+","+swamp+","+index+","+grass+","+foliage);}
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
char file[1024];int fail_read;
int narm_file_size(char*,uint,uint*out){std::ifstream f(file,std::ios::binary|std::ios::ate);if(!f)return-1;*out=(uint)f.tellg();return 0;}
int narm_file_read(char*,void*out,uint off,uint n,uint){if(fail_read)return-1;std::ifstream f(file,std::ios::binary);f.seekg(off);f.read((char*)out,n);return(int)f.gcount();}
#include "mc10_types.tc"
#include "mc10_u64.tc"
uint mc_asset_read_u32(char*d,int i){return(uint)(unsigned char)d[i]|((uint)(unsigned char)d[i+1]<<8)|((uint)(unsigned char)d[i+2]<<16)|((uint)(unsigned char)d[i+3]<<24);}
struct MCBiomeSample{int biome;float temperature;float rainfall;};
int mc_biome_query(int x,int z,MCBiomeSample*s){s->biome=((x^z)&3)==0?6:1;s->temperature=(float)((uint)(x*3129871+z*17)&65535)/65536.0f;s->rainfall=(float)((uint)(z*116129781+x*31)&65535)/65536.0f;return 1;}
#include "mc10_block_colors.tc"
extern "C" uint index_for(uint t,uint r){return mc_block_color_index(t,r);}
extern "C" uint swamp(uint rgb){return mc_block_color_swamp(rgb);}
extern "C" int init(const char*p){strcpy(file,p);return mc_block_colors_open();}
extern "C" int pump(){return mc_block_colors_pump();}
extern "C" void begin(int x,int z){mc_block_colors_begin(x,z);}
extern "C" int color(uint id,uint meta,int x,int z){return mc_block_color_at(id,meta,x,z);}
extern "C" uint*tile(int tile){return mc_block_color_tile(tile);}
extern "C" void failure(int n){fail_read=n;}
'''
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--game',type=Path,required=True);ap.add_argument('--pack',type=Path,default=ROOT/'converted/biomecolor10.dat');ap.add_argument('--output',type=Path,default=ROOT/'color_test_temp');a=ap.parse_args()
    out=a.output;out.mkdir(parents=True,exist_ok=True);rng=random.Random(1029)
    vals=[0,1,6553,13107,19660,26214,32768,45875,52428,58982,65535,65536,131072]
    cases=[(t,r,s)for t in vals for r in vals for s in(0,1)]+[(rng.randrange(65537),rng.randrange(65537),rng.randrange(2))for _ in range(4096)]
    # Exact source climate at every column used by the bounded loader fixture.
    for z in range(-64,-16):
        for x in range(-48,0):cases.append(((x*3129871+z*17)&65535,(z*116129781+x*31)&65535,int(((x^z)&3)==0)))
    for n in('ColorizerGrass','ColorizerFoliage'):
        txt=(a.source/(n+'.java')).read_text();(out/(n+'.java')).write_text(re.sub(r'^package .*?;\s*','',txt))
    (out/'Colors.java').write_text(JAVA);(out/'cases.txt').write_text('\n'.join(' '.join(map(str,c))for c in cases))
    subprocess.run(['javac','-J-Xmx96m',str(out/'ColorizerGrass.java'),str(out/'ColorizerFoliage.java'),str(out/'Colors.java')],check=True)
    run=subprocess.run(['java','-Xms8m','-Xmx96m','-XX:ReservedCodeCacheSize=16m','-XX:CompressedClassSpaceSize=32m','-Xss256k','-cp',str(out),'Colors',str(a.pack),str(out/'cases.txt')],capture_output=True,text=True,check=True)
    vectors=[list(map(int,line.split(',')))for line in run.stdout.splitlines()];(out/'vectors.txt').write_text(run.stdout)
    (out/'probe.cpp').write_text(CPP)
    subprocess.run(['g++','-std=c++17','-O0','-fwrapv','-shared','-I',str(ROOT/'src'),'-I',str(a.game/'src'),str(out/'probe.cpp'),'-o',str(out/'probe.dll')],check=True)
    runtime=os.add_dll_directory(r'C:\msys64\mingw64\bin')if os.name=='nt'else None
    dll=ctypes.CDLL(str((out/'probe.dll').resolve()));dll.init.argtypes=[ctypes.c_char_p];dll.index_for.argtypes=[ctypes.c_uint32,ctypes.c_uint32];dll.index_for.restype=ctypes.c_uint32;dll.swamp.restype=ctypes.c_uint32;dll.tile.restype=ctypes.POINTER(ctypes.c_uint32)
    data=a.pack.read_bytes()
    for t,r,s,index,g,f in vectors:
        actual=dll.index_for(t,r);assert actual==index,(t,r,actual,index)
        ga=int.from_bytes(data[32+index*6:35+index*6],'big');fa=int.from_bytes(data[35+index*6:38+index*6],'big')
        if s:ga=dll.swamp(ga);fa=dll.swamp(fa)
        assert(ga,fa)==(g,f)
    assert dll.init(str(a.pack.resolve()).encode())==1
    dll.begin(-2,-3);assert dll.color(18,1,9999,9999)==6396257;assert dll.color(18,2,9999,9999)==8431445;assert dll.color(2,0,-48,-64)==-1
    for i in range(9):assert dll.pump()==int(i==8)
    for k,(z,x)in enumerate((z,x)for z in range(-64,-16)for x in range(-48,0)):
        v=vectors[-2304+k];g,f=v[-2:]
        assert dll.color(2,0,x,z)==g;assert dll.color(31,1,x,z)==g;assert dll.color(31,0,x,z)==0xffffff
        assert dll.color(18,0,x,z)==f;assert dll.color(106,0,x,z)==f
    assert dll.color(2,0,0,-64)==-1;assert dll.color(18,0,-49,-64)==-1
    for i,tile in enumerate((0,38,39,56,52,53,132,133,143)):assert ctypes.string_at(dll.tile(tile),1024)==data[393248+i*1024:394272+i*1024]
    dll.begin(0,0);dll.failure(1);assert dll.pump()==-1;dll.failure(0);dll.begin(-2,-3)
    for i in range(9):assert dll.pump()==int(i==8)
    print('Owned Colorizer double/Q16 and swamp parity PASS',len(vectors),'climate vectors;2304exactnegative-coordinate columns;9RGBA tiles;cooperativefailure/retry')
if __name__=='__main__':main()

