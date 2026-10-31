"""Compile isolated production data operations and execute actual NARM Thumb.

This is not an RP2350/emulator or native-service test. No hardware or save is
accessed. The image executes without calling any firmware service.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

HERE=Path(__file__).resolve().parents[1]
PROJECT=Path(os.environ.get('MC10_PROJECT',r'C:\Users\enzob\OneDrive\Documentos\RP2350_GP_Computer\Firmware\resource_development\programs\mc10_narm'))
COMPILER=Path(os.environ.get('MC10_ARM_COMPILER',str(PROJECT.parents[3]/'Compiler_For_NARM/narmcc.exe')))
WORKSPACE=Path(r'C:\Users\enzob\Documents\Codex\2026-10-13\co')
sys.path.insert(0,str(WORKSPACE/'work/arm_tools'))
sys.path.insert(0,str(PROJECT/'tests'))
from player_arm import execute

def named(kind,name,payload):
 name=name.encode();return bytes([kind])+struct.pack('>H',len(name))+name+payload
def scalar(kind,name,value):return named(kind,name,struct.pack({1:'>b',2:'>h',3:'>i',5:'>f'}[kind],value))
def stack(slot,item,count,tag=None):
 fields=[scalar(1,'Slot',slot),scalar(2,'id',item),scalar(1,'Count',count),scalar(2,'Damage',0)]
 if tag is not None:fields.append(named(10,'tag',tag))
 return b''.join(fields)+b'\0'
def run():
 special=named(8,'name',b'\0\x03tag')+named(7,'raw',struct.pack('>i',4)+b'\0\xff\x80\x01')+b'\0'
 stacks=[stack(0,262,12,special),stack(9,262,32),stack(100,317,1),stack(103,298,1)]
 fields=[named(9,'Inventory',bytes([10])+struct.pack('>i',len(stacks))+b''.join(stacks)),
  scalar(2,'Health',17),scalar(2,'Air',290),scalar(3,'XpLevel',7),scalar(3,'XpTotal',70),scalar(5,'XpP',.5)]
 player=named(10,'',b''.join(fields)+b'\0');tag=named(10,'tag',special)
 with tempfile.TemporaryDirectory(prefix='mc10-player-data-arm-',ignore_cleanup_errors=True)as temporary:
  temp=Path(temporary);vector=temp/'probe_vectors.tc';image=temp/'player_data_arm.narm'
  vector.write_text('#define PROBE_NBT_BYTES '+str(len(player))+'\n#define PROBE_TAG_BYTES '+str(len(tag))+'\nunsigned char probe_player_nbt[]={'+','.join(hex(v)for v in player)+'};\nunsigned char probe_expected_tag[]={'+','.join(hex(v)for v in tag)+'};\n')
  result=subprocess.run([str(COMPILER),str(HERE/'tests/player_data_arm.tc'),'-I',str(temp),'-I',str(PROJECT),'-I',str(PROJECT/'src'),'-O3','--stack-bytes','32768','-o',str(image)],capture_output=True,text=True)
  assert result.returncode==0,result.stderr+result.stdout
  print(result.stdout.strip(),flush=True)
  status=execute(image)
  assert status==0,('actual NARM Thumb data operation failed',status)
  print('NARM Thumb data probe: PASS synthetic NBT load/tag retention; split/place/merge; armor rejection/value/quick-transfer; two complete 82,176-byte staged commits, fixed pointers, signed coordinates, nibble edges and chunk metadata.')
if __name__=='__main__':run()
