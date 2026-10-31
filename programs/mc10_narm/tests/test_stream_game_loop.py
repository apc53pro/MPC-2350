"""Exercise actual integrated game + fixed-pool streaming with host services."""
import os
from pathlib import Path
import subprocess
import tempfile

CANDIDATE=Path(__file__).resolve().parents[1]
PROJECT=Path(os.environ.get('MC10_PROJECT',r'C:\Users\enzob\OneDrive\Documentos\RP2350_GP_Computer\Firmware\resource_development\programs\mc10_narm'))
STREAM_ROOT=Path(os.environ.get('MC10_STREAM_ROOT',str(PROJECT)))
def run():
 with tempfile.TemporaryDirectory(prefix='mc10-stream-game-')as temporary:
  temp=Path(temporary)
  source=(PROJECT/'src/mc10_options.tc').read_text()
  start=source.index('int mc_options_narm_to_lwjgl');end=source.index('{',start)+1;depth=1
  while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
  (temp/'key_map.tc').write_text(source[start:end])
  for generated,actual in [('game_under_test.tc','mc10_game.tc'),('profile_under_test.tc','mc10_profile.tc')]:
   (temp/generated).write_text('#include "'+(PROJECT/'src'/actual).as_posix()+'"\n')
  exe=temp/'stream_game.exe'
  result=subprocess.run(['gcc','-O2','-fno-strict-aliasing',str(CANDIDATE/'tests/stream_game_fixture.c'),'-I',str(temp),'-I',str(PROJECT),'-I',str(STREAM_ROOT/'src'),'-I',str(PROJECT/'src'),'-o',str(exe)],capture_output=True,text=True)
  assert result.returncode==0,result.stderr
  subprocess.run([str(exe)],check=True)
if __name__=='__main__':run()
