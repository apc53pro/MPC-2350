"""Deterministic fixed-pool streaming transitions, errors and owned chunk bytes.

The adapter substitutes only region I/O and the incremental renderer pump.
Actual chunk/nibble/provider code is compiled unchanged. No save is modified.
"""
import ctypes
import importlib.util
import math
import os
from pathlib import Path
import subprocess
import tempfile

CANDIDATE=Path(__file__).resolve().parents[1]
PROJECT=Path(os.environ.get('MC10_PROJECT',r'C:\Users\enzob\OneDrive\Documentos\RP2350_GP_Computer\Firmware\resource_development\programs\mc10_narm'))
STREAM_ROOT=Path(os.environ.get('MC10_STREAM_ROOT',str(PROJECT)))

def integer(lib,name):return ctypes.c_int32.in_dll(lib,name)
def array(lib,name,size):return (ctypes.c_ubyte*size).in_dll(lib,name)
def storage(lib):
 return tuple(bytes(array(lib,name,9*size)) for name,size in [
  ('mc_runtime_blocks',32768),('mc_runtime_metadata',16384),('mc_runtime_skylight',16384),
  ('mc_runtime_blocklight',16384),('mc_runtime_heightmap',256)])
def slots(lib):return [(lib.fixture_slot_x(i),lib.fixture_slot_z(i))for i in range(9)]
def payloads(lib):
 parts=storage(lib)
 return {coords:b''.join(part[i*size:(i+1)*size]for part,size in zip(parts,[32768,16384,16384,16384,256]))for i,coords in enumerate(slots(lib))}
def target(x,z):return {(x+dx,z+dz)for dz in [-1,0,1]for dx in [-1,0,1]}
def ready(lib):
 assert lib.mc_stream_ready()==1
 assert integer(lib,'mc_runtime_access_ok').value==1
 assert integer(lib,'mc_stream_dirty').value==0
 assert lib.mc_runtime_verify_access()==1
def pump(lib):
 previous=integer(lib,'fixture_load_calls').value
 result=lib.mc_stream_step()
 assert integer(lib,'fixture_load_calls').value-previous<=1,'more than one region load in one pump'
 if result!=1:
  assert not lib.mc_stream_ready()
  assert not integer(lib,'mc_runtime_access_ok').value
  assert integer(lib,'mc_stream_dirty').value==1
 return result
def finish(lib):
 for _ in range(100):
  result=pump(lib)
  if result:return result
 raise AssertionError('transition did not terminate')
def validate_pattern(lib):
 for i,(x,z)in enumerate(slots(lib)):
  assert lib.fixture_slot_state(i)==1
  expected=bytes(((x*37+z*73+j*13+(j>>8))&255)for j in range(82176))
  assert payloads(lib)[x,z]==expected,(x,z)
  for field,value in enumerate([x&0xffffffff,z&0xffffffff,x&3,z&3,2,(x^z)&0xffffffff,2,82200]):
   assert lib.fixture_slot_metadata(i,field)==value,(x,z,field)
 assert integer(lib,'mc_runtime_entities').value==sum(x&3 for x,z in slots(lib))
 assert integer(lib,'mc_runtime_tiles').value==sum(z&3 for x,z in slots(lib))
 assert integer(lib,'mc_runtime_ticks').value==18

def run():
 with tempfile.TemporaryDirectory(prefix='mc10-stream-',ignore_cleanup_errors=True)as temporary:
  dll=Path(temporary)/'stream.dll'
  result=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(CANDIDATE/'tests/stream_fixture.c'),'-I',str(STREAM_ROOT/'src'),'-I',str(PROJECT/'src'),'-o',str(dll)],capture_output=True,text=True)
  assert result.returncode==0,result.stderr
  lib=ctypes.CDLL(str(dll));lib.fixture_slot_metadata.restype=ctypes.c_uint32
  lib.fixture_init(0,0);ready(lib);validate_pattern(lib)
  checks=0
  for x,z in [(1,0),(1,1),(-1,-1),(-2,0),(0,0),(-1,0),(0,0)]:
   old_center=(integer(lib,'mc_runtime_center_x').value,integer(lib,'mc_runtime_center_z').value)
   before=payloads(lib);calls=integer(lib,'fixture_load_calls').value
   expected_loads=len(target(x,z)-set(before))
   assert lib.mc_stream_request(x,z)==0
   while integer(lib,'mc_stream_status').value==1:
    assert (integer(lib,'mc_runtime_center_x').value,integer(lib,'mc_runtime_center_z').value)==old_center
    assert pump(lib)==0
   assert integer(lib,'fixture_load_calls').value-calls==expected_loads
   assert finish(lib)==1;ready(lib);validate_pattern(lib)
   assert set(slots(lib))==target(x,z)
   assert all(payloads(lib)[k]==v for k,v in before.items()if k in target(x,z))
   unchanged=(storage(lib),slots(lib),integer(lib,'fixture_load_calls').value,integer(lib,'fixture_cache_begin').value)
   for _ in range(8):assert lib.mc_stream_request(x,z)==1 and pump(lib)==1
   assert unchanged==(storage(lib),slots(lib),integer(lib,'fixture_load_calls').value,integer(lib,'fixture_cache_begin').value)
   checks+=1
  # Every failure can occur after the decoder has overwritten ALL staging bytes;
  # the selected resident slot must remain byte-for-byte and metadata unchanged.
  for status in [-21,-22,-23,-24,-25,-26,-27,-28,-29,-30]:
   lib.fixture_init(0,0);assert lib.mc_stream_request(1,1)==0
   assert pump(lib)==0 # (2,1) center/cardinal success, then fail a later chunk.
   next_x,next_z=1,2
   integer(lib,'fixture_fail_enabled').value=1
   integer(lib,'fixture_fail_x').value=next_x;integer(lib,'fixture_fail_z').value=next_z
   integer(lib,'fixture_fail_status').value=status
   before=(storage(lib),slots(lib))
   assert pump(lib)==status
   assert before==(storage(lib),slots(lib))
   assert integer(lib,'mc_runtime_center_x').value==0
   calls=integer(lib,'fixture_load_calls').value
   for _ in range(3):assert lib.mc_stream_request(1,1)==status and pump(lib)==status
   assert integer(lib,'fixture_load_calls').value==calls
   integer(lib,'fixture_fail_enabled').value=0
   assert lib.mc_stream_retry()==0 and finish(lib)==1;ready(lib);validate_pattern(lib)
   checks+=1
  # Cancellation after each possible I/O/cache boundary never resumes partial
  # old caches; retargeting the old center reloads anything already evicted.
  for stop in range(11):
   lib.fixture_init(0,0);assert lib.mc_stream_request(1,1)==0
   for _ in range(stop):assert pump(lib)==0
   lib.mc_stream_cancel();assert pump(lib)==-44
   assert lib.mc_stream_request(0,0)==0 and finish(lib)==1
   ready(lib);assert set(slots(lib))==target(0,0);validate_pattern(lib);checks+=1
  # Retarget while loading and while cache compilation is incomplete.
  for stop in [1,6,8]:
   lib.fixture_init(0,0);lib.mc_stream_request(1,1)
   for _ in range(stop):assert pump(lib)==0
   assert lib.mc_stream_request(-1,-1)==0 and finish(lib)==1
   ready(lib);assert set(slots(lib))==target(-1,-1);validate_pattern(lib);checks+=1
  # Access validation fails closed even if all nine successful loads exist.
  lib.fixture_init(0,0);lib.mc_stream_request(1,0);integer(lib,'fixture_verify_fail').value=1
  assert finish(lib)==-47;integer(lib,'fixture_verify_fail').value=0
  assert lib.mc_stream_retry()==0 and finish(lib)==1;ready(lib)
  # The renderer's unsupported registry/shape error must be checked BEFORE
  # access_ready, both on initial compilation and a fully loaded new window.
  for move in [False,True]:
   lib.fixture_init(0,0)
   if not move:lib.mc_stream_reset()
   integer(lib,'fixture_block_error').value=1
   assert lib.mc_stream_request(int(move),0)==0 and finish(lib)==-48
   assert integer(lib,'mc_render_rebuild_bad_id').value==200
   assert [integer(lib,'mc_render_rebuild_bad_'+axis).value for axis in 'xyz']==[-513,127,-1]
   before=integer(lib,'fixture_load_calls').value
   for _ in range(4):assert lib.mc_stream_request(int(move),0)==-48 and pump(lib)==-48
   assert integer(lib,'fixture_load_calls').value==before
   integer(lib,'fixture_block_error').value=0
   assert lib.mc_stream_retry()==0 and finish(lib)==1;ready(lib)
   assert integer(lib,'fixture_load_calls').value==before,'retry of validated chunks reloaded SD data'
   checks+=1
  for x,z in [(-1875000,0),(1874999,0),(0,-1875000),(0,1874999),(2147483647,-2147483648)]:
   lib.fixture_init(0,0);before=(storage(lib),slots(lib))
   assert lib.mc_stream_request(x,z)==-45 and pump(lib)==-45
   assert before==(storage(lib),slots(lib));checks+=1
  # Read original owned chunks from the existing save into host-only loader
  # vectors, then move diagonally and re-enter the original saved window.
  import sys
  sys.path.insert(0,str(PROJECT/'tests'))
  from test_region import load_level,read_chunk,tagged
  world=PROJECT/'MC10/saves/New World';info=load_level(world/'level.dat')
  pos=tagged(tagged(info,'Player',10),'Pos',9)[1];cx=math.floor(pos[0])//16;cz=math.floor(pos[2])//16
  route=[(cx,cz),(cx+1,cz),(cx+1,cz+1),(cx,cz)]
  coordinates=sorted(set.union(*(target(x,z)for x,z in route)));assert len(coordinates)<=64
  bank=array(lib,'fixture_bank',64*82176);bank_x=(ctypes.c_int32*64).in_dll(lib,'fixture_bank_x');bank_z=(ctypes.c_int32*64).in_dll(lib,'fixture_bank_z')
  vectors={}
  for i,(x,z)in enumerate(coordinates):
   _,_,level=read_chunk(world/'region'/f'r.{x//32}.{z//32}.mcr',x,z)
   data=b''.join(tagged(level,key,7)for key in ['Blocks','Data','SkyLight','BlockLight','HeightMap'])
   assert len(data)==82176;vectors[x,z]=data;ctypes.memmove(ctypes.addressof(bank)+i*82176,data,len(data));bank_x[i]=x;bank_z[i]=z
  integer(lib,'fixture_bank_count').value=len(coordinates);integer(lib,'fixture_use_bank').value=1
  lib.fixture_init(cx,cz)
  for x,z in route:
   result=lib.mc_stream_request(x,z);assert result==1 or result==0 and finish(lib)==1
   ready(lib);assert payloads(lib)=={k:vectors[k]for k in target(x,z)};checks+=1
  print(f'Streaming: PASS {checks} transitions/failure/cancel/retarget/owned routes; fixed-slot bindings, exact arrays and metadata, one read/pump, fail-closed access, no idle I/O.')
  import _ctypes;_ctypes.FreeLibrary(lib._handle)
if __name__=='__main__':run()
