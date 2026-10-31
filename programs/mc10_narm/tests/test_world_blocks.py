"""Compare all registered IDs and legal static metadata against owned Java."""
from pathlib import Path
import argparse
import ctypes
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from source_block_oracle import compile_oracle

ROOT=Path(__file__).resolve().parents[1]
CPP='''
#include <cstdio>
#include <cstdint>
typedef unsigned int uint;
#define MC10_WORLD_SYSTEMS 1
uint n[27];
int mc_runtime_try_get_block(int x,int y,int z,uint *v){*v=(x>=-1&&x<=1&&y>=-1&&y<=1&&z>=-1&&z<=1)?n[(x+1)*9+(y+1)*3+z+1]:0;return 1;}
#include "mc10_blocks.tc"
#include "mc10_blocks_extra.tc"
int main(){mc_blocks_init();for(uint id=1;id<256;id++){MCBlock *b=&mc_blocks[id];if(!b->present)continue;printf("R,%u,%d,%d,%d,%d,%d,%d\\n",id,b->texture,b->render_type,b->opaque_cube,b->normal_block,b->light_opacity,b->light_value);float slip=mc_block_slipperiness(id);printf("S,%u,%u\\n",id,*(uint*)&slip);for(uint m=0;m<16;m++){if(id==34&&(m&7)>5)continue;for(int side=0;side<6;side++)printf("T,%u,%u,%d,%d\\n",id,m,side,mc_block_texture(id,side,m,0,0,0));float x0,y0,z0,x1,y1,z1;mc_block_bounds_at(id,m,0,0,0,&x0,&y0,&z0,&x1,&y1,&z1);printf("B,%u,%u,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\\n",id,m,(double)x0,(double)y0,(double)z0,(double)x1,(double)y1,(double)z1);}}}
'''

def legal_bounds(id,m):
    if id in (29,33,34):return (m&7)<=5
    if id==77:return (m&7) in (1,2,3,4)
    # Ladder's selected bounds are established by collision/selection callback,
    # not setBlockBoundsBasedOnState; oracle dispatch does not exercise it here.
    if id in (36,50,65,68,75,76,117,118,120):return False
    return True

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--output',type=Path,default=ROOT/'oracle_temp');args=ap.parse_args()
    out=compile_oracle(args.source,args.output)
    j=subprocess.run(['java','-Xms8m','-Xmx96m','-XX:ReservedCodeCacheSize=16m','-XX:CompressedClassSpaceSize=32m','-Xss256k','-cp',str(out),'Oracle'],capture_output=True,text=True,check=True).stdout
    (out/'probe.cpp').write_text(CPP)
    subprocess.run(['g++','-std=c++17','-O0','-I',str(ROOT/'src'),str(out/'probe.cpp'),'-o',str(out/'probe.exe')],check=True)
    c=subprocess.run([str(out/'probe.exe')],capture_output=True,text=True,check=True).stdout
    def records(s):return {tuple(v[:4] if v[0]=='T' else v[:3] if v[0]=='B' else v[:2]):v for line in s.splitlines() if(v:=line.split(','))}
    jr,cr=records(j),records(c);errors=[];counts=dict(R=0,T=0,B=0,S=0)
    for key,v in jr.items():
        typ=key[0];id=int(key[1]);cv=cr.get(key)
        if not cv:errors.append((key,'missing'));continue
        if typ=='B':
            if not legal_bounds(id,int(key[2])):continue
            js=[struct.pack('<f',float(x)) for x in v[3:]];cs=[struct.pack('<f',float(x)) for x in cv[3:]]
            if js!=cs:errors.append((key,cv[3:],v[3:]));continue
        elif cv!=v:errors.append((key,cv,v));continue
        counts[typ]+=1
    if errors:
        print('\n'.join(map(str,errors[:80])));raise AssertionError(f'{len(errors)} mismatches')
    assert counts['R']==122
    print('Owned1.0 block source parity PASS',counts)
if __name__=='__main__':main()

