"""Build 26 culling comparison from archived Build 24, strict hashes and safe play."""
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
        defines=['-DMC10_PREPARATION_PROFILE=1', '-DMC10_CULL_PROFILE=1']
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
    assert metadata['build']=='26_VISIBILITY_CULLING_LEGACY_FONT'
    assert metadata['schema']=='mc10_culling_abc_v1'
    assert metadata['preparation_protocol']=='50500001' and metadata['culling_protocol']=='43550001'
    assert 'based on Build 24' in metadata['comparison_scope']
    assert 'NOT Build 24 release' in metadata['comparison_scope']
    assert 'renderStats(1025) in all' in metadata['culling_comparison']
    assert 'worker2' in metadata['culling_comparison'] and 'worker258' in metadata['culling_comparison']
    assert metadata['culling_modes']=='3' and metadata['culling_measured_samples']=='60'
    assert metadata['culling_rendered_frames']=='72' and metadata['culling_warmup_frames']=='12'
    assert '012/120/201/021/210/102' in metadata['culling_order']
    assert 'zero native glyph calls' in metadata['font_backend']
    assert 'geometry_comparison' not in metadata
    spec = importlib.util.spec_from_file_location('geometry_report', PROJECT / 'tools/mc10_cull_report.py')
    report = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(report)
    with tempfile.TemporaryDirectory(prefix='geometry-report-') as tmp:
        path = Path(tmp) / 'fixture.csv'
        path.write_text(executed.stdout, encoding='utf-8')
        parsed = report.read_report(path)
        assert len(parsed['comparisons']) == 4
        for scene in report.SCENES:
            assert set(parsed['comparisons'][scene])=={'off_to_backface','off_to_combined','backface_to_combined'}
            for mode, expected_depth, expected_native in (('backface',960,(0,0,0,0,0)),('combined',860,(40,10,80,20,1))):
                warmup=parsed['warmups'][scene][mode]['traffic']
                culling=parsed['warmups'][scene][mode]['culling']
                assert culling==dict(zip(report.CULL_FIELDS,(20,6)+expected_native))
                assert warmup['depth_tests']==expected_depth and warmup['depth_rejects']==200
                assert warmup['command_bytes']==5280 and warmup['triangle_bin_memberships']==30
                assert 'service_us' not in warmup
            for comparison in parsed['comparisons'][scene].values():
                assert comparison['paired_speedup']['total_us']>1
                for key in report.METRICS:
                    assert comparison['paired_delta_ms_range'][key][0] <= comparison['paired_delta_ms'][key] <= comparison['paired_delta_ms_range'][key][1]
                    assert comparison['paired_percent_range'][key][0] <= comparison['paired_percent'][key] <= comparison['paired_percent_range'][key][1]
            assert parsed['comparisons'][scene]['off_to_backface']['work_delta']['faces']==-6
            assert parsed['comparisons'][scene]['off_to_combined']['work_delta']['depth_tests']==-200
            assert parsed['comparisons'][scene]['backface_to_combined']['work_delta']['faces']==0
            assert parsed['comparisons'][scene]['backface_to_combined']['work_delta']['depth_tests']==-100
            assert parsed['views'][scene+'_baseline']['tile_jobs']==5
            assert parsed['views'][scene+'_backface']['tile_jobs']==4
            assert parsed['views'][scene+'_combined']['tile_jobs']==3
            assert parsed['views'][scene+'_combined']['service_unattributed_us']==100
            assert parsed['views'][scene+'_combined']['game_call_overhead_us']==100
        mutations = [
            ('meta', 'schema', 'value', 'mc10_culling_ab_v1'),
            ('meta', 'culling_measured_samples', 'value', '40'),
            ('meta', 'culling_warmup_frames', 'value', '8'),
            ('meta', 'culling_modes', 'value', '2'),
            ('meta', 'culling_order', 'value', 'alternating pairs'),
            ('meta', 'culling_warmup_backface_1_hiz_triangles_tested', 'value', '1'),
            ('sample', 'saved_hud_off_backface', 'sampler_flags', '3073'),
            ('sample', 'saved_hud_off_backface', 'stage_mode', '2'),
            ('sample', 'saved_hud_off_combined', 'pair_order', '6'),
            ('sample', 'saved_hud_off_backface', 'color_fnv', '0'),
            ('meta', 'geometry_protocol', 'value', '0'),
            ('meta', 'build', 'value', '24_PREPARATION_FIXES_LEGACY_FONT'),
            ('meta', 'preparation_protocol', 'value', '0'),
            ('meta', 'culling_protocol', 'value', '4d530001'),
            ('meta', 'culling_warmup_combined_1_backface_rejects', 'value', '21'),
            ('meta', 'culling_warmup_combined_1_hiz_triangles_occluded', 'value', '41'),
            ('meta', 'culling_warmup_combined_1_hiz_summary_cell_checks', 'value', '-1'),
            ('meta', 'culling_warmup_combined_1_hiz_numeric_bound_fallbacks', 'value', '4294967296'),
            ('meta', 'stage_protocol', 'value', '0'),
            ('meta', 'tile_protocol', 'value', '0'),
            ('meta', 'effective_far_plane_blocks', 'value', '16'),
            ('meta', 'effective_resolution', 'value', '160x120 RGB332'),
            ('meta', 'mode', 'value', 'gameplay'),
            ('sample', 'saved_hud_off_combined', 'color_fnv', '0'),
            ('sample', 'saved_hud_on_combined', 'depth_fnv', '0'),
            ('sample', 'saved_hud_off_combined', 'tile_mode', '0'),
            ('sample', 'saved_hud_off_combined', 'stage_mode', '0'),
            ('sample', 'saved_hud_off_combined', 'sampler_flags', '1537'),
            ('sample', 'saved_hud_off_baseline', 'sampler_flags', '3073'),
            ('sample', 'saved_hud_off_baseline', 'service_us', '1'),
            ('sample', 'saved_hud_off_combined', 'depth_rejects', '1'),
            ('sample', 'saved_hud_off_combined', 'mask_tests', '1'),
            ('sample', 'saved_hud_off_combined', 'pixel_writes', '1'),
            ('sample', 'saved_hud_off_combined', 'triangle_bin_memberships', '1'),
            ('sample', 'saved_hud_off_combined', 'dirty_depth_stores', '1'),
            ('sample', 'saved_hud_off_combined', 'service_us', '0'),
            ('sample', 'saved_hud_off_combined', 'service_us', '99999'),
            ('sample', 'saved_hud_off_combined', 'validation_us', '99999'),
            ('sample', 'saved_hud_off_combined', 'join_wait_us', '99999'),
            ('sample', 'saved_hud_off_combined', 'core0_raster_us', '99999'),
            ('sample', 'saved_hud_off_combined', 'core1_active_us', '99999'),
            ('sample', 'saved_hud_off_combined', 'depth_tests', '999'),
            ('sample', 'saved_hud_off_combined', 'sys_hz', '0'),
            ('sample', 'saved_hud_off_combined', 'service_us', '-1'),
            ('sample', 'saved_hud_off_combined', 'service_us', '4294967296'),
            ('meta', 'culling_warmup_combined_1_depth_tests', 'value', '0'),
            ('meta', 'culling_warmup_combined_1_mask_tests', 'value', '761'),
            ('meta', 'culling_warmup_combined_1_command_bytes', 'value', '1'),
            ('meta', 'culling_warmup_combined_1_occupied_strips', 'value', '1'),
            ('meta', 'culling_warmup_combined_1_bin_read_bytes', 'value', '1'),
            ('meta', 'culling_warmup_combined_1_bin_reset_bytes', 'value', '1'),
            ('meta', 'culling_warmup_combined_1_valid_depth_loads', 'value', '1281'),
            ('meta', 'culling_warmup_combined_1_dirty_depth_stores', 'value', '1281'),
            ('meta', 'culling_warmup_combined_1_dirty_words', 'value', '321'),
            ('meta', 'culling_warmup_combined_1_staged_pixels', 'value', '1281'),
            ('meta', 'culling_warmup_combined_1_dirty_words', 'value', '201'),
            ('meta', 'culling_warmup_combined_1_dirty_words', 'value', '49'),
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
        for required_key in ('culling_protocol', 'preparation_protocol') + tuple('culling_warmup_'+mode+'_1_'+key for mode in ('backface','combined') for key in report.CULL_FIELDS):
            changed=[row for row in rows if not(row['record']=='meta' and row['key']==required_key)]
            stream=io.StringIO();writer=csv.DictWriter(stream,fieldnames=raw[0])
            writer.writeheader();writer.writerows(changed);path.write_text(stream.getvalue(),encoding='utf-8')
            try:
                report.read_report(path)
            except ValueError:
                pass
            else:
                raise AssertionError('Culling report accepted missing field: ' + required_key)
        # New validator must not weaken old schemas, which still mean identical work.
        old_spec=importlib.util.spec_from_file_location('old_geometry_report', PROJECT/'tools/mc10_geometry_report.py')
        old_report=importlib.util.module_from_spec(old_spec);old_spec.loader.exec_module(old_report)
        path.write_text(executed.stdout,encoding='utf-8')
        try:
            old_report.read_report(path)
        except ValueError:
            pass
        else:
            raise AssertionError('Old geometry validator accepted culling schema')
        # A conservative rejection's work can change with 8ms flush boundaries.
        # Warm-up coverage and candidate-to-candidate work need not be identical.
        changed=[dict(row) for row in rows]
        target=next(row for row in changed if row['record']=='sample' and row['view']=='saved_hud_off_combined')
        target['clamped_candidates']=str(int(target['clamped_candidates'])-1)
        target['depth_tests']=str(int(target['depth_tests'])-1)
        stream=io.StringIO();writer=csv.DictWriter(stream,fieldnames=raw[0])
        writer.writeheader();writer.writerows(changed);path.write_text(stream.getvalue(),encoding='utf-8')
        report.read_report(path)
        # Candidate work cannot exceed its paired baseline.
        target['clamped_candidates']='901';target['depth_tests']='961'
        stream=io.StringIO();writer=csv.DictWriter(stream,fieldnames=raw[0])
        writer.writeheader();writer.writerows(changed);path.write_text(stream.getvalue(),encoding='utf-8')
        try:
            report.read_report(path)
        except ValueError:
            pass
        else:
            raise AssertionError('Culling report accepted increased raster work')
        # Zero rejection activity is valid, but must not be called an occlusion win.
        changed=[dict(row) for row in rows]
        for row in changed:
            if row['record']=='meta' and row['key'].startswith(('culling_warmup_backface_','culling_warmup_combined_')):
                row['value']='0'
        stream=io.StringIO();writer=csv.DictWriter(stream,fieldnames=raw[0])
        writer.writeheader();writer.writerows(changed);path.write_text(stream.getvalue(),encoding='utf-8')
        zero_report=report.read_report(path)
        assert not any(zero_report['warmups']['saved_hud_off']['combined']['culling'].values())
        # Overlap is intentional: two active workers can sum to more than flush.
        assert 2 * 550 > 700
        path.write_text(executed.stdout, encoding='utf-8')
        report.read_report(path)
    numeric_bound=int(executed.stderr.strip().split('abc_numeric_bound_bytes=')[-1])
    assert len(executed.stdout.encode()) < numeric_bound < 98304
    print('Culling ABC profiler PASS: 72 frames/60 samples, OFF/BF/BOTH balanced six-permutation triples; equal flags1025 and optimized submit; both feature warm-ups1537 with BF native Hi-Z counters zero; strict77-column ABCschema/protocol70/hashes/immutableinput and all3 paired marginal comparisons; 72 cancellation/servicefail points, CSV96KiB guard/readback and Enter3/F3 handoff; CSV bytes='+str(len(executed.stdout.encode()))+' numeric-width bound='+str(numeric_bound))


def test_switch():
    source=(PROJECT/'build_mc10.ps1').read_text()
    assert '[switch]$CullProfile' in source
    assert 'if ($CullProfile) { $PreparationProfile = $true }' in source
    assert 'mc10_narm_build26_visibility_culling' in source
    assert "if ($CullProfile) { $profileCompilerFlags += '-DMC10_CULL_PROFILE=1' }" in source
    assert 'mc10_cull_report.py' in source
    assert 'mc10_narm_build24_preparation_fixes' in source and 'mc10_narm_build23_geometry_optimization' in source
    assert 'test_backface_culling.py' in source
    assert 'RetainedMesh' not in source and 'MC10_RETAINED_MESH' not in source
    benchmark=(PROJECT/'src/mc10_benchmark.tc').read_text()
    assert '#ifndef MC10_CULL_PROFILE' in benchmark
    assert 'mc_render_cull_mode=0;mc_render_cull_diagnostics=0;' in benchmark
    run()


if __name__ == '__main__':
    test_switch()
