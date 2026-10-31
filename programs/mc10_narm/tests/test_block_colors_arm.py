"""Actual NARM Thumb colorizer indexing, SD reader and full residency colors."""
from pathlib import Path
import argparse
import struct
import subprocess
import sys
from test_world_blocks_arm import fnv
ROOT=Path(__file__).resolve().parents[1]
CODE=r'''
#define MC10_WORLD_SYSTEMS 1
#define NARM_STORAGE_SD 1
#define narm_file_size probe_file_size
#define narm_file_read probe_file_read
int probe_file_size(char*p,uint storage,uint*out){*out=PROBE_ASSET_BYTES;return 0;}
int probe_file_read(char*p,void*out,uint off,uint bytes,uint storage){uint i;unsigned char*d=(unsigned char*)out;if(off>PROBE_ASSET_BYTES||bytes>PROBE_ASSET_BYTES-off)return-1;for(i=0;i<bytes;i=i+1)d[i]=probe_asset[off+i];return(int)bytes;}
#include "mc10_types.tc"
#include "mc10_u64.tc"
uint mc_asset_read_u32(char*d,int i){return((uint)d[i]&255)|(((uint)d[i+1]&255)<<8)|(((uint)d[i+2]&255)<<16)|(((uint)d[i+3]&255)<<24);}
struct MCBiomeSample{int biome;float temperature;float rainfall;};
int mc_biome_query(int x,int z,struct MCBiomeSample*s){s->biome=((x^z)&3)==0?6:1;s->temperature=(float)((uint)(x*3129871+z*17)&65535)/65536.0f;s->rainfall=(float)((uint)(z*116129781+x*31)&65535)/65536.0f;return 1;}
#include "mc10_block_colors.tc"
int main(void){int i,j;uint hash=0x811c9dc5,word;for(i=0;i<PROBE_VECTORS;i=i+1)if(mc_block_color_index(probe_vectors[i*3],probe_vectors[i*3+1])!=probe_vectors[i*3+2])return 100+i;
 if(!mc_block_colors_open())return 2;mc_block_colors_begin(-2,-3);if(mc_block_color_at(2,0,-48,-64)!=-1)return 3;for(i=0;i<9;i=i+1)if(mc_block_colors_pump()!=(i==8?1:0))return 10+i;
 for(i=0;i<2304;i=i+1){word=mc_block_column_grass[i];for(j=0;j<4;j=j+1){hash=(hash^(word&255))*0x1000193;word=word>>8;}word=mc_block_column_foliage[i];for(j=0;j<4;j=j+1){hash=(hash^(word&255))*0x1000193;word=word>>8;}}
 if(hash!=PROBE_COLOR_HASH)return 20;
 if(mc_block_color_at(18,1,10000,10000)!=6396257||mc_block_color_at(18,2,10000,10000)!=8431445)return 21;
 if(mc_block_color_at(2,0,0,-64)!=-1)return 22;
 return 0;}
'''
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--game',type=Path,required=True);ap.add_argument('--compiler',type=Path,required=True);ap.add_argument('--pack',type=Path,default=ROOT/'converted/biomecolor10.dat');ap.add_argument('--output',type=Path,default=ROOT/'color_arm_temp');ap.add_argument('--color-work',type=Path,default=ROOT/'color_test_temp');a=ap.parse_args()
    out=a.output;out.mkdir(parents=True,exist_ok=True);rows=[list(map(int,line.split(',')))for line in(a.color_work/'vectors.txt').read_text().splitlines()]
    chosen=rows[::41]+rows[:20];vectors=[v for row in chosen for v in(row[0],row[1],row[3])]
    packed=b''.join(struct.pack('<II',*row[-2:])for row in rows[-2304:]);expected=fnv(packed);asset=a.pack.read_bytes()
    source='#define PROBE_ASSET_BYTES '+str(len(asset))+'\n#define PROBE_VECTORS '+str(len(chosen))+'\n#define PROBE_COLOR_HASH '+hex(expected)+'\n'
    source+='unsigned char probe_asset[]={'+','.join(hex(x)for x in asset)+'};\nuint probe_vectors[]={'+','.join(hex(x)for x in vectors)+'};\n'+CODE
    tc=out/'block_colors_arm.tc';image=out/'block_colors_arm.narm';tc.write_text(source)
    result=subprocess.run([str(a.compiler),str(tc),'-I',str(ROOT/'src'),'-I',str(a.game/'src'),'-O3','--stack-bytes','32768','-o',str(image)],capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr
    print(result.stdout.strip(),flush=True)
    sys.path.insert(0,r'C:\Users\enzob\Documents\Codex\2026-10-13\co\work\arm_tools');sys.path.insert(0,str(a.game/'tests'))
    from player_arm import execute
    status=execute(image);assert status==0,('NARM Thumb colorizer/loader failure',status)
    print('Actual NARM Thumb biome-color probe PASS',len(chosen),'ownedJavaindices;2304negative-coordinate RGB888pairs;original9RGBAtilesandbounded9pumpresidentloader')
if __name__=='__main__':main()

