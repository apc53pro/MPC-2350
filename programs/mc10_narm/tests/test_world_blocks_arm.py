"""Execute block metadata, mesh SD decoder and collision as real NARM Thumb.

This isolated CPU probe replaces the SD transport with a synthetic in-memory
subset of the owned-source mesh pack. It is not the unsupported GP-PICO
emulator, and never accesses hardware or a user save.
"""
from pathlib import Path
import argparse
import ctypes
import os
import struct
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
def fnv(data):
    h=0x811c9dc5
    for b in data:h=((h^b)*0x1000193)&0xffffffff
    return h
PRELUDE=r'''
#define MC10_WORLD_SYSTEMS 1
#define NARM_STORAGE_SD 1
#define narm_file_read probe_file_read
#define narm_file_size probe_file_size
int probe_file_read(char*p,void*out,uint off,uint bytes,uint storage){uint i;unsigned char*d=(unsigned char*)out;if(p[11]=='t'){if(off>243584||bytes>243584-off)return-1;for(i=0;i<bytes;i=i+1)d[i]=off+i<32?probe_tint_header[off+i]:0;return(int)bytes;}if(off>PROBE_MESH_BYTES||bytes>PROBE_MESH_BYTES-off)return-1;for(i=0;i<bytes;i=i+1)d[i]=probe_mesh_bytes[off+i];return(int)bytes;}
int probe_file_size(char*p,uint storage,uint*out){*out=p[11]=='t'?243584:PROBE_MESH_BYTES;return 0;}
#include "mc10_types.tc"
#include "mc10_u64.tc"
#include "mc10_java_double.tc"
#include "mc10_fp64.tc"
int probe_x,probe_y,probe_z,probe_id,probe_meta,probe_ids[27],probe_metas[27];
int mc_runtime_try_get_block(int x,int y,int z,uint*out){x=x-probe_x;y=y-probe_y;z=z-probe_z;*out=0;if(x==0&&y==0&&z==0){*out=(uint)probe_id;return 1;}if(x>=-1&&x<=1&&y>=-1&&y<=1&&z>=-1&&z<=1)*out=(uint)probe_ids[(x+1)*9+(y+1)*3+z+1];return 1;}
int mc_runtime_try_get_metadata(int x,int y,int z,uint*out){x=x-probe_x;y=y-probe_y;z=z-probe_z;*out=0;if(x==0&&y==0&&z==0){*out=(uint)probe_meta;return 1;}if(x>=-1&&x<=1&&y>=-1&&y<=1&&z>=-1&&z<=1)*out=(uint)probe_metas[(x+1)*9+(y+1)*3+z+1];return 1;}
int mc_runtime_chunk_exists(int x,int z){return 1;}
int mc_region_floor_div(int n,int d){int q=n/d;return n<0&&n%d?q-1:q;}
uint mc_asset_read_u32(char*d,int i){return((uint)d[i]&255)|(((uint)d[i+1]&255)<<8)|(((uint)d[i+2]&255)<<16)|(((uint)d[i+3]&255)<<24);}
#include "mc10_blocks.tc"
#include "mc10_blocks_extra.tc"
#include "mc10_paths.tc"
#include "mc10_block_render_extra.tc"
#include "mc10_collision.tc"
uint probe_hash,probe_count;
void probe_hash_word(uint word){int i;for(i=0;i<4;i=i+1){probe_hash=(probe_hash^(word&255))*0x1000193;word=word>>8;}}
void mc_render_source_quad(uint id,int x,int y,int z,float*v,uint descriptor){int i;uint*p=(uint*)v;for(i=0;i<23;i=i+1)probe_hash_word(p[i]);probe_hash_word(descriptor);probe_count=probe_count+1;}
'''
MAIN=r'''
int main(void){int i,j,k,status,base,at,slot;struct MCAABB query;uint id;float slip;
 mc_blocks_init();mc_blocks_set_graphics_level(1);mc_fp_init();
 for(i=1;i<=122;i=i+1)if(!mc_blocks[i].present)return 10+i;
 if(mc_block_static_status(36)!=4||mc_block_static_status(200)!=0)return 200;
 if(mc_block_texture(44,2,3,0,0,0)!=16)return 201;
 slip=mc_block_slipperiness(79);if(*((uint*)&slip)!=0x3f7ae148)return 203;
 slip=mc_block_slipperiness(1);if(*((uint*)&slip)!=0x3f19999a)return 204;
 if(!mc_block_mesh_open())return 202;
 for(i=0;i<PROBE_CASES;i=i+1){base=i*59;probe_id=probe_cases[base];probe_meta=probe_cases[base+1];probe_x=probe_cases[base+2];probe_y=probe_cases[base+3];probe_z=probe_cases[base+4];for(j=0;j<27;j=j+1){probe_ids[j]=probe_cases[base+5+j];probe_metas[j]=probe_cases[base+32+j];}if(mc_block_mesh_key((uint)probe_id,(uint)probe_meta,probe_x,probe_y,probe_z)!=probe_keys[i])return 1000+i;probe_hash=0x811c9dc5;probe_count=0;status=mc_block_mesh_render((uint)probe_id,(uint)probe_meta,probe_x,probe_y,probe_z);if(status!=1||probe_count!=probe_counts[i]||probe_hash!=probe_hashes[i])return 2000+i;}
 probe_x=0;probe_y=0;probe_z=0;probe_id=0;for(i=0;i<27;i=i+1){probe_ids[i]=0;probe_metas[i]=0;}
 at=0;for(i=0;i<PROBE_COLLISIONS;i=i+1){id=probe_collisions[at];j=probe_collisions[at+1];k=probe_collisions[at+2];at=at+3;mc_fp_reset();for(base=0;base<3;base=base+1){query.low[base]=mc_fp_i32(-100);query.high[base]=mc_fp_i32(100);}mc_collision_error=0;mc_collision_count=0;mc_collision_shape(&query,id,(uint)j,0,0,0);if(mc_collision_error||mc_fp_error||mc_collision_count!=k)return 3000+i;for(j=0;j<k;j=j+1)for(base=0;base<6;base=base+1){slot=base<3?mc_collision_boxes[j].low[base]:mc_collision_boxes[j].high[base-3];if(mc_fp_values[slot].lo!=probe_collisions[at]||mc_fp_values[slot].hi!=probe_collisions[at+1])return 4000+i;at=at+2;}}
 mc_collision_error=0;mc_collision_shape(&query,36,0,0,0,0);if(mc_collision_error!=4)return 5000;
 return 0;}
'''

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--game',type=Path,required=True);ap.add_argument('--compiler',type=Path,required=True);ap.add_argument('--pack',type=Path,default=ROOT/'converted/blocks10.dat');ap.add_argument('--output',type=Path,default=ROOT/'arm_probe_temp');ap.add_argument('--mesh-work',type=Path,default=ROOT/'mesh_test_temp');ap.add_argument('--collision-work',type=Path,default=ROOT/'collision_oracle_temp');a=ap.parse_args()
    out=a.output;out.mkdir(parents=True,exist_ok=True)
    allcases=[list(map(int,line.split()))for line in(a.mesh_work/'cases.txt').read_text().splitlines()]
    selected=[];seen=set()
    for row in allcases:
        group=(row[0],row[1] in(0,7,15),row[2]<0)
        if group not in seen:seen.add(group);selected.append(row)
    dllpath=a.mesh_work/'probe.dll'
    runtime=os.add_dll_directory(r'C:\msys64\mingw64\bin')if os.name=='nt'else None
    dll=ctypes.CDLL(str(dllpath.resolve()));dll.init.argtypes=[ctypes.c_char_p];dll.run.argtypes=[ctypes.POINTER(ctypes.c_int)];dll.data.restype=ctypes.POINTER(ctypes.c_uint32);dll.key.restype=ctypes.c_uint32
    assert dll.init(str(a.pack.parent.resolve()).encode())==1
    keys=[];hashes=[];counts=[]
    for row in selected:
        n=dll.run((ctypes.c_int*len(row))(*row));assert n>=0
        keys.append(dll.key());counts.append(n);hashes.append(fnv(ctypes.string_at(dll.data(),n*96)))
    full=a.pack.read_bytes();entries={}
    for i in range(struct.unpack_from('<I',full,12)[0]):
        key,offset,n,flags=struct.unpack_from('<IIHH',full,32+i*12);entries[key]=(full[offset:offset+n*96],n)
    unique_keys=sorted(set(keys));payload=bytearray();index=[];payload_offset=32+len(unique_keys)*12
    for key in unique_keys:
        data,n=entries[key];index.append(struct.pack('<IIHH',key,payload_offset+len(payload),n,0));payload.extend(data)
    mesh=struct.pack('<8s6I',b'MCBM10\0\0',1,len(index),32,payload_offset,96,0)+b''.join(index)+payload
    tintheader=a.pack.with_name('blocktint10.dat').read_bytes()[:32]
    result=subprocess.run(['java','-Xms8m','-Xmx96m','-XX:ReservedCodeCacheSize=16m','-XX:CompressedClassSpaceSize=32m','-Xss256k','-cp',str(a.collision_work),'CollisionOracle'],capture_output=True,text=True,check=True)
    vectors={(int(v[0]),int(v[1])):v for line in result.stdout.splitlines()if(v:=line.split(','))}
    pairs=[(44,0),(44,5),(70,1),(77,1),(78,2),(78,3),(81,0),(88,0),(92,3),(93,0),(107,0),(107,4),(117,0),(118,3),(120,4),(34,2),(113,0),(101,0),(50,1),(60,0)]
    coll=[]
    for pair in pairs:
        v=vectors[pair];coll.extend([*pair,int(v[2])])
        for s in v[3:]:bits=int(s,16);coll.extend([bits&0xffffffff,bits>>32])
    def array(name,values,kind='uint'):return kind+' '+name+'[]={'+','.join(hex(v)if v>=0 else str(v)for v in values)+'};\n'
    source='#define PROBE_MESH_BYTES '+str(len(mesh))+'\n'+array('probe_mesh_bytes',mesh,'unsigned char')+array('probe_tint_header',tintheader,'unsigned char')
    source+='#define PROBE_CASES '+str(len(selected))+'\n#define PROBE_COLLISIONS '+str(len(pairs))+'\n'
    source+=array('probe_cases',[v for row in selected for v in row],'int')+array('probe_keys',keys)+array('probe_hashes',hashes)+array('probe_counts',counts)+array('probe_collisions',coll)+PRELUDE+MAIN
    tc=out/'world_blocks_arm.tc';image=out/'world_blocks_arm.narm';tc.write_text(source)
    built=subprocess.run([str(a.compiler),str(tc),'-I',str(ROOT/'src'),'-I',str(a.game/'src'),'-O3','--stack-bytes','32768','-o',str(image)],capture_output=True,text=True)
    assert built.returncode==0,built.stdout+built.stderr
    print(built.stdout.strip(),flush=True)
    sys.path.insert(0,str(Path(r'C:\Users\enzob\Documents\Codex\2026-10-13\co\work\arm_tools')));sys.path.insert(0,str(a.game/'tests'))
    from player_arm import execute
    result=execute(image);assert result==0,('NARM Thumb block probe failed',result)
    print('Actual NARM Thumb block probe PASS',len(selected),'source-verified mesh SD/key cases;',len(pairs),'source binary64 collision records;122-ID registry and unsafe moving piston guard')
if __name__=='__main__':main()

