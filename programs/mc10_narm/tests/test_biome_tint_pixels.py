"""Verify production dynamic tint LRU against original RGBA and RGB332 rules."""
from pathlib import Path
import argparse
import ctypes
import os
import struct
import subprocess
from test_block_colors import CPP,ROOT

TAIL=r'''
#define MC10_TILE_PROFILE 1
int flushes;
void mc_render_list_flush(void){flushes++;}
#include "mc10_biome_tint.tc"
extern "C" int tint(int tile,uint rgb,int level){return mc_tint_tile((rgb<<8)|(uint)tile,level);}
extern "C" unsigned char*pixels(int slot){return(unsigned char*)&mc_tint_lru_pixels[slot*64];}
extern "C" uint*mask(int slot){return &mc_tint_lru_masks[slot*8];}
extern "C" int recipe(uint key){return mc_tint_recipe(key);}
extern "C" void reset(){mc_tint_reset();flushes=0;}
extern "C" int flushed(){return flushes;}
'''
def main():
    p=argparse.ArgumentParser();p.add_argument('--game',type=Path,required=True);p.add_argument('--pack',type=Path,default=ROOT/'converted/biomecolor10.dat');p.add_argument('--output',type=Path,default=ROOT/'tint_test_temp');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    (a.output/'probe.cpp').write_text(CPP+TAIL)
    subprocess.run(['g++','-std=c++17','-O0','-fwrapv','-shared','-I',str(a.game/'src'),str(a.output/'probe.cpp'),'-o',str(a.output/'probe.dll')],check=True)
    runtime=os.add_dll_directory(r'C:\msys64\mingw64\bin')if os.name=='nt'else None
    dll=ctypes.CDLL(str((a.output/'probe.dll').resolve()));dll.init.argtypes=[ctypes.c_char_p];dll.tint.argtypes=[ctypes.c_int,ctypes.c_uint32,ctypes.c_int];dll.pixels.restype=ctypes.POINTER(ctypes.c_ubyte);dll.mask.restype=ctypes.POINTER(ctypes.c_uint32);dll.recipe.argtypes=[ctypes.c_uint32]
    assert dll.init(str(a.pack.resolve()).encode())==1;dll.reset()
    f32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
    asset=a.pack.read_bytes();tiles=(0,38,39,56,52,53,132,133,143);colors=(0xffffff,0x619961,0x80a755,0x123456,0x00ff00,0xf01020)
    for t,tile in enumerate(tiles):
        raw=asset[393248+t*1024:394272+t*1024]
        for rgb in colors:
            scales=[f32(((rgb>>shift)&255)/255)for shift in(16,8,0)]
            for level in range(16):
                expected=bytearray();mask=[0]*8
                for i in range(256):
                    r,g,b,alpha=raw[i*4:i*4+4];r,g,b=[int(f32(c*s))for c,s in zip((r,g,b),scales)]
                    base=(r&224)|((g>>5)<<2)|(b>>6)
                    expected.append((((base>>5)*level+7)//15)<<5|(((((base>>2)&7)*level+7)//15)<<2)|(((base&3)*level+7)//15))
                    if alpha>=128:mask[i>>5]|=1<<(31-(i&31))
                slot=dll.tint(tile,rgb,level);assert slot>=0
                actual=ctypes.string_at(dll.pixels(slot),256)
                assert actual==bytes(expected),(tile,hex(rgb),level,next((i for i,(x,y)in enumerate(zip(actual,expected))if x!=y),-1))
                assert ctypes.string_at(dll.mask(slot),32)==struct.pack('<8I',*mask)
    assert dll.flushed()>16
    before=dll.flushed();slot=dll.tint(143,colors[-1],15);assert dll.flushed()==before
    for key in range(1,8192):assert dll.recipe(key)==key
    assert dll.recipe(8192)==0;assert dll.recipe(4096)==4096
    print('Dynamic source RGBA tint pixels/masks PASS',len(tiles)*len(colors)*16,'tiles;LRUflush-before-eviction;immutable8191recipeoverflow')
if __name__=='__main__':main()

