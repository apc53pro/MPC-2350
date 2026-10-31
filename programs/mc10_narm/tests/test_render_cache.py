"""Compare full emitted quad streams: direct renderer, compact cache and overflow.
Optional --baseline compares against a saved Build 10 renderer as well.
"""
import argparse,ctypes,math,struct,subprocess,tempfile,random
from pathlib import Path
from test_region import load_level,read_chunk,tagged
PROJECT=Path(__file__).resolve().parents[1]
class Pair(ctypes.Structure):_fields_=[('lo',ctypes.c_uint32),('hi',ctypes.c_uint32)]
class Player(ctypes.Structure):
 _fields_=[('present',ctypes.c_int32)]+[(n,Pair)for n in ['pos_x','pos_y','pos_z','motion_x','motion_y','motion_z']]+[('yaw_bits',ctypes.c_uint32),('pitch_bits',ctypes.c_uint32)]
class Options(ctypes.Structure):_fields_=[('gamma',ctypes.c_float),('fancy',ctypes.c_int32)]
def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def pair(v):lo,hi=struct.unpack('<II',struct.pack('<d',v));return Pair(lo,hi)
def integer(lib,n):return ctypes.c_int32.in_dll(lib,n)
def build(tmp,renderer,baseline=False,autoprofile=False):
 root=tmp/('old'if baseline else'new');root.mkdir();dll=root/'render.dll'
 (root/'renderer_under_test.tc').write_text('#include "'+renderer.as_posix()+'"\n')
 result=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(PROJECT/'tests/render_fixture.c'),'-I',str(root),'-I',str(PROJECT),*( ['-DMC_RENDER_BASELINE']if baseline else[]),*( ['-DMC10_AUTOPROFILE=1']if autoprofile else[]),'-o',str(dll)],capture_output=True,text=True)
 assert result.returncode==0,result.stderr
 return ctypes.CDLL(str(dll))
def load(lib,world):
 info=load_level(world/'level.dat');pos=tagged(tagged(info,'Player',10),'Pos',9)[1];cx=math.floor(pos[0])//16;cz=math.floor(pos[2])//16
 lib.fixture_render_init(cx,cz)
 for c in range(9):
  x,z=cx+c%3-1,cz+c//3-1;_,_,level=read_chunk(world/'region'/f'r.{x//32}.{z//32}.mcr',x,z)
  for name,tag,size in [('fixture_blocks','Blocks',32768),('fixture_metadata','Data',16384),('fixture_sky','SkyLight',16384),('fixture_light','BlockLight',16384)]:
   array=(ctypes.c_char*(size*9)).in_dll(lib,name);data=tagged(level,tag,7);ctypes.memmove(ctypes.addressof(array)+c*size,data,size)
 p=Player.in_dll(lib,'mc_runtime_player');p.present=1;p.pos_x=pair(pos[0]);p.pos_y=pair(pos[1]);p.pos_z=pair(pos[2])
 return pos
def trace(lib,yaw,pitch,gamma,fancy):
 p=Player.in_dll(lib,'mc_runtime_player');p.yaw_bits=bits(yaw);p.pitch_bits=bits(pitch)
 o=Options.in_dll(lib,'mc_options');o.gamma=gamma;o.fancy=fancy
 lib.fixture_render_frame();n=integer(lib,'fixture_trace_count').value
 assert integer(lib,'fixture_trace_error').value==0
 array=(ctypes.c_uint32*(65536*22)).in_dll(lib,'fixture_trace')
 return bytes(array)[:n*88],integer(lib,'fixture_block_reads').value,integer(lib,'fixture_light_reads').value,integer(lib,'fixture_clock_calls').value
def run(baseline=None):
 with tempfile.TemporaryDirectory(prefix='mc10-render-',ignore_cleanup_errors=True) as tmp:
  tmp=Path(tmp);lib=build(tmp,PROJECT/'src/mc10_render.tc');old=build(tmp,baseline,True)if baseline else None
  world=PROJECT/'MC10/saves/New World';load(lib,world)
  if old:load(old,world)
  # One compile serves all later cameras and gamma/Fancy settings.
  assert lib.mc_render_build_cache()==1,'cache capacity insufficient for staged world'
  groups=integer(lib,'mc_render_cache_group_count').value;cases=0
  print('Resident surface cache:',groups,'groups;',groups*8,'bytes',flush=True)
  print('Exact geometry cache:',integer(lib,'mc_render_geometry_count').value,'quads;',integer(lib,'mc_render_geometry_blocks').value,'complete cached blocks',flush=True)
  for yaw,pitch in [(0,0),(90,0),(180,25),(-39.125,17.7),(.89453125,-89)]:
   for gamma,fancy in [(0,0),(.5,1),(1,1)]:
    integer(lib,'mc_render_cache_ready').value=0;direct=trace(lib,yaw,pitch,gamma,fancy)
    integer(lib,'mc_render_cache_ready').value=1;cached=trace(lib,yaw,pitch,gamma,fancy)
    if cached[0]!=direct[0]:
     a=struct.unpack('<'+'I'*(len(cached[0])//4),cached[0]);b=struct.unpack('<'+'I'*(len(direct[0])//4),direct[0])
     diff=next((i for i,(v,w)in enumerate(zip(a,b))if v!=w),min(len(a),len(b)))
     raise AssertionError(('cache stream mismatch',yaw,pitch,gamma,fancy,len(a)//22,len(b)//22,'first field',diff,'rows',a[(diff//22)*22:(diff//22+1)*22],b[(diff//22)*22:(diff//22+1)*22]))
    assert cached[2]==0,'cached frame queried world lighting'
    if old:
     original=trace(old,yaw,pitch,gamma,fancy)
     assert original[0]==cached[0],('Build 10 mismatch',yaw,pitch,gamma,fancy)
     if pitch==-89 and gamma==0:
      print('Sky-view service calls: Build 10',original[3],'clock queries; cached',cached[3],'; faces',len(cached[0])//88,flush=True)
      assert cached[3]*20<original[3],'dense per-block clock polling reintroduced'
    if pitch==-89 and gamma==0:
     print('Sky section traversal:',integer(lib,'mc_render_groups_visited').value,'visited;',integer(lib,'mc_render_groups_skipped').value,'skipped',flush=True)
     assert integer(lib,'mc_render_groups_visited').value+integer(lib,'mc_render_groups_skipped').value==groups
    cases+=1
  # Vary eye position, yaw/pitch, near-plane intersections and resident edges.
  rng=random.Random(1300);p=Player.in_dll(lib,'mc_runtime_player')
  original_position=(pair(-150.8718206735353),pair(73.62000000476837),pair(350.6435452798552))
  cx,cz=integer(lib,'mc_runtime_center_x').value,integer(lib,'mc_runtime_center_z').value
  for sample in range(48):
   p.pos_x=pair(cx*16+rng.uniform(-15.5,31.5));p.pos_y=pair(rng.uniform(30,100));p.pos_z=pair(cz*16+rng.uniform(-15.5,31.5))
   yaw,pitch=rng.uniform(-360,360),rng.uniform(-89.9,89.9);gamma,fancy=(sample%3)*.5,sample&1
   integer(lib,'mc_render_cache_ready').value=0;direct=trace(lib,yaw,pitch,gamma,fancy)[0]
   integer(lib,'mc_render_cache_ready').value=1;cached=trace(lib,yaw,pitch,gamma,fancy)[0]
   assert cached==direct,('section/geometry mismatch',sample,yaw,pitch)
   assert integer(lib,'mc_render_groups_visited').value+integer(lib,'mc_render_groups_skipped').value==groups
   cases+=1
  p.pos_x,p.pos_y,p.pos_z=original_position
  # Either optimization can be bypassed independently without changing output.
  expected=trace(lib,0,0,0,1)[0]
  for sections,geometry in [(0,1),(1,0),(0,0)]:
   integer(lib,'mc_render_sections_enabled').value=sections;integer(lib,'mc_render_geometry_enabled').value=geometry
   assert trace(lib,0,0,0,1)[0]==expected
  integer(lib,'mc_render_sections_enabled').value=1;integer(lib,'mc_render_geometry_enabled').value=1
  # Whole-block rollback when a tiny geometry pool fills; no partial block loss.
  integer(lib,'mc_render_geometry_limit').value=1;lib.mc_render_build_geometry()
  assert integer(lib,'mc_render_geometry_count').value<=1
  for gamma,fancy in [(0,0),(.5,1),(1,1)]:
   integer(lib,'mc_render_cache_ready').value=0;direct=trace(lib,90,15,gamma,fancy)[0]
   integer(lib,'mc_render_cache_ready').value=1;assert trace(lib,90,15,gamma,fancy)[0]==direct
  integer(lib,'mc_render_geometry_limit').value=12288;lib.mc_render_build_geometry()
  # Artificial overflow must render exactly through the non-dropping fallback.
  integer(lib,'mc_render_cache_ready').value=0;expected_fallback=trace(lib,0,0,0,0)[0]
  integer(lib,'mc_render_cache_limit').value=1;assert lib.mc_render_build_cache()==0
  assert integer(lib,'mc_render_cache_ready').value==0
  fallback=trace(lib,0,0,0,0);assert fallback[0]==expected_fallback
  if old:assert fallback[0]==trace(old,0,0,0,0)[0]
  print(f'Render cache: PASS {cases} complete bit-identical quad streams (vertices/UV/mask/light/order); independent bypass, tiny geometry pool and surface overflow fallback PASS')
  import _ctypes;_ctypes.FreeLibrary(lib._handle)
  if old:_ctypes.FreeLibrary(old._handle)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--baseline',type=Path);args=p.parse_args();run(args.baseline)
