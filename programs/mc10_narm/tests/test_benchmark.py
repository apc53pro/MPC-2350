"""Automated two-view benchmark sequencing, CSV statistics and file safety."""
import csv
import io
import statistics
import subprocess
import tempfile
import importlib.util
from pathlib import Path

PROJECT=Path(__file__).resolve().parents[1]

def run(legacy=False):
    with tempfile.TemporaryDirectory(prefix="mc10-benchmark-") as tmp:
        exe=Path(tmp)/"benchmark.exe"
        defines=["-DMC10_LEGACY_FONT=1"] if legacy else []
        result=subprocess.run(["gcc","-O2",*defines,str(PROJECT/"tests/benchmark_fixture.c"),"-I",str(PROJECT),"-o",str(exe)],capture_output=True,text=True)
        assert result.returncode==0,result.stderr
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        assert result.returncode==0,result.stdout+result.stderr
    raw=list(csv.reader(io.StringIO(result.stdout)))
    assert len(raw[0])==27
    assert all(len(row)==len(raw[0]) for row in raw),"inconsistent CSV column count"
    rows=list(csv.DictReader(io.StringIO(result.stdout)))
    metadata={row["key"]:row["value"] for row in rows if row["record"]=="meta"}
    assert metadata["complete"]=="1" and rows[-1]["key"]=="complete"
    assert metadata["level_name"]=='A "quoted", world'
    assert metadata["samples_per_view"]=="5" and metadata["warmup_frames_per_view"]=="1"
    assert metadata["mode"]=="render_only_fixed_player_no_physics_or_world_ticks"
    assert len([key for key in metadata if key.startswith("chunk_")])==63
    assert metadata["schema"]=="mc10_autoprofile_v2"
    assert metadata["build"]==("17_LEGACY_FONT_HUD_AB" if legacy else "15_BUFFERED_GLYPHS_HUD_AB")
    assert metadata["font_backend"]==("original per-pixel path; no native glyph submission; startup cache preparation retained" if legacy else "cached exact 8x8 masks via narm_half_glyph; pixel fallback on service error")
    for view,pitch in [("saved_hud_on","41a00000"),("saved_hud_off","41a00000"),("sky_hud_on","c2b40000"),("sky_hud_off","c2b40000")]:
        samples=[row for row in rows if row["record"]=="sample" and row["view"]==view]
        assert len(samples)==5 and [row["sample"] for row in samples]==list("12345")
        assert all(row["yaw_bits_hex"]=="41200000" and row["pitch_bits_hex"]==pitch for row in samples)
        for row in samples:
            assert row["physical_ticks"]==row["tick_us"]=="0"
            assert int(row["hud_us"])==(0 if view.endswith("off") else 80)
            assert int(row["ui_us"])==int(row["hud_us"])+int(row["present_us"])
            assert int(row["total_us"])==sum(int(row[k]) for k in ["gen_us","draw_us","input_us","tick_us","yield_us","setup_us","ui_us","other_us"])
        for record,reduce in [("min",min),("median",statistics.median),("max",max)]:
            summary=next(row for row in rows if row["record"]==record and row["view"]==view)
            for key in raw[0][3:20]:
                assert int(summary[key])==reduce([int(row[key]) for row in samples]),(view,record,key)
    spec=importlib.util.spec_from_file_location("profile_report",PROJECT/"tools/mc10_profile_report.py")
    report_tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(report_tool)
    with tempfile.TemporaryDirectory(prefix="mc10-report-") as tmp:
        path=Path(tmp)/"fixture.csv";path.write_text(result.stdout,encoding="utf-8")
        report=report_tool.read_report(path);assert len(report["views"])==4 and len(report["hud_ab"])==2
        path.write_text(result.stdout.rsplit("meta",1)[0],encoding="utf-8")
        try:report_tool.read_report(path)
        except ValueError:pass
        else:raise AssertionError("incomplete report accepted")
    print("Auto profiler: PASS four HUD A/B cases, warm-up exclusion, twenty samples, fixed workload, restore/cancel, CSV statistics, post-measure I/O, unique names, short writes, readback and guards")
    if legacy:
        from test_font_batch import run as font_test
        font_test(legacy=True,legacy_define="MC10_LEGACY_FONT")
    return rows

if __name__=="__main__":
    buffered=run();legacy=run(legacy=True)
    # The orchestrator's inputs/measurements differ only in identifying metadata
    # here; real hardware timing will naturally differ with the font backend.
    for rows in (buffered,legacy):
        rows[:]=[row for row in rows if row.get("key") not in ("build","font_backend")]
    assert buffered==legacy,"Legacy-font build changed the benchmark sequence or logged inputs"
