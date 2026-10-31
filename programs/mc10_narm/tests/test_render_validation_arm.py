"""Actual NARM Thumb check: unsupported block/type after surface overflow."""
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile

HERE=Path(__file__).resolve().parents[1]
PROJECT=Path(os.environ.get('MC10_PROJECT',r'C:\Users\enzob\OneDrive\Documentos\RP2350_GP_Computer\Firmware\resource_development\programs\mc10_narm'))
COMPILER=Path(os.environ.get('MC10_ARM_COMPILER',str(PROJECT.parents[3]/'Compiler_For_NARM/narmcc.exe')))
RENDERER=Path(os.environ.get('MC10_RENDERER',str(PROJECT/'src/mc10_render.tc')))
sys.path.insert(0,r'C:\Users\enzob\Documents\Codex\2026-10-13\co\work\arm_tools')
sys.path.insert(0,str(PROJECT/'tests'))
from player_arm import execute
def run():
 with tempfile.TemporaryDirectory(prefix='mc10-render-validation-arm-',ignore_cleanup_errors=True)as temporary:
  temp=Path(temporary);image=temp/'render_validation.narm'
  (temp/'renderer_under_test.tc').write_text('#include "'+RENDERER.as_posix()+'"\n')
  # Host adapter spelling only: NARMCC accepts this uint hash seed as hex,
  # while its decimal-literal parser rejects values above signed INT_MAX.
  # All production modules remain byte-for-byte unchanged.
  adapter=(PROJECT/'tests/render_fixture.c').read_text().replace('2166136261','0x811c9dc5').replace('../src/',(PROJECT/'src').as_posix()+'/')
  adapter=re.sub(r'float v\[20\]=\{([^}]+)\};',lambda match:'float v[20];'+''.join(f'v[{i}]={value};'for i,value in enumerate(match[1].split(','))),adapter)
  (temp/'render_validation_adapter.c').write_text(adapter)
  result=subprocess.run([str(COMPILER),str(HERE/'tests/render_validation_arm.tc'),'-I',str(temp),'-I',str(PROJECT),'-I',str(PROJECT/'tests'),'-O3','--stack-bytes','32768','-o',str(image)],capture_output=True,text=True)
  assert result.returncode==0,result.stderr+result.stdout
  print(result.stdout.strip(),flush=True)
  result=execute(image)
  assert result==0,('actual NARM Thumb renderer validation failed',result)
  print('NARM Thumb renderer validation: PASS unknown ID and unsupported registered type after surface overflow; exact signed coordinates, cache invalidity, capture-state cleanup and cancel reset.')
if __name__=='__main__':run()
