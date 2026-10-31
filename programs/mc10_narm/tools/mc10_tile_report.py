"""Validate Build 21 paired textured-tile profiling and exact-image hashes."""
import argparse
import csv
import json
import statistics
from pathlib import Path

SCENES=('saved_hud_on','saved_hud_off','sky_hud_on','sky_hud_off')
METRICS=('total_us','gen_us','draw_us')
PARTITION=('gen_us','draw_us','input_us','tick_us','yield_us','setup_us','ui_us','other_us')
WORK=('faces','pending','physical_ticks','visited','skipped','surface_entries','geometry_quads','cached_blocks','yaw_bits_hex','pitch_bits_hex')
PIXEL_COUNTERS=('depth_tests','depth_rejects','mask_tests','pixel_writes')

def require(condition,message):
    if not condition:raise ValueError(message)

def read_report(path):
    with Path(path).open(encoding='utf-8-sig',newline='') as stream:
        reader=csv.DictReader(stream);rows=list(reader)
        require(len(reader.fieldnames or [])==51,'Expected 51 CSV columns')
    require(rows and all(None not in r and all(v is not None for v in r.values()) for r in rows),'Truncated/inconsistent CSV')
    meta={r['key']:r['value'] for r in rows if r['record']=='meta'}
    require(meta.get('schema')=='mc10_textured_tiles_ab_v1' and meta.get('tile_protocol')=='54420001','Unsupported tile protocol')
    require(rows[-1]['key']=='complete' and rows[-1]['value']=='1','Incomplete run')
    require(meta.get('resident_loaded')=='9' and meta.get('resident_missing')==meta.get('resident_invalid')=='0','World must contain nine valid resident chunks')
    raw=[r for r in rows if r['record']=='sample'];require(len(raw)==40,'Expected 40 measured samples')
    groups={};result={'metadata':meta,'views':{},'comparisons':{}}
    for scene in SCENES:
        prefix=f'tile_warmup_{SCENES.index(scene)+1}_'
        require(all(prefix+field in meta for field in PIXEL_COUNTERS),f'{scene}: missing untimed warm-up counters')
        warmup={field:int(meta[prefix+field]) for field in PIXEL_COUNTERS}
        require(all(0<=value<=0xffffffff for value in warmup.values()),f'{scene}: invalid warm-up counter')
        require(warmup['depth_rejects']+warmup['pixel_writes']<=warmup['depth_tests'],f'{scene}: impossible warm-up depth/write counts')
        require(warmup['mask_tests']<=warmup['depth_tests']-warmup['depth_rejects'],f'{scene}: impossible warm-up mask count')
        for mode,name in enumerate(('immediate','tiles')):
            view=scene+'_'+name;group=[r for r in raw if r['view']==view]
            require([r['sample'] for r in group]==list('12345'),f'{view}: missing/duplicate samples')
            groups[view]=group
            for r in group:
                require(int(r['tile_mode'])==mode and int(r['sampler_flags'])==1,f'{view}: incorrect diagnostic mode')
                require(int(r['sys_hz'])>0 and int(r['peri_hz'])>0,f'{view}: clocks unavailable')
                require(r['tick_us']==r['physical_ticks']=='0',f'{view}: world ticks executed')
                require(int(r['total_us'])==sum(int(r[k]) for k in PARTITION),f'{view}: inconsistent timing partition')
                require(int(r['ui_us'])==int(r['hud_us'])+int(r['present_us']),f'{view}: inconsistent UI timing')
                require(int(r['scratch_bytes'])==15360,f'{view}: unexpected SRAM scratch contract')
                require(r['depth_rejects']==r['mask_tests']==r['pixel_writes']=='0',f'{view}: detailed pixel instrumentation enabled during measurement')
                for field in ('color_fnv','depth_fnv'):
                    require(0<=int(r[field])<=0xffffffff,f'{view}: invalid {field}')
                if 'hud_off' in scene:require(r['hud_us']=='0',f'{view}: HUD unexpectedly drawn')
                if scene.startswith('sky'):require(r['pitch_bits_hex']=='c2b40000',f'{view}: sky pitch is not -90')
                if mode==0:require(r['tile_jobs']==r['queued_triangles']=='0',f'{view}: immediate baseline submitted tile jobs')
                if mode==1:
                    require(int(r['depth_tests'])==sum(int(r[k]) for k in ('general_candidates','clamped_candidates','wrap16_candidates','masked_candidates')),f'{view}: tile candidates disagree with span coverage')
                    require(int(r['depth_tests'])==warmup['depth_tests'],f'{view}: warm-up and measured coverage differ')
                    require(warmup['mask_tests']<=int(r['masked_candidates']),f'{view}: warm-up mask tests exceed masked candidates')
            result['views'][view]={metric:statistics.median(int(r[metric]) for r in group)/1000 for metric in METRICS}
        a=groups[scene+'_immediate'];b=groups[scene+'_tiles']
        for i,(old,new) in enumerate(zip(a,b)):
            require(all(old[k]==new[k] for k in WORK+('sys_hz','peri_hz')),f'{scene}: paired scene/workload/clocks differ')
            require(old['depth_fnv']==new['depth_fnv'],f'{scene}: depth/validity image mismatch')
            if 'hud_off' in scene:require(old['color_fnv']==new['color_fnv'],f'{scene}: color image mismatch')
            require({int(old['pair_order']),int(new['pair_order'])}=={0,1},f'{scene}: invalid pair ordering')
            require(int(old['pair_order'])==((SCENES.index(scene)+i)&1),f'{scene}: pair order did not alternate')
        require(all(len({r[k] for r in a+b})==1 for k in WORK+('depth_fnv',)),f'{scene}: scene changed between samples')
        if 'hud_off' in scene:require(len({r['color_fnv'] for r in a+b})==1,f'{scene}: color changed between samples')
        delta={metric:statistics.median(int(y[metric])-int(x[metric]) for x,y in zip(a,b))/1000 for metric in METRICS}
        result['comparisons'][scene]={'paired_median_delta_ms':delta,
            'total_speedup':statistics.median(int(x['total_us'])/int(y['total_us']) for x,y in zip(a,b)),
            'tile_jobs_median':statistics.median(int(r['tile_jobs']) for r in b),
            'queued_triangles_median':statistics.median(int(r['queued_triangles']) for r in b),
            'untimed_warmup':dict(warmup,
                depth_reject_fraction=warmup['depth_rejects']/max(1,warmup['depth_tests']),
                mask_tests_per_masked_candidate=warmup['mask_tests']/max(1,int(b[0]['masked_candidates']))),
            'hashes_checked':'depth+validity; color' if 'hud_off' in scene else 'depth+validity (dynamic HUD color excluded)'}
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('csv',type=Path);parser.add_argument('--json',action='store_true');args=parser.parse_args()
    report=read_report(args.csv)
    if args.json:print(json.dumps(report,indent=2));return
    print('40 paired samples validated; exact depth hashes in all views and color hashes with HUD off.')
    for scene,data in report['comparisons'].items():
        print(scene)
        for metric in METRICS:
            a=report['views'][scene+'_immediate'][metric];b=report['views'][scene+'_tiles'][metric]
            print(f'  {metric}: {a:.3f} -> {b:.3f} ms; median paired change {data["paired_median_delta_ms"][metric]:+.3f} ms')
        print(f'  frame speedup {data["total_speedup"]:.3f}x; jobs {data["tile_jobs_median"]}; triangles {data["queued_triangles_median"]}')
        warmup=data['untimed_warmup']
        print(f'  untimed tile warm-up only: depth rejects {100*warmup["depth_reject_fraction"]:.1f}%; mask tests / masked candidates {100*warmup["mask_tests_per_masked_candidate"]:.1f}%; pixel writes {warmup["pixel_writes"]}')

if __name__=='__main__':main()
