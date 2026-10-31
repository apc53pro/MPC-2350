"""Validate Build 26 three-way Build-24-based backface/occlusion attribution.

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
METRICS = ('total_us', 'gen_us', 'draw_us', 'validation_us', 'triangle_setup_us', 'tile_flush_us')
PARTITION = ('gen_us', 'draw_us', 'input_us', 'tick_us', 'yield_us', 'setup_us', 'ui_us', 'other_us')
WORK = ('pending', 'physical_ticks', 'surface_entries', 'geometry_quads', 'cached_blocks',
        'yaw_bits_hex', 'pitch_bits_hex')
REDUCIBLE = ('faces', 'general_spans', 'clamped_spans', 'wrap16_spans', 'masked_spans',
             'general_candidates', 'clamped_candidates', 'wrap16_candidates',
             'masked_candidates', 'depth_tests', 'queued_triangles')
CULL_FIELDS = ('backface_eligible', 'backface_rejects', 'hiz_triangles_tested', 'hiz_triangles_occluded',
               'hiz_summary_cell_checks', 'hiz_fully_covered_cells_published', 'hiz_numeric_bound_fallbacks')
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


MODES = ('baseline', 'backface', 'combined')
PERMUTATIONS = ('012', '120', '201', '021', '210', '102')
COMPARISONS = (('off_to_backface', 'baseline', 'backface'),
               ('off_to_combined', 'baseline', 'combined'),
               ('backface_to_combined', 'backface', 'combined'))


def read_warmup(meta, scene_index, mode):
    prefix = f'culling_warmup_{mode}_{scene_index + 1}_'
    keys = PIXEL + ('tile_jobs', 'queued_triangles') + STAGES
    warm = {key: number(meta.get(prefix + key), prefix + key) for key in keys}
    label = f'{SCENES[scene_index]}/{mode} discarded warm-up'
    require(warm['depth_rejects'] + warm['pixel_writes'] <= warm['depth_tests'], label + ': impossible depth/write counts')
    require(warm['mask_tests'] <= warm['depth_tests'] - warm['depth_rejects'], label + ': impossible mask count')
    require(warm['occupied_strips'] == warm['core0_strips'] + warm['core1_strips'], label + ': worker strips disagree')
    require(warm['command_bytes'] == warm['triangle_bin_memberships'] * 176, label + ': command bytes disagree')
    require(warm['bin_read_bytes'] == warm['tile_jobs'] * 60 * 72, label + ': bin reads disagree')
    require(warm['bin_reset_bytes'] == (warm['list_calls'] + warm['tile_jobs']) * 60 * 72, label + ': bin resets disagree')
    require(warm['valid_depth_loads'] <= warm['staged_pixels'], label + ': depth loads exceed staged pixels')
    require(warm['dirty_depth_stores'] <= warm['staged_pixels'], label + ': dirty stores exceed staged pixels')
    require(warm['dirty_words'] <= (warm['staged_pixels'] + 3) // 4, label + ': dirty words exceed staged words')
    require(warm['staged_pixels'] % 4 == 0, label + ': staged pixels not word aligned')
    require(warm['dirty_words'] <= warm['dirty_depth_stores'] <= 4 * warm['dirty_words'], label + ': dirty words/stores disagree')
    stage_accounting(warm, warm['tile_jobs'], label)
    culling = {key: number(meta.get(prefix + key), prefix + key) for key in CULL_FIELDS}
    require(culling['backface_rejects'] <= culling['backface_eligible'], label + ': impossible backface count')
    require(culling['hiz_triangles_occluded'] <= culling['hiz_triangles_tested'], label + ': impossible Hi-Z count')
    if mode == 'backface':
        require(all(culling[key] == 0 for key in CULL_FIELDS[2:]), label + ': occlusion counters enabled in backface-only mode')
    return {'traffic': {key: value for key, value in warm.items() if key not in TIMES},
            'culling': culling}


def compare_samples(before, after):
    def values(key, operation):
        return [operation(int(a[key]), int(b[key])) for a, b in zip(before, after)]
    delta = {key: values(key, lambda a, b: (b-a)/1000) for key in METRICS}
    percent = {key: values(key, lambda a, b: 100*(b-a)/max(1,a)) for key in METRICS}
    speedup = {key: values(key, lambda a, b: a/max(1,b)) for key in METRICS}
    return {'paired_delta_ms': {key: statistics.median(value) for key, value in delta.items()},
            'paired_percent': {key: statistics.median(value) for key, value in percent.items()},
            'paired_speedup': {key: statistics.median(value) for key, value in speedup.items()},
            'paired_delta_ms_range': {key: [min(value), max(value)] for key, value in delta.items()},
            'paired_percent_range': {key: [min(value), max(value)] for key, value in percent.items()},
            'paired_speedup_range': {key: [min(value), max(value)] for key, value in speedup.items()},
            'work_delta': {key: statistics.median(int(b[key])-int(a[key]) for a,b in zip(before,after)) for key in REDUCIBLE}}


def read_report(path):
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
        require(tuple(reader.fieldnames or ()) == COLUMNS, 'Expected exact 77-column culling schema')
    require(rows and all(None not in row and all(value is not None for value in row.values()) for row in rows),
            'Truncated/inconsistent CSV')
    require(all(row['record'] in ('meta', 'sample') for row in rows), 'Unknown record type')
    metadata_rows = [row for row in rows if row['record'] == 'meta']
    meta = {row['key']: row['value'] for row in metadata_rows}
    require(len(meta) == len(metadata_rows), 'Duplicate metadata key')
    require(meta.get('schema') == 'mc10_culling_abc_v1' and meta.get('tile_protocol') == '54420001'
            and meta.get('stage_protocol') == '44530001' and meta.get('geometry_protocol') == '47500001',
            'Unsupported culling prerequisite protocol')
    require(meta.get('build') == '26_VISIBILITY_CULLING_LEGACY_FONT', 'Unsupported culling build')
    require(meta.get('preparation_protocol') == '50500001', 'Unsupported preparation protocol')
    require(meta.get('culling_protocol') == '43550001', 'Unsupported visibility culling protocol')
    require(meta.get('culling_modes') == '3' and meta.get('culling_rendered_frames') == '72'
            and meta.get('culling_measured_samples') == '60' and meta.get('culling_warmup_frames') == '12',
            'Expected three modes, 72 frames and 60 samples')
    require('012/120/201/021/210/102' in meta.get('culling_order', ''), 'Missing rotating-triple order contract')
    require(rows[-1]['record'] == 'meta' and rows[-1]['key'] == 'complete' and rows[-1]['value'] == '1',
            'Incomplete run')
    require(meta.get('resident_loaded') == '9' and meta.get('resident_missing') == meta.get('resident_invalid') == '0',
            'World must contain nine valid resident chunks')
    require(meta.get('mode') == 'render_only_fixed_player_no_physics_or_world_ticks',
            'Expected frozen read-only benchmark input')
    require(meta.get('effective_resolution') == '320x240 RGB332'
            and meta.get('effective_projection_scale') == '171.375 horizontal and vertical'
            and meta.get('effective_far_plane_blocks') == '48'
            and meta.get('effective_smooth_lighting') == 'off; flat per-face saved light and directional shading',
            'Unexpected renderer configuration')
    raw = [row for row in rows if row['record'] == 'sample']
    require(len(raw) == 60, 'Expected 60 measured samples')
    require(all(row['view'] in {scene + '_' + mode for scene in SCENES for mode in MODES} for row in raw),
            'Unexpected view')
    result = {'metadata': meta, 'views': {}, 'comparisons': {}, 'warmups': {},
              'accounting': 'All three modes use optimized Build 24 submission/preparation and identical stage timers. Validation + exclusive setup + flush are serial; workers overlap; join wait is nested. Backface/combined warm-ups are discarded. OFF to BF, OFF to BOTH and BF to BOTH use paired samples from each rotating triple; BF to BOTH is marginal occlusion cost/benefit.'}
    for scene_index, scene in enumerate(SCENES):
        result['warmups'][scene] = {mode: read_warmup(meta, scene_index, mode) for mode in MODES[1:]}
        groups = {}
        for mode_index, mode in enumerate(MODES):
            view = scene + '_' + mode
            group = [row for row in raw if row['view'] == view]
            require([row['sample'] for row in group] == list('12345'), f'{view}: missing/duplicate samples')
            groups[mode] = group
            accounting = []
            for sample_index, row in enumerate(group):
                numeric = {key: number(value, f'{view}/{key}') for key,value in row.items()
                           if key not in ('record','view','key','value','yaw_bits_hex','pitch_bits_hex')}
                require(numeric['tile_mode'] == 1 and numeric['stage_mode'] == mode_index
                        and numeric['sampler_flags'] == 1025, f'{view}: incorrect diagnostic mode')
                require(numeric['pair_order'] == (scene_index*5+sample_index)%6, f'{view}: incorrect rotating-triple permutation')
                require(numeric['sys_hz'] > 0 and numeric['peri_hz'] > 0, f'{view}: clocks unavailable')
                require(numeric['tick_us'] == numeric['physical_ticks'] == 0, f'{view}: world ticks executed')
                require(numeric['total_us'] == sum(numeric[key] for key in PARTITION), f'{view}: inconsistent frame partition')
                require(numeric['ui_us'] == numeric['hud_us'] + numeric['present_us'], f'{view}: inconsistent UI timing')
                require(numeric['scratch_bytes'] == 15360, f'{view}: unexpected SRAM scratch contract')
                require(all(numeric[key] == 0 for key in PIXEL[1:] + STRUCTURAL),
                        f'{view}: detailed instrumentation enabled during measurement')
                require(numeric['list_calls'] > 0 or numeric['tile_jobs'] == numeric['queued_triangles'] == 0,
                        f'{view}: tile work without instrumented calls')
                accounting.append(stage_accounting(numeric, numeric['tile_jobs'], view, numeric['draw_us']))
                if 'hud_off' in scene:
                    require(numeric['hud_us'] == 0, f'{view}: HUD unexpectedly drawn')
                if scene.startswith('sky'):
                    require(row['pitch_bits_hex'] == 'c2b40000', f'{view}: sky pitch is not -90')
                require(numeric['depth_tests'] == sum(numeric[key] for key in
                        ('general_candidates','clamped_candidates','wrap16_candidates','masked_candidates')),
                        f'{view}: candidates disagree with span coverage')
                require(numeric['visited'] + numeric['skipped'] == numeric['surface_entries'],
                        f'{view}: surface traversal accounting differs')
            measured = {key: statistics.median(int(row[key]) for row in group)/1000
                        for key in dict.fromkeys(METRICS + TIMES)}
            for key in ('tile_jobs','list_calls'):
                measured[key] = statistics.median(int(row[key]) for row in group)
            for key in ('service_unattributed_us','game_call_overhead_us'):
                measured[key] = statistics.median(item[key] for item in accounting)
            result['views'][view] = measured
        combined_rows = sum(groups.values(), [])
        require(all(len({row[key] for row in combined_rows}) == 1 for key in WORK + ('depth_fnv',)),
                f'{scene}: scene or depth changed between samples/modes')
        if 'hud_off' in scene:
            require(len({row['color_fnv'] for row in combined_rows}) == 1, f'{scene}: color changed between samples/modes')
        result['comparisons'][scene] = {}
        for comparison, before_mode, after_mode in COMPARISONS:
            before,after = groups[before_mode],groups[after_mode]
            for a,b in zip(before,after):
                require(all(a[key] == b[key] for key in WORK + ('sys_hz','peri_hz','pair_order')),
                        f'{scene}/{comparison}: triple input/clocks/order differ')
                require(a['depth_fnv'] == b['depth_fnv'], f'{scene}/{comparison}: depth image mismatch')
                if 'hud_off' in scene:
                    require(a['color_fnv'] == b['color_fnv'], f'{scene}/{comparison}: color image mismatch')
                require(all(int(b[key]) <= int(a[key]) for key in REDUCIBLE),
                        f'{scene}/{comparison}: culling increased submitted/raster work')
            result['comparisons'][scene][comparison] = compare_samples(before,after)
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
    print('60 three-way culling samples validated: OFF, backface only, combined. Depth hashes match all views; HUD-hidden color hashes match.')
    print(report['accounting'])
    for scene in SCENES:
        print(scene)
        for comparison, before_mode, after_mode in COMPARISONS:
            measured = report['comparisons'][scene][comparison]
            print('  ' + comparison)
            for key in METRICS:
                before = report['views'][scene+'_'+before_mode][key]
                after = report['views'][scene+'_'+after_mode][key]
                lo,hi = measured['paired_delta_ms_range'][key]
                print(f'    {key}: {before:.3f} -> {after:.3f} ms; paired delta {measured["paired_delta_ms"][key]:+.3f} ms ({measured["paired_percent"][key]:+.2f}%); range {lo:+.3f}..{hi:+.3f} ms; {measured["paired_speedup"][key]:.3f}x')
        for mode in MODES[1:]:
            counts = report['warmups'][scene][mode]['culling']
            print('  discarded ' + mode + ' counters only: ' + ', '.join(f'{key}={value}' for key,value in counts.items()))
        if not report['warmups'][scene]['combined']['culling']['hiz_triangles_occluded']:
            print('  No whole triangles were occluded in the discarded combined warm-up; do not infer an occlusion rejection benefit from this view.')


if __name__ == '__main__':
    main()
