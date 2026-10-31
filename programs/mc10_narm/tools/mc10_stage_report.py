"""Validate Build 22 draw-stage attribution and measure observer overhead.

Only the native service's validation + exclusive setup + flush are serial.
Worker load/raster/commit/active times overlap; join wait is inside flush.
Detailed traffic/depth counters come from discarded warm-ups, not timed rows.
"""
import argparse
import csv
import json
import statistics
from pathlib import Path

SCENES = ('saved_hud_on', 'saved_hud_off', 'sky_hud_on', 'sky_hud_off')
METRICS = ('total_us', 'gen_us', 'draw_us')
PARTITION = ('gen_us', 'draw_us', 'input_us', 'tick_us', 'yield_us', 'setup_us', 'ui_us', 'other_us')
WORK = ('faces', 'pending', 'physical_ticks', 'visited', 'skipped', 'surface_entries', 'geometry_quads',
        'cached_blocks', 'yaw_bits_hex', 'pitch_bits_hex', 'general_spans', 'clamped_spans',
        'wrap16_spans', 'masked_spans', 'general_candidates', 'clamped_candidates',
        'wrap16_candidates', 'masked_candidates', 'depth_tests', 'queued_triangles')
PIXEL = ('depth_tests', 'depth_rejects', 'mask_tests', 'pixel_writes')
STAGES = ('service_us', 'validation_us', 'triangle_setup_us', 'tile_flush_us', 'join_wait_us',
          'core0_load_us', 'core0_raster_us', 'core0_commit_us', 'core0_active_us',
          'core1_load_us', 'core1_raster_us', 'core1_commit_us', 'core1_active_us',
          'list_calls', 'occupied_strips', 'triangle_bin_memberships', 'command_bytes',
          'staged_pixels', 'valid_depth_loads', 'dirty_depth_stores', 'dirty_words',
          'bin_read_bytes', 'bin_reset_bytes', 'core0_strips', 'core1_strips')
TIMES = STAGES[:13]
STRUCTURAL = STAGES[14:]
BASE_COLUMNS = ('record', 'view', 'sample', 'total_us', 'gen_us', 'draw_us', 'input_us', 'tick_us',
                'yield_us', 'setup_us', 'ui_us', 'other_us', 'hud_us', 'present_us', 'faces', 'batches',
                'pending', 'physical_ticks', 'visited', 'skipped', 'surface_entries', 'geometry_quads',
                'cached_blocks', 'yaw_bits_hex', 'pitch_bits_hex', 'general_spans', 'clamped_spans',
                'wrap16_spans', 'masked_spans', 'general_candidates', 'clamped_candidates',
                'wrap16_candidates', 'masked_candidates', 'clamped_eligible_spans',
                'clamped_eligible_candidates', 'sampler_flags', 'sys_hz', 'peri_hz', 'pair_order',
                'tile_mode', 'tile_jobs', 'queued_triangles', 'depth_tests', 'depth_rejects',
                'mask_tests', 'pixel_writes', 'scratch_bytes', 'color_fnv', 'depth_fnv')
COLUMNS = BASE_COLUMNS + ('stage_mode',) + STAGES + ('key', 'value')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(value, label):
    try:
        parsed = int(value)
    except (ValueError, TypeError):
        raise ValueError(f'{label}: expected unsigned integer') from None
    require(0 <= parsed <= 0xffffffff, f'{label}: unsigned integer out of range')
    return parsed


def stage_accounting(values, jobs, label, draw_us=None):
    """Bound independent timer-rounding error, without double-counting workers.

    There are at most a handful of microsecond timer boundaries per list/job.
    Eight microseconds per call/job plus eight fixed is deliberately conservative;
    it is a rounding allowance, not a percentage that can hide missing stages.
    """
    tolerance = 8 * (values['list_calls'] + jobs + 1)
    serial = sum(values[key] for key in ('validation_us', 'triangle_setup_us', 'tile_flush_us'))
    require(serial <= values['service_us'] + tolerance, f'{label}: serial stages exceed service time')
    require(values['join_wait_us'] <= values['tile_flush_us'] + tolerance,
            f'{label}: join wait exceeds enclosing flush')
    for core in (0, 1):
        subtotal = sum(values[f'core{core}_{stage}_us'] for stage in ('load', 'raster', 'commit'))
        active = values[f'core{core}_active_us']
        require(subtotal <= active + tolerance, f'{label}: core{core} stages exceed active time')
        require(active <= values['tile_flush_us'] + tolerance,
                f'{label}: core{core} active time exceeds enclosing flush')
    if draw_us is not None:
        require(values['service_us'] <= draw_us + tolerance, f'{label}: service time exceeds game draw interval')
    return {'service_unattributed_us': max(0, values['service_us'] - serial),
            'game_call_overhead_us': max(0, draw_us - values['service_us']) if draw_us is not None else None,
            'rounding_allowance_us': tolerance}


def read_report(path):
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
        require(tuple(reader.fieldnames or ()) == COLUMNS, 'Expected exact 77-column draw-stage schema')
    require(rows and all(None not in row and all(v is not None for v in row.values()) for row in rows),
            'Truncated/inconsistent CSV')
    require(all(row['record'] in ('meta', 'sample') for row in rows), 'Unknown record type')
    metadata_rows = [row for row in rows if row['record'] == 'meta']
    meta = {row['key']: row['value'] for row in metadata_rows}
    require(len(meta) == len(metadata_rows), 'Duplicate metadata key')
    require(meta.get('schema') == 'mc10_draw_stages_ab_v1' and meta.get('tile_protocol') == '54420001'
            and meta.get('stage_protocol') == '44530001', 'Unsupported draw-stage protocol')
    require(rows[-1]['record'] == 'meta' and rows[-1]['key'] == 'complete' and rows[-1]['value'] == '1',
            'Incomplete run')
    require(meta.get('resident_loaded') == '9' and meta.get('resident_missing') == meta.get('resident_invalid') == '0',
            'World must contain nine valid resident chunks')
    raw = [row for row in rows if row['record'] == 'sample']
    require(len(raw) == 40, 'Expected 40 measured samples')
    require(all(row['view'] in {scene + '_' + mode for scene in SCENES for mode in ('baseline', 'instrumented')}
                for row in raw), 'Unexpected view')
    result = {'metadata': meta, 'views': {}, 'comparisons': {},
              'accounting': 'Validation + exclusive setup + flush are serial; worker times overlap; join wait is nested in flush. Warm-up timings are discarded.'}
    for scene_index, scene in enumerate(SCENES):
        prefix = f'stage_warmup_{scene_index + 1}_'
        warm_keys = PIXEL + ('tile_jobs', 'queued_triangles') + STAGES
        require(all(prefix + key in meta for key in warm_keys), f'{scene}: missing discarded warm-up counters')
        warm = {key: number(meta[prefix + key], prefix + key) for key in warm_keys}
        require(warm['depth_rejects'] + warm['pixel_writes'] <= warm['depth_tests'], f'{scene}: impossible warm-up depth/write counts')
        require(warm['mask_tests'] <= warm['depth_tests'] - warm['depth_rejects'], f'{scene}: impossible warm-up mask count')
        require(warm['occupied_strips'] == warm['core0_strips'] + warm['core1_strips'], f'{scene}: warm-up worker strip counts disagree')
        require(warm['command_bytes'] == warm['triangle_bin_memberships'] * 176, f'{scene}: warm-up command byte count disagrees')
        require(warm['bin_read_bytes'] == warm['tile_jobs'] * 60 * 72, f'{scene}: warm-up bin read bytes disagree')
        require(warm['bin_reset_bytes'] == (warm['list_calls'] + warm['tile_jobs']) * 60 * 72, f'{scene}: warm-up bin reset bytes disagree')
        require(warm['valid_depth_loads'] <= warm['staged_pixels'], f'{scene}: warm-up depth loads exceed staged pixels')
        require(warm['dirty_depth_stores'] <= warm['staged_pixels'], f'{scene}: warm-up dirty depth stores exceed staged pixels')
        require(warm['dirty_words'] <= (warm['staged_pixels'] + 3) // 4, f'{scene}: warm-up dirty words exceed staged words')
        require(warm['staged_pixels'] % 4 == 0, f'{scene}: warm-up staged pixels are not word aligned')
        require(warm['dirty_words'] <= warm['dirty_depth_stores'] <= 4 * warm['dirty_words'],
                f'{scene}: warm-up dirty words/depth stores disagree')
        stage_accounting(warm, warm['tile_jobs'], f'{scene} discarded warm-up')
        groups = []
        for mode, name in enumerate(('baseline', 'instrumented')):
            view = scene + '_' + name
            group = [row for row in raw if row['view'] == view]
            require([row['sample'] for row in group] == list('12345'), f'{view}: missing/duplicate samples')
            groups.append(group)
            accounting = []
            for row in group:
                numeric = {key: number(value, f'{view}/{key}') for key, value in row.items()
                           if key not in ('record', 'view', 'key', 'value', 'yaw_bits_hex', 'pitch_bits_hex')}
                require(numeric['tile_mode'] == 1 and numeric['stage_mode'] == mode
                        and numeric['sampler_flags'] == (1025 if mode else 1), f'{view}: incorrect diagnostic mode')
                require(numeric['sys_hz'] > 0 and numeric['peri_hz'] > 0, f'{view}: clocks unavailable')
                require(numeric['tick_us'] == numeric['physical_ticks'] == 0, f'{view}: world ticks executed')
                require(numeric['total_us'] == sum(numeric[key] for key in PARTITION), f'{view}: inconsistent frame partition')
                require(numeric['ui_us'] == numeric['hud_us'] + numeric['present_us'], f'{view}: inconsistent UI timing')
                require(numeric['scratch_bytes'] == 15360, f'{view}: unexpected SRAM scratch contract')
                require(all(numeric[key] == 0 for key in PIXEL[1:] + STRUCTURAL),
                        f'{view}: detailed instrumentation enabled during measurement')
                if not mode:
                    require(all(numeric[key] == 0 for key in STAGES), f'{view}: baseline contains stage instrumentation')
                else:
                    require(numeric['list_calls'] > 0 or numeric['tile_jobs'] == numeric['queued_triangles'] == 0,
                            f'{view}: tile work without instrumented list calls')
                    accounting.append(stage_accounting(numeric, numeric['tile_jobs'], view, numeric['draw_us']))
                if 'hud_off' in scene:
                    require(numeric['hud_us'] == 0, f'{view}: HUD unexpectedly drawn')
                if scene.startswith('sky'):
                    require(row['pitch_bits_hex'] == 'c2b40000', f'{view}: sky pitch is not -90')
                require(numeric['depth_tests'] == sum(numeric[key] for key in
                        ('general_candidates', 'clamped_candidates', 'wrap16_candidates', 'masked_candidates')),
                        f'{view}: tile candidates disagree with span coverage')
                require(numeric['depth_tests'] == warm['depth_tests'], f'{view}: discarded warm-up coverage differs')
                require(numeric['queued_triangles'] == warm['queued_triangles'], f'{view}: discarded warm-up triangle count differs')
                require(warm['mask_tests'] <= numeric['masked_candidates'], f'{view}: warm-up masks exceed masked candidates')
            result['views'][view] = {key: statistics.median(int(row[key]) for row in group) / 1000
                                     for key in METRICS + TIMES}
            if accounting:
                result['views'][view]['service_unattributed_us'] = statistics.median(item['service_unattributed_us'] for item in accounting)
                result['views'][view]['game_call_overhead_us'] = statistics.median(item['game_call_overhead_us'] for item in accounting)
        a, b = groups
        for index, (baseline, instrumented) in enumerate(zip(a, b)):
            require(all(baseline[key] == instrumented[key] for key in WORK + ('sys_hz', 'peri_hz')),
                    f'{scene}: paired scene/workload/clocks differ')
            require(baseline['depth_fnv'] == instrumented['depth_fnv'], f'{scene}: depth/validity image mismatch')
            if 'hud_off' in scene:
                require(baseline['color_fnv'] == instrumented['color_fnv'], f'{scene}: color image mismatch')
            require({int(baseline['pair_order']), int(instrumented['pair_order'])} == {0, 1}, f'{scene}: invalid pair ordering')
            require(int(baseline['pair_order']) == ((scene_index + index) & 1), f'{scene}: pair order did not alternate')
        require(all(len({row[key] for row in a + b}) == 1 for key in WORK + ('depth_fnv',)), f'{scene}: scene changed between samples')
        if 'hud_off' in scene:
            require(len({row['color_fnv'] for row in a + b}) == 1, f'{scene}: color changed between samples')
        result['comparisons'][scene] = {
            'paired_observer_delta_ms': {key: statistics.median(int(y[key]) - int(x[key]) for x, y in zip(a, b)) / 1000 for key in METRICS},
            'paired_observer_percent': {key: statistics.median(100 * (int(y[key]) - int(x[key])) / max(1, int(x[key])) for x, y in zip(a, b)) for key in METRICS},
            'untimed_warmup': {key: value for key, value in warm.items() if key not in TIMES},
            'hashes_checked': 'depth+validity; color' if 'hud_off' in scene else 'depth+validity (dynamic HUD color excluded)'}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    report = read_report(args.csv)
    if args.json:
        print(json.dumps(report, indent=2))
        return
    print('40 paired samples validated; both modes use optimized tiles. Exact depth hashes in all views and color hashes with HUD off.')
    print(report['accounting'])
    for scene, comparison in report['comparisons'].items():
        print(scene)
        for key in METRICS:
            before = report['views'][scene + '_baseline'][key]
            after = report['views'][scene + '_instrumented'][key]
            print(f'  {key}: {before:.3f} -> {after:.3f} ms; observer delta {comparison["paired_observer_delta_ms"][key]:+.3f} ms ({comparison["paired_observer_percent"][key]:+.2f}%)')
        measured = report['views'][scene + '_instrumented']
        print('  serial service stages, median ms: ' + ', '.join(f'{key}={measured[key]:.3f}' for key in TIMES[:4]))
        print(f'  nested join wait={measured["join_wait_us"]:.3f} ms; service residual={measured["service_unattributed_us"] / 1000:.3f} ms; game-call residual={measured["game_call_overhead_us"] / 1000:.3f} ms')
        for core in (0, 1):
            print(f'  overlapping core{core} median ms: ' + ', '.join(f'{stage}={measured[f"core{core}_{stage}_us"]:.3f}' for stage in ('load', 'raster', 'commit', 'active')))
        warm = comparison['untimed_warmup']
        print(f'  untimed traffic only: memberships={warm["triangle_bin_memberships"]}; command bytes={warm["command_bytes"]}; staged pixels={warm["staged_pixels"]}; valid depth loads={warm["valid_depth_loads"]}; dirty depth stores={warm["dirty_depth_stores"]}; dirty words={warm["dirty_words"]}')


if __name__ == '__main__':
    main()
