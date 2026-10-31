"""Exercise unchanged production input/timer/pause loop with NARM service stubs."""
import os,subprocess,tempfile
from pathlib import Path
PROJECT=Path(__file__).resolve().parents[1]
PRODUCTION=Path(os.environ.get('MC10_PROJECT',str(PROJECT)))
def run():
 with tempfile.TemporaryDirectory(prefix='mc10-game-') as tmp:
  tmp=Path(tmp);source=(PRODUCTION/'src/mc10_options.tc').read_text()
  start=source.index('int mc_options_narm_to_lwjgl');brace=source.index('{',start);end=brace+1;depth=1
  while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
  key_map=source[start:end].replace('{','{++fixture_translations;',1)
  (tmp/'key_map.tc').write_text(key_map,encoding='utf-8')
  compiler_root=PRODUCTION.parents[3]/'Compiler_For_NARM'
  for defines in ([],['-DMC10_TILE_PROFILE'],['-DMC10_TILE_PROFILE','-DMC10_INTERACTIVE']):
   exe=tmp/('game'+str(len(defines))+'.exe')
   result=subprocess.run(['gcc','-O2',*defines,str(PROJECT/'tests/game_fixture.c'),'-I',str(tmp),'-I',str(compiler_root),'-o',str(exe)],capture_output=True,text=True)
   assert result.returncode==0,result.stderr
   subprocess.run([str(exe)],check=True)
  print('Game loop: PASS input/ticks/pause/options/F3, pending-batch boundary, saved culling choices and interactive FPS-only startup (3 configurations)')
if __name__=='__main__':run()
