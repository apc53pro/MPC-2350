"""Compile/run the production guarded game-loop state machine with host services."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
PROJECT=Path(os.environ.get('MC10_PROJECT','C:/Users/enzob/OneDrive/Documentos/RP2350_GP_Computer/Firmware/resource_development/programs/mc10_narm'))
def run():
    with tempfile.TemporaryDirectory(prefix='mc10-world-game-',ignore_cleanup_errors=True)as temp:
        tmp=Path(temp)
        provider=(PROJECT/'tests/stream_fixture.c').read_text().replace('struct MCWorldInfo {int hardcore,game_type;};','struct MCWorldInfo {int hardcore,game_type;uint random_seed_lo,random_seed_hi;};')
        (tmp/'stream_fixture.c').write_text(provider)
        (tmp/'game_under_test.tc').write_text((PROJECT/'src/mc10_game.tc').read_text())
        (tmp/'profile_under_test.tc').write_text((PROJECT/'src/mc10_profile.tc').read_text())
        options=(PROJECT/'src/mc10_options.tc').read_text();match=re.search(r'int mc_options_narm_to_lwjgl\([^\n]*\)\s*\{',options);assert match
        end=match.end();level=1
        while level:level+=(options[end]=='{')-(options[end]=='}');end+=1
        (tmp/'key_map.tc').write_text(options[match.start():end])
        exe=tmp/'world_game.exe';result=subprocess.run(['gcc','-O2','-fno-strict-aliasing',str(HERE/'world_game_fixture.c'),'-I',str(tmp),'-I',str(PROJECT),'-I',str(PROJECT/'src'),'-o',str(exe)],capture_output=True,text=True);assert result.returncode==0,result.stderr
        result=subprocess.run([str(exe)],capture_output=True,text=True);assert result.returncode==0,result.stdout+result.stderr
        print(result.stdout.strip())
if __name__=='__main__':run()

