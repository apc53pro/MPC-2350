"""Checkpoint persistence/error handling and diagnostic legacy-font selection."""
import subprocess
import tempfile
from pathlib import Path
from test_font_batch import run as font_test
PROJECT=Path(__file__).resolve().parents[1]

def run():
    with tempfile.TemporaryDirectory(prefix="mc10-startup-") as tmp:
        exe=Path(tmp)/"startup_trace.exe"
        result=subprocess.run(["gcc","-O2",str(PROJECT/"tests/startup_trace_fixture.c"),"-I",str(PROJECT),"-o",str(exe)],capture_output=True,text=True)
        assert result.returncode==0,result.stderr
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        assert result.returncode==0,result.stdout+result.stderr
        print(result.stdout.strip())
    font_test(legacy=True)

if __name__=="__main__":run()
