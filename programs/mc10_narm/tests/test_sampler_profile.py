"""Controlled same-image sampler A/B sequencing, protocol and CSV safety."""
import csv
import io
import subprocess
import tempfile
import importlib.util
from pathlib import Path

PROJECT=Path(__file__).resolve().parents[1]

def run():
    with tempfile.TemporaryDirectory(prefix='mc10-sampler-ab-') as tmp:
        exe=Path(tmp)/'sampler.exe'
        built=subprocess.run(['gcc','-O2','-DMC10_SAMPLER_PROFILE=1','-DMC10_LEGACY_FONT=1',str(PROJECT/'tests/benchmark_fixture.c'),'-I',str(PROJECT),'-o',str(exe)],capture_output=True,text=True)
        assert built.returncode==0,built.stderr
        executed=subprocess.run([str(exe)],capture_output=True,text=True)
        assert executed.returncode==0,executed.stdout+executed.stderr
    raw=list(csv.reader(io.StringIO(executed.stdout)))
    assert len(raw[0])==41 and all(len(r)==41 for r in raw)
    rows=list(csv.DictReader(io.StringIO(executed.stdout)))
    meta={r['key']:r['value'] for r in rows if r['record']=='meta'}
    assert meta['schema']=='mc10_sampler_ab_v1' and meta['build']=='20_SAMPLER_AB_LEGACY_FONT'
    assert rows[-1]['key']=='complete' and rows[-1]['value']=='1'
    samples=[r for r in rows if r['record']=='sample'];assert len(samples)==40
    for view in ('saved_hud_on','saved_hud_off','sky_hud_on','sky_hud_off'):
        for mode,flags in (('general',257),('clamped',1)):
            group=[r for r in samples if r['view']==view+'_'+mode]
            assert [r['sample'] for r in group]==list('12345')
            for r in group:
                assert int(r['sampler_flags'])==flags
                assert r['sys_hz']=='408000000' and r['peri_hz']=='102000000'
                assert int(r['clamped_eligible_spans'])==int(r[mode+'_spans'])==100
                assert r['tick_us']==r['physical_ticks']=='0'
                assert int(r['total_us'])==sum(int(r[k]) for k in ('gen_us','draw_us','input_us','tick_us','yield_us','setup_us','ui_us','other_us'))
                assert int(r['ui_us'])==int(r['hud_us'])+int(r['present_us'])
                if 'hud_off' in view:assert r['hud_us']=='0'
    spec=importlib.util.spec_from_file_location('sampler_report',PROJECT/'tools/mc10_sampler_report.py')
    report=importlib.util.module_from_spec(spec);spec.loader.exec_module(report)
    with tempfile.TemporaryDirectory(prefix='sampler-report-') as tmp:
        path=Path(tmp)/'fixture.csv';path.write_text(executed.stdout,encoding='utf-8')
        parsed=report.read_report(path);assert len(parsed['comparisons'])==4
        for original,replacement in [('257,408000000','1,408000000'),('408000000','0'),('53500001','53500000'),('meta'+','*39+'"complete","1"','')]:
            changed=executed.stdout.replace(original,replacement,1)
            assert changed!=executed.stdout,(original,'mutation failed')
            path.write_text(changed,encoding='utf-8')
            try:report.read_report(path)
            except ValueError:pass
            else:raise AssertionError('Invalid report accepted: '+original)
    print('Sampler profiler: PASS 48 frames/40 paired samples, alternating order, warmups, flags/clocks, all 48 cancellation points, firmware gate, restore, CSV columns/counters, post-frame short-write/readback safety; report validation rejects corrupt/incomplete runs')
    return executed.stdout

if __name__=='__main__':run()
