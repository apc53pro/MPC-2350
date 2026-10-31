"""Build 25 retained mesh comparison, strict schema and safe play handoff."""
import csv
import importlib.util
import io
import subprocess
import tempfile
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]


def run():
    with tempfile.TemporaryDirectory(prefix='mc10-geometry-ab-') as tmp:
        executable = Path(tmp) / 'geometry.exe'
        defines=['-DMC10_PREPARATION_PROFILE=1', '-DMC10_RETAINED_MESH_PROFILE=1']
        built = subprocess.run(['gcc', '-O2', *defines, '-DMC10_GEOMETRY_PROFILE=1', '-DMC10_STAGE_PROFILE=1', '-DMC10_TILE_PROFILE=1',
                                '-DMC10_LEGACY_FONT=1', str(PROJECT / 'tests/benchmark_fixture.c'),
                                '-I', str(PROJECT), '-o', str(executable)], capture_output=True, text=True)
        assert built.returncode == 0, built.stderr
        executed = subprocess.run([str(executable)], capture_output=True, text=True)
        assert executed.returncode == 0, executed.stdout + executed.stderr
    raw = list(csv.reader(io.StringIO(executed.stdout)))
    assert len(raw[0]) == 77 and all(len(row) == 77 for row in raw)
    rows = list(csv.DictReader(io.StringIO(executed.stdout)))
    metadata = {row['key']: row['value'] for row in rows if row['record']=='meta'}
    assert metadata['build']=='25_RETAINED_INDEXED_MESH_LEGACY_FONT'
    assert metadata['schema']=='mc10_retained_mesh_ab_v1'
    assert metadata['preparation_protocol']=='50500001' and metadata['mesh_protocol']=='4d530001'
    assert 'Build 24 optimized' in metadata['comparison_scope']
    assert 'NOT Build 24 release' in metadata['comparison_scope']
    assert 'renderStats(1025) in both' in metadata['mesh_comparison']
    assert 'stage_mode=0 retained mesh off,1 retained mesh on' in metadata['mesh_comparison']
    assert 'zero native glyph calls' in metadata['font_backend']
    assert 'geometry_comparison' not in metadata
    spec = importlib.util.spec_from_file_location('geometry_report', PROJECT / 'tools/mc10_mesh_report.py')
    report = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(report)
    with tempfile.TemporaryDirectory(prefix='geometry-report-') as tmp:
        path = Path(tmp) / 'fixture.csv'
        path.write_text(executed.stdout, encoding='utf-8')
        parsed = report.read_report(path)
        assert len(parsed['comparisons']) == 4
        for scene in report.SCENES:
            comparison = parsed['comparisons'][scene]
            warmup = comparison['untimed_warmup']
            assert comparison['untimed_mesh_warmup']=={'canonical_transforms':120, 'position_cache_hits':280}
            assert warmup['depth_tests'] == 1060 and warmup['depth_rejects'] == 200
            assert warmup['command_bytes'] == 5280 and warmup['triangle_bin_memberships'] == 30
            assert warmup['occupied_strips'] == warmup['core0_strips'] + warmup['core1_strips'] == 10
            assert parsed['views'][scene + '_optimized']['service_unattributed_us'] == 100
            assert parsed['views'][scene + '_optimized']['game_call_overhead_us'] == 100
            assert 'service_us' not in warmup  # Discarded warm-up times never become performance results.
            assert 'paired_delta_ms' in comparison
            assert comparison['paired_speedup']['total_us'] > 1
            assert comparison['paired_delta_ms']['gen_us'] < 0
            assert comparison['paired_delta_ms']['triangle_setup_us'] < 0
            assert parsed['views'][scene + '_baseline']['tile_jobs'] == 5
            assert parsed['views'][scene + '_optimized']['tile_jobs'] == 4
        mutations = [
            ('meta', 'schema', 'value', 'mc10_draw_stages_ab_v1'),
            ('meta', 'geometry_protocol', 'value', '0'),
            ('meta', 'build', 'value', '24_PREPARATION_FIXES_LEGACY_FONT'),
            ('meta', 'preparation_protocol', 'value', '0'),
            ('meta', 'mesh_protocol', 'value', '0'),
            ('meta', 'mesh_warmup_1_canonical_transforms', 'value', '-1'),
            ('meta', 'mesh_warmup_1_position_cache_hits', 'value', '4294967296'),
            ('meta', 'stage_protocol', 'value', '0'),
            ('meta', 'tile_protocol', 'value', '0'),
            ('meta', 'effective_far_plane_blocks', 'value', '16'),
            ('meta', 'effective_resolution', 'value', '160x120 RGB332'),
            ('meta', 'mode', 'value', 'gameplay'),
            ('sample', 'saved_hud_off_optimized', 'color_fnv', '0'),
            ('sample', 'saved_hud_on_optimized', 'depth_fnv', '0'),
            ('sample', 'saved_hud_off_optimized', 'tile_mode', '0'),
            ('sample', 'saved_hud_off_optimized', 'stage_mode', '0'),
            ('sample', 'saved_hud_off_optimized', 'sampler_flags', '1537'),
            ('sample', 'saved_hud_off_baseline', 'sampler_flags', '3073'),
            ('sample', 'saved_hud_off_baseline', 'service_us', '1'),
            ('sample', 'saved_hud_off_optimized', 'depth_rejects', '1'),
            ('sample', 'saved_hud_off_optimized', 'mask_tests', '1'),
            ('sample', 'saved_hud_off_optimized', 'pixel_writes', '1'),
            ('sample', 'saved_hud_off_optimized', 'triangle_bin_memberships', '1'),
            ('sample', 'saved_hud_off_optimized', 'dirty_depth_stores', '1'),
            ('sample', 'saved_hud_off_optimized', 'service_us', '0'),
            ('sample', 'saved_hud_off_optimized', 'service_us', '99999'),
            ('sample', 'saved_hud_off_optimized', 'validation_us', '99999'),
            ('sample', 'saved_hud_off_optimized', 'join_wait_us', '99999'),
            ('sample', 'saved_hud_off_optimized', 'core0_raster_us', '99999'),
            ('sample', 'saved_hud_off_optimized', 'core1_active_us', '99999'),
            ('sample', 'saved_hud_off_optimized', 'depth_tests', '999'),
            ('sample', 'saved_hud_off_optimized', 'sys_hz', '0'),
            ('sample', 'saved_hud_off_optimized', 'service_us', '-1'),
            ('sample', 'saved_hud_off_optimized', 'service_us', '4294967296'),
            ('meta', 'geometry_warmup_1_depth_tests', 'value', '0'),
            ('meta', 'geometry_warmup_1_mask_tests', 'value', '41'),
            ('meta', 'geometry_warmup_1_command_bytes', 'value', '1'),
            ('meta', 'geometry_warmup_1_occupied_strips', 'value', '1'),
            ('meta', 'geometry_warmup_1_bin_read_bytes', 'value', '1'),
            ('meta', 'geometry_warmup_1_bin_reset_bytes', 'value', '1'),
            ('meta', 'geometry_warmup_1_valid_depth_loads', 'value', '1281'),
            ('meta', 'geometry_warmup_1_dirty_depth_stores', 'value', '1281'),
            ('meta', 'geometry_warmup_1_dirty_words', 'value', '321'),
            ('meta', 'geometry_warmup_1_staged_pixels', 'value', '1281'),
            ('meta', 'geometry_warmup_1_dirty_words', 'value', '201'),
            ('meta', 'geometry_warmup_1_dirty_words', 'value', '49'),
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
        for required_key in ('mesh_protocol', 'preparation_protocol', 'mesh_warmup_1_canonical_transforms', 'mesh_warmup_1_position_cache_hits'):
            changed=[row for row in rows if not(row['record']=='meta' and row['key']==required_key)]
            stream=io.StringIO();writer=csv.DictWriter(stream,fieldnames=raw[0])
            writer.writeheader();writer.writerows(changed);path.write_text(stream.getvalue(),encoding='utf-8')
            try:
                report.read_report(path)
            except ValueError:
                pass
            else:
                raise AssertionError('Mesh report accepted missing field: ' + required_key)
        # New validator must reject old schemas even if they share 77 columns.
        old_report_spec=importlib.util.spec_from_file_location('old_geometry_report', PROJECT/'tools/mc10_geometry_report.py')
        old_report=importlib.util.module_from_spec(old_report_spec);old_report_spec.loader.exec_module(old_report)
        path.write_text(executed.stdout,encoding='utf-8')
        try:
            old_report.read_report(path)
        except ValueError:
            pass
        else:
            raise AssertionError('Old geometry validator accepted retained mesh schema')
        # Overlap is intentional: two active workers can sum to more than flush.
        assert 2 * 550 > 700
        path.write_text(executed.stdout, encoding='utf-8')
        report.read_report(path)
    print('Retained mesh profiler PASS: 48 frames/40 paired samples; both optimized submission/native preparation with flags1025; detailed1537 warm-ups and mesh68/69 metadata only; strict77-column mesh protocol; hashes/cancellation/service failures/protocol gates/verified CSV and optimized Enter-to-play handoff')


def test_switch():
    source=(PROJECT/'build_mc10.ps1').read_text()
    assert '[switch]$RetainedMeshProfile' in source
    assert 'if ($RetainedMeshProfile) { $PreparationProfile = $true }' in source
    assert 'mc10_narm_build25_retained_indexed_mesh' in source
    assert "if ($RetainedMeshProfile) { $profileCompilerFlags += '-DMC10_RETAINED_MESH_PROFILE=1' }" in source
    assert 'mc10_mesh_report.py' in source
    # Preserve older release identities and macros; their own fixtures still run.
    assert 'mc10_narm_build24_preparation_fixes' in source and 'mc10_narm_build23_geometry_optimization' in source
    run()


if __name__ == '__main__':
    test_switch()
