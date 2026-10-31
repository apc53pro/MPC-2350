"""Incremental and synchronous cache compilers must emit identical geometry.

Checks owned chunks, reused/non-grid slot order, per-pump bounds, cancellation,
overflow fallback and render-state restoration. Never writes the owned save.
"""
import ctypes
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

CANDIDATE=Path(__file__).resolve().parents[1]
PROJECT=Path(os.environ.get('MC10_PROJECT',r'C:\Users\enzob\OneDrive\Documentos\RP2350_GP_Computer\Firmware\resource_development\programs\mc10_narm'))
RENDERER=Path(os.environ.get('MC10_RENDERER',str(PROJECT/'src/mc10_render.tc')))
sys.path.insert(0,str(PROJECT/'tests'))
from test_render_cache import integer,load,trace,Player,pair,Options
from test_region import read_chunk,tagged

def raw(lib,name,size):return bytes((ctypes.c_ubyte*size).in_dll(lib,name))
def snapshot(lib):
 groups=integer(lib,'mc_render_cache_group_count').value;quads=integer(lib,'mc_render_geometry_count').value
 ranges=(ctypes.c_uint32*32768).in_dll(lib,'mc_render_geometry_range')
 centers=(ctypes.c_float*(12288*3)).in_dll(lib,'mc_render_geometry_center')
 first=[int(ranges[i]&16383)for i in range(groups)if ranges[i]&0x80000000 and ranges[i]&0x7fffc000]
 return (groups,quads,integer(lib,'mc_render_geometry_blocks').value,
  raw(lib,'mc_render_cached_keys',groups*4),raw(lib,'mc_render_cached_style',groups*4),
  raw(lib,'mc_render_run_end',groups*4),raw(lib,'mc_render_geometry_range',groups*4),
  raw(lib,'mc_render_geometry',quads*84),
  b''.join(struct.pack('<fff',*centers[i*3:i*3+3])for i in first))

def incremental(lib,clock_increment=0,interleave=True):
 integer(lib,'fixture_clock_increment').value=clock_increment
 lib.mc_render_rebuild_begin();calls=0
 while True:
  before_state=integer(lib,'mc_render_rebuild_state').value
  before_cursor=integer(lib,'mc_render_rebuild_cursor').value
  before_cells=integer(lib,'mc_render_rebuild_chunk').value*32768+integer(lib,'mc_render_rebuild_cell_index').value
  fancy=calls&1
  if interleave:Options.in_dll(lib,'mc_options').fancy=fancy
  result=lib.mc_render_rebuild_step();calls+=1
  assert integer(lib,'mc_render_capture').value==0
  assert integer(lib,'mc_render_capture_fancy').value==0
  assert integer(lib,'mc_render_cached_block').value==0
  assert integer(lib,'mc_render_cached_standard').value==0
  assert integer(lib,'mc_render_texture_override').value==-1
  assert integer(lib,'mc_render_force_mask').value==0
  if interleave:assert Options.in_dll(lib,'mc_options').fancy==fancy,'forced Fancy leaked across pump'
  if before_state==1:
   after=integer(lib,'mc_render_rebuild_chunk').value*32768+integer(lib,'mc_render_rebuild_cell_index').value
   assert 0<=after-before_cells<=128
   if clock_increment:assert after-before_cells<=16
  elif before_state==2:assert integer(lib,'mc_render_rebuild_cursor').value-before_cursor<=128
  elif before_state==3:assert integer(lib,'mc_render_rebuild_cursor').value-before_cursor<=8
  if result:break
  assert not integer(lib,'mc_render_cache_ready').value
  assert calls<100000
 integer(lib,'fixture_clock_increment').value=0
 return calls

def run():
 with tempfile.TemporaryDirectory(prefix='mc10-stream-render-',ignore_cleanup_errors=True)as temporary:
  temp=Path(temporary);dll=temp/'render.dll'
  (temp/'renderer_under_test.tc').write_text('#include "'+RENDERER.as_posix()+'"\n')
  wrapper=temp/'fixture.c'
  wrapper.write_text('#define MC10_PLAYER_FOUNDATION 1\n#include "'+(PROJECT/'tests/render_fixture.c').as_posix()+'"\nvoid fixture_set_chunk(int i,int x,int z){mc_runtime_chunks[i].chunk.x_position=x;mc_runtime_chunks[i].chunk.z_position=z;}\nint fixture_chunk_x(int i){return mc_runtime_chunks[i].chunk.x_position;}\nint fixture_chunk_z(int i){return mc_runtime_chunks[i].chunk.z_position;}\nvoid fixture_set_type(int id,int type){mc_blocks[id].render_type=type;}\nvoid fixture_set_present(int id,int present){mc_blocks[id].present=present;}\nfloat mc_game_partial_tick(void){return 0;}\nvoid mc_hud_draw(void){}\nvoid mc_viewmodel_capture_frame(float partial){}\nvoid mc_viewmodel_draw(float partial){}\n')
  result=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(wrapper),'-I',str(temp),'-I',str(PROJECT),'-o',str(dll)],capture_output=True,text=True)
  assert result.returncode==0,result.stderr
  lib=ctypes.CDLL(str(dll));world=PROJECT/'MC10/saves/New World';pos=load(lib,world)
  original_center=(integer(lib,'mc_runtime_center_x').value,integer(lib,'mc_runtime_center_z').value)
  views=[(0,0,0,0),(90,0,.5,1),(180,25,1,1),(-39.125,17.7,.5,0),(.89453125,-89,0,1)]
  comparisons=0
  for layout in range(2):
   if layout:
    # Model slot reuse: only coordinates, never slot index, identify a chunk.
    # This saved northern window contains only already-ported registrations.
    # The diagonal southern window really has unported IDs; test it explicitly
    # as a failure later, never erase/substitute those original owned blocks.
    cx=integer(lib,'mc_runtime_center_x').value;cz=integer(lib,'mc_runtime_center_z').value-1
    integer(lib,'mc_runtime_center_x').value=cx;integer(lib,'mc_runtime_center_z').value=cz
    for slot in range(9):
     sequence=(slot*5)%9;x,z=cx+sequence%3-1,cz+sequence//3-1
     lib.fixture_set_chunk(slot,x,z)
     _,_,level=read_chunk(world/'region'/f'r.{x//32}.{z//32}.mcr',x,z)
     for name,key,size in [('fixture_blocks','Blocks',32768),('fixture_metadata','Data',16384),('fixture_sky','SkyLight',16384),('fixture_light','BlockLight',16384),('fixture_height','HeightMap',256)]:
      array=(ctypes.c_ubyte*(size*9)).in_dll(lib,name)
      ctypes.memmove(ctypes.addressof(array)+slot*size,tagged(level,key,7),size)
    p=Player.in_dll(lib,'mc_runtime_player');p.pos_x=pair(cx*16+8.125);p.pos_y=pair(pos[1]);p.pos_z=pair(cz*16+8.5)
   assert lib.mc_render_build_cache()==1
   expected=snapshot(lib)
   frames=[trace(lib,*view)[0]for view in views]
   for clock in [0,4]:
    pumps=incremental(lib,clock)
    assert pumps>2300,('early rebuild completion',layout,clock,pumps,integer(lib,'mc_render_rebuild_bad_id').value)
    assert integer(lib,'mc_render_cache_ready').value==1
    assert snapshot(lib)==expected,('cache compiler mismatch',layout,clock)
    for view,frame in zip(views,frames):assert trace(lib,*view)[0]==frame;comparisons+=1
   # Cancel during each phase and restart: partially built cache never exposed.
   for phase in [1,2,3]:
    lib.mc_render_rebuild_begin()
    while integer(lib,'mc_render_rebuild_state').value!=phase:assert lib.mc_render_rebuild_step()==0
    lib.mc_render_rebuild_step();lib.mc_render_rebuild_cancel()
    assert not integer(lib,'mc_render_cache_ready').value
    assert not integer(lib,'mc_render_capture').value
    incremental(lib,0,False);assert snapshot(lib)==expected
   # Whole-block geometry capacity rollback remains exact, including fallback.
   integer(lib,'mc_render_geometry_limit').value=1
   lib.mc_render_build_cache();small=snapshot(lib)
   incremental(lib);assert snapshot(lib)==small
   integer(lib,'mc_render_cache_ready').value=0
   direct=trace(lib,90,15,.5,1)[0]
   integer(lib,'mc_render_cache_ready').value=1
   assert trace(lib,90,15,.5,1)[0]==direct;comparisons+=1
   integer(lib,'mc_render_geometry_limit').value=12288
   integer(lib,'mc_render_cache_limit').value=1
   incremental(lib);assert integer(lib,'mc_render_cache_ready').value==0
   assert integer(lib,'mc_render_cache_error').value==1
   assert integer(lib,'mc_render_geometry_count').value==0
   assert trace(lib,90,15,.5,1)[0]==direct;comparisons+=1
   integer(lib,'mc_render_cache_limit').value=32768
  # Unsupported IDs and render types must fail even after surface overflow.
  # Place the first invalid block at the LAST resident cell so validation must
  # traverse the entire remaining overflow tail rather than stopping early.
  blocks=(ctypes.c_ubyte*(9*32768)).in_dll(lib,'fixture_blocks')
  last=9*32768-1;saved=blocks[last];unsupported_checks=0
  for limit in [32768,1]:
   for bad_id,render_type in [(200,None),(200,99)]:
    blocks[last]=bad_id
    if render_type is not None:lib.fixture_set_present(bad_id,1);lib.fixture_set_type(bad_id,render_type)
    integer(lib,'mc_render_cache_limit').value=limit
    incremental(lib)
    assert lib.mc_render_rebuild_block_error()==1
    assert integer(lib,'mc_render_rebuild_bad_id').value==bad_id
    assert [integer(lib,'mc_render_rebuild_bad_'+axis).value for axis in 'xyz']==[
     lib.fixture_chunk_x(8)*16+15,127,lib.fixture_chunk_z(8)*16+15]
    assert integer(lib,'mc_render_cache_ready').value==0
    assert integer(lib,'mc_render_geometry_count').value==0
    if limit==1:assert integer(lib,'mc_render_cache_error').value==1
    blocks[last]=saved
    if render_type is not None:lib.fixture_set_type(bad_id,0);lib.fixture_set_present(bad_id,0)
    unsupported_checks+=1
  integer(lib,'mc_render_cache_limit').value=32768
  incremental(lib);assert not lib.mc_render_rebuild_block_error()
  assert integer(lib,'mc_render_cache_ready').value==1
  # A real adjacent owned window contains slabs/furnace/iron bars not present
  # in this port's registry. It must fail explicitly, not silently omit them.
  import re
  known={0}|{int(value)for value in re.findall(r'mc_block_register\((\d+),',(PROJECT/'src/mc10_blocks.tc').read_text())}
  cx,cz=original_center[0],original_center[1]+1
  integer(lib,'mc_runtime_center_x').value=cx;integer(lib,'mc_runtime_center_z').value=cz
  first_unknown=None
  for slot in range(9):
   x,z=cx+slot%3-1,cz+slot//3-1;lib.fixture_set_chunk(slot,x,z)
   _,_,level=read_chunk(world/'region'/f'r.{x//32}.{z//32}.mcr',x,z)
   for name,key,size in [('fixture_blocks','Blocks',32768),('fixture_metadata','Data',16384),('fixture_sky','SkyLight',16384),('fixture_light','BlockLight',16384),('fixture_height','HeightMap',256)]:
    array=(ctypes.c_ubyte*(size*9)).in_dll(lib,name);data=tagged(level,key,7)
    ctypes.memmove(ctypes.addressof(array)+slot*size,data,size)
    if key=='Blocks' and first_unknown is None:
     for index,id in enumerate(data):
      if id not in known:first_unknown=(id,x*16+(index>>11),index&127,z*16+((index>>7)&15));break
  assert first_unknown is not None,'owned route no longer exercises unsupported registry guard'
  incremental(lib);assert lib.mc_render_rebuild_block_error()
  assert tuple(integer(lib,name).value for name in ['mc_render_rebuild_bad_id','mc_render_rebuild_bad_x','mc_render_rebuild_bad_y','mc_render_rebuild_bad_z'])==first_unknown
  assert not integer(lib,'mc_render_cache_ready').value and not integer(lib,'mc_render_geometry_count').value
  print(f'Stream render cache: PASS exact surface/run/geometry/center arrays and {comparisons} quad streams; two supported owned layouts, bounded pumps, interleaved Fancy, cancel/restart, exact overflow fallbacks, {unsupported_checks} ID/shape guards incl overflow tails; real owned unsupported block fails explicitly {first_unknown}.')
  import _ctypes;_ctypes.FreeLibrary(lib._handle)
if __name__=='__main__':run()
