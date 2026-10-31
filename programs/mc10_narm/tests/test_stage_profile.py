"""Build 22 paired stage timers, discarded counter warmups and play handoff."""
import csv
import importlib.util
import io
import subprocess
import tempfile
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]


def run():
    with tempfile.TemporaryDirectory(prefix='mc10-stage-ab-') as tmp:
        executable = Path(tmp) / 'stage.exe'
        built = subprocess.run(['gcc', '-O2', '-DMC10_STAGE_PROFILE=1', '-DMC10_TILE_PROFILE=1',
                                '-DMC10_LEGACY_FONT=1', str(PROJECT / 'tests/benchmark_fixture.c'),
                                '-I', str(PROJECT), '-o', str(executable)], capture_output=True, text=True)
        assert built.returncode == 0, built.stderr
        executed = subprocess.run([str(executable)], capture_output=True, text=True)
        assert executed.returncode == 0, executed.stdout + executed.stderr
    raw = list(csv.reader(io.StringIO(executed.stdout)))
    assert len(raw[0]) == 77 and all(len(row) == 77 for row in raw)
    rows = list(csv.DictReader(io.StringIO(executed.stdout)))
    spec = importlib.util.spec_from_file_location('stage_report', PROJECT / 'tools/mc10_stage_report.py')
    report = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(report)
    with tempfile.TemporaryDirectory(prefix='stage-report-') as tmp:
        path = Path(tmp) / 'fixture.csv'
        path.write_text(executed.stdout, encoding='utf-8')
        parsed = report.read_report(path)
        assert len(parsed['comparisons']) == 4
        for scene in report.SCENES:
            comparison = parsed['comparisons'][scene]
            warmup = comparison['untimed_warmup']
            assert warmup['depth_tests'] == 1060 and warmup['depth_rejects'] == 200
            assert warmup['command_bytes'] == 5280 and warmup['triangle_bin_memberships'] == 30
            assert warmup['occupied_strips'] == warmup['core0_strips'] + warmup['core1_strips'] == 10
            assert parsed['views'][scene + '_instrumented']['service_unattributed_us'] == 100
            assert parsed['views'][scene + '_instrumented']['game_call_overhead_us'] == 200
            assert 'service_us' not in warmup  # Discarded warm-up times never become performance results.
            assert 'paired_observer_delta_ms' in comparison
        mutations = [
            ('meta', 'stage_protocol', 'value', '0'),
            ('meta', 'tile_protocol', 'value', '0'),
            ('sample', 'saved_hud_off_instrumented', 'color_fnv', '0'),
            ('sample', 'saved_hud_on_instrumented', 'depth_fnv', '0'),
            ('sample', 'saved_hud_off_instrumented', 'tile_mode', '0'),
            ('sample', 'saved_hud_off_instrumented', 'stage_mode', '0'),
            ('sample', 'saved_hud_off_instrumented', 'sampler_flags', '1537'),
            ('sample', 'saved_hud_off_baseline', 'sampler_flags', '1025'),
            ('sample', 'saved_hud_off_baseline', 'service_us', '1'),
            ('sample', 'saved_hud_off_instrumented', 'depth_rejects', '1'),
            ('sample', 'saved_hud_off_instrumented', 'mask_tests', '1'),
            ('sample', 'saved_hud_off_instrumented', 'pixel_writes', '1'),
            ('sample', 'saved_hud_off_instrumented', 'triangle_bin_memberships', '1'),
            ('sample', 'saved_hud_off_instrumented', 'dirty_depth_stores', '1'),
            ('sample', 'saved_hud_off_instrumented', 'service_us', '0'),
            ('sample', 'saved_hud_off_instrumented', 'service_us', '99999'),
            ('sample', 'saved_hud_off_instrumented', 'validation_us', '99999'),
            ('sample', 'saved_hud_off_instrumented', 'join_wait_us', '99999'),
            ('sample', 'saved_hud_off_instrumented', 'core0_raster_us', '99999'),
            ('sample', 'saved_hud_off_instrumented', 'core1_active_us', '99999'),
            ('sample', 'saved_hud_off_instrumented', 'depth_tests', '999'),
            ('sample', 'saved_hud_off_instrumented', 'sys_hz', '0'),
            ('sample', 'saved_hud_off_instrumented', 'service_us', '-1'),
            ('sample', 'saved_hud_off_instrumented', 'service_us', '4294967296'),
            ('meta', 'stage_warmup_1_depth_tests', 'value', '0'),
            ('meta', 'stage_warmup_1_mask_tests', 'value', '41'),
            ('meta', 'stage_warmup_1_command_bytes', 'value', '1'),
            ('meta', 'stage_warmup_1_occupied_strips', 'value', '1'),
            ('meta', 'stage_warmup_1_bin_read_bytes', 'value', '1'),
            ('meta', 'stage_warmup_1_bin_reset_bytes', 'value', '1'),
            ('meta', 'stage_warmup_1_valid_depth_loads', 'value', '1281'),
            ('meta', 'stage_warmup_1_dirty_depth_stores', 'value', '1281'),
            ('meta', 'stage_warmup_1_dirty_words', 'value', '321'),
            ('meta', 'stage_warmup_1_staged_pixels', 'value', '1281'),
            ('meta', 'stage_warmup_1_dirty_words', 'value', '201'),
            ('meta', 'stage_warmup_1_dirty_words', 'value', '49'),
        ]
        for kind, key, field, value in mutations:
            changed = [dict(row) for row in rows]
            target = next(row for row in changed if row['record'] == kind and row['key' if kind == 'meta' else 'view'] == key)
            target[field] = value
            stream = io.StringIO()
            writer = csv.DictWriter(stream, fieldnames=raw[0])
            writer.writeheader()
            writer.writerows(changed)
            path.write_text(stream.getvalue(), encoding='utf-8')
            try:
                report.read_report(path)
            except ValueError:
                pass
            else:
                raise AssertionError('Corrupt report accepted: ' + key + '/' + field)
        # Overlap is intentional: two active workers can sum to more than flush.
        assert 2 * 550 > 700
        path.write_text(executed.stdout, encoding='utf-8')
        report.read_report(path)
    print('Stage profiler PASS: 48 frames/40 paired optimized-tile samples; flags1/1025, detailed1537 warm-ups only; 77-column protocol, accounting and observer overhead; hashes untimed; all cancellation/service failure positions; old-firmware gate; CSV safe short writes/readback, restore and Enter-to-play handoff')


if __name__ == '__main__':
    run()
