"""Paired tile benchmark, state restoration, service failures and CSV hashes."""
import csv
import io
import subprocess
import tempfile
import importlib.util
from pathlib import Path
PROJECT=Path(__file__).resolve().parents[1]

def run():
    with tempfile.TemporaryDirectory(prefix='mc10-tile-ab-') as tmp:
        exe=Path(tmp)/'tile.exe'
        built=subprocess.run(['gcc','-O2','-DMC10_TILE_PROFILE=1','-DMC10_LEGACY_FONT=1',str(PROJECT/'tests/benchmark_fixture.c'),'-I',str(PROJECT),'-o',str(exe)],capture_output=True,text=True)
        assert built.returncode==0,built.stderr
        executed=subprocess.run([str(exe)],capture_output=True,text=True)
        assert executed.returncode==0,executed.stdout+executed.stderr
    raw=list(csv.reader(io.StringIO(executed.stdout)))
    assert len(raw[0])==51 and all(len(r)==51 for r in raw)
    rows=list(csv.DictReader(io.StringIO(executed.stdout)))
    spec=importlib.util.spec_from_file_location('tile_report',PROJECT/'tools/mc10_tile_report.py')
    report=importlib.util.module_from_spec(spec);spec.loader.exec_module(report)
    with tempfile.TemporaryDirectory(prefix='tile-report-') as tmp:
        path=Path(tmp)/'fixture.csv';path.write_text(executed.stdout,encoding='utf-8')
        parsed=report.read_report(path);assert len(parsed['comparisons'])==4
        for scene in report.SCENES:
            warmup=parsed['comparisons'][scene]['untimed_warmup']
            assert warmup['depth_tests']==1060 and warmup['depth_rejects']==200
            assert warmup['mask_tests']==40 and warmup['pixel_writes']==800
            assert warmup['depth_reject_fraction']==200/1060
            assert warmup['mask_tests_per_masked_candidate']==1
        mutations=[('meta','tile_protocol','value','0'),('sample','saved_hud_off_tiles','color_fnv','0'),('sample','saved_hud_on_tiles','depth_fnv','0'),('sample','saved_hud_off_tiles','tile_mode','0'),('sample','saved_hud_off_tiles','sys_hz','0'),('sample','saved_hud_off_tiles','depth_rejects','1'),('sample','saved_hud_off_tiles','mask_tests','1'),('sample','saved_hud_off_tiles','pixel_writes','1'),('sample','saved_hud_off_tiles','sampler_flags','513'),('sample','saved_hud_off_tiles','depth_tests','999'),('meta','tile_warmup_1_depth_tests','value','0'),('meta','tile_warmup_1_depth_rejects','value','1061'),('meta','tile_warmup_1_mask_tests','value','41'),('meta','tile_warmup_1_pixel_writes','value','1061')]
        for kind,key,field,value in mutations:
            changed=[dict(r) for r in rows]
            target=next(r for r in changed if r['record']==kind and r['key' if kind=='meta' else 'view']==key)
            target[field]=value
            stream=io.StringIO();writer=csv.DictWriter(stream,fieldnames=raw[0]);writer.writeheader();writer.writerows(changed)
            path.write_text(stream.getvalue(),encoding='utf-8')
            try:report.read_report(path)
            except ValueError:pass
            else:raise AssertionError('Corrupt report accepted: '+key+'/'+field)
    print('Tile profiler PASS: 48 frames/40 paired samples; detailed counters only in four discarded tile warm-ups; all measured flags=1 and detailed counters zero; ordering, clocks, hashes outside timed frames; all 48 cancellation and service-failure positions; capability gate; state restore; safe short writes/readback; rejects image/protocol/mode/instrumentation mismatches')

if __name__=='__main__':run()
