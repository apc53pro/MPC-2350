"""Depth-only pass contract and actual near-world/hand raster regression.

Compiles only extracted lifecycle helpers (small fixture), and reuses an
already-built production textured renderer for pixel checks. No MCU emulator.
"""
from pathlib import Path
import argparse
import ctypes as C
import os
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
TERRAIN = Path('C:/Users/enzob/OneDrive/Documentos/RP2350_GP_Computer/Firmware/tests/terrain')
sys.path.insert(0, str(TERRAIN))
import test_textured_tiles as native


def extract(source, name):
    start = source.rfind('\n', 0, source.index(name + '(')) + 1
    brace = source.index('{', start); end = brace + 1; level = 1
    while level:
        level += (source[end] == '{') - (source[end] == '}'); end += 1
    return source[start:end]


PREAMBLE = r'''
#include <cstdint>
#include <cassert>
#include <cstring>
struct var { uint32_t value; };
static bool native_graphics_depth_initialized=false;
static uint8_t native_graphics_frame_id=1;
static uint32_t flushes, pristine, scratch, stats_resets, flags=0x400;
static var color[19200];
static bool pending=false;
static void tri3d_flush_solid_batch() {++flushes; if(pending){color[0].value=0x12345678;pending=false;}}
static void tri3d_mark_depth_pristine(uint8_t frame) {pristine=frame;}
static void tri3d_set_tile_scratch_enabled(bool enabled) {scratch=enabled;}
static void tri3d_stats_reset() {++stats_resets;}
enum {NARM_SERVICE_OK=0, NARM_SERVICE_ERR_ARGUMENTS=-2};
#define EXPORT extern "C" __declspec(dllexport)
'''

TESTS = r'''
EXPORT uint32_t clear_pass(var* stamp,uint32_t frame,bool initialized) {
 native_graphics_frame_id=(uint8_t)frame;native_graphics_depth_initialized=initialized;
 native_graphics_clear_depth(stamp);return native_graphics_frame_id;
}
EXPORT void unit_tests() {
 var stamp[19200];memset(stamp,0x7e,sizeof(stamp));
 native_graphics_frame_id=254;native_graphics_depth_initialized=true;
 color[0].value=0xaaaaaaaa;pending=true;flags=0x600;stats_resets=0;flushes=0;
 assert(service(stamp,1)==-2 && pending && !flushes && native_graphics_frame_id==254);
 assert(service(stamp,0)==0 && !pending && flushes==1 && native_graphics_frame_id==255);
 assert(color[0].value==0x12345678 && pristine==255 && scratch==1 && !stats_resets && flags==0x600);
 for(auto word:stamp)assert(word.value==0x7e7e7e7e);
 assert(service(stamp,0)==0 && native_graphics_frame_id==1 && pristine==1);
 for(auto word:stamp)assert(word.value==0);
 memset(stamp,0x55,sizeof(stamp));native_graphics_depth_initialized=false;
 assert(service(stamp,0)==0 && native_graphics_depth_initialized && native_graphics_frame_id==2);
 for(auto word:stamp)assert(word.value==0);
 assert(color[0].value==0x12345678 && !stats_resets && flags==0x600);
}
'''


def run(source, renderer_path):
    source_text = source.read_text(encoding='utf-8')
    case_start = source_text.index('    case NARM_GRAPHICS_HALF_CLEAR_DEPTH:')
    case_end = source_text.index('\n    case ', case_start + 1)
    case = source_text[case_start:case_end].split(':', 1)[1]
    assert 'if (args[0] == 76u) return (int32_t)0x44430001u;' in source_text
    runtime = os.add_dll_directory(str(Path(shutil.which('g++')).parent)) if os.name == 'nt' else None
    with tempfile.TemporaryDirectory(prefix='depth-clear-', ignore_cleanup_errors=True) as tmp:
        tmp = Path(tmp); cpp = tmp/'depth.cpp'; dll = tmp/'depth.dll'
        cpp.write_text(PREAMBLE + extract(source_text, 'native_graphics_initialize_depth') + '\n' +
                       extract(source_text, 'native_graphics_clear_depth') + '\n' +
                       'static int service(var* stamp,uint32_t arg_words){' + case + '}\n' + TESTS)
        subprocess.run(['g++', '-shared', '-O2', '-std=c++17', str(cpp), '-o', str(dll)], check=True)
        lifecycle = C.CDLL(str(dll)); lifecycle.unit_tests()
        lifecycle.clear_pass.argtypes=[native.PTR,native.WORD,C.c_bool];lifecycle.clear_pass.restype=native.WORD
        renderer = C.CDLL(str(renderer_path))
        signatures = {
            'worker':([C.c_bool],None),'scratch':([C.c_bool],None),
            'diagnostics':([native.WORD],None),'pristine':([native.WORD],None),
            'nonpristine':([],None),'end':([],C.c_bool),
            'begin':([native.PTR,native.PTR,native.PTR,native.WORD,native.WORD],C.c_bool),
            'draw':([native.PTR,native.PTR,native.PTR,native.WORD,C.POINTER(native.Vertex),native.WORD,C.POINTER(native.Transform),native.PTR,C.c_int32,C.c_int32,native.WORD,C.c_bool,native.PTR],C.c_bool),
        }
        for name,(args,result) in signatures.items():
            function=getattr(renderer,'probe_'+name);function.argtypes=args;function.restype=result
        textures=[(16,16,native.Guarded(bytes([color])*256),[None]) for color in (0x24,0xe0)]
        initial=(bytes([0x55])*native.PIXELS,bytes(native.PIXELS*4),bytes(native.PIXELS))
        world=native.make_draw(native.quad(120,140,100,80,.3),asset=0)
        hand=native.make_draw(native.quad(150,160,60,70,.8),asset=1)
        world_frame=native.render(renderer,initial,7,[[world]],textures,2,live_worker=True,pristine=True)
        occluded=native.render(renderer,world_frame,7,[[hand]],textures,2,live_worker=True)
        pixel=180*320+180
        assert world_frame[0][pixel]==occluded[0][pixel]==0x24
        for frame in (7,255):
            stamp=native.Guarded(world_frame[2]);next_frame=lifecycle.clear_pass(stamp.ptr,frame,True)
            assert next_frame==(8 if frame==7 else 1)
            cleared=(world_frame[0],world_frame[1],stamp.bytes())
            # Fresh generation models exactly the helper's production boundary.
            actual=native.render(renderer,cleared,next_frame,[[hand]],textures,2,live_worker=True,pristine=True)
            assert actual[0][pixel]==0xe0,'near terrain incorrectly occluded separate hand pass'
            assert actual[0][145*320+125]==0x24,'world color erased outside hand'
            assert actual[0][10*320+10]==0x55,'background color erased'
        import _ctypes
        _ctypes.FreeLibrary(renderer._handle);_ctypes.FreeLibrary(lifecycle._handle)
    if runtime:runtime.close()
    print('PASS depth-only clear: preflight/no side effects, queued flush, color preservation, generation/wrap/init, flags/counters; actual tiled near-world/hand overlap')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT.parents[1]/'native_runtime.cpp')
    parser.add_argument('--renderer-dll',type=Path,required=True)
    args=parser.parse_args();run(args.source,args.renderer_dll.resolve())
