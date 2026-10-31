"""Validate and summarize Build 20 same-image sampler A/B CSVs, read-only."""
import argparse
import csv
import json
import statistics
from pathlib import Path

SCENES=('saved_hud_on','saved_hud_off','sky_hud_on','sky_hud_off')
METRICS=('total_us','gen_us','draw_us')
PARTITION=('gen_us','draw_us','input_us','tick_us','yield_us','setup_us','ui_us','other_us')
WORK=('faces','pending','physical_ticks','visited','skipped','surface_entries','geometry_quads','cached_blocks','yaw_bits_hex','pitch_bits_hex')

def require(condition,message):
    if not condition:raise ValueError(message)

def read_report(path):
    with Path(path).open(encoding='utf-8-sig',newline='') as stream:
        reader=csv.DictReader(stream);rows=list(reader)
        require(len(reader.fieldnames or [])==41,'Expected 41 CSV columns')
    require(rows and all(None not in r and all(v is not None for v in r.values()) for r in rows),'Truncated/inconsistent CSV')
    meta={r['key']:r['value'] for r in rows if r['record']=='meta'}
    require(meta.get('schema')=='mc10_sampler_ab_v1' and meta.get('sampler_protocol')=='53500001','Unsupported sampler protocol')
    require(rows[-1]['key']=='complete' and rows[-1]['value']=='1','Incomplete run')
    require(meta.get('resident_loaded')=='9' and meta.get('resident_missing')==meta.get('resident_invalid')=='0','World must contain nine valid resident chunks')
    raw=[r for r in rows if r['record']=='sample'];require(len(raw)==40,'Expected 40 measured samples')
    groups={};result={'metadata':meta,'views':{},'comparisons':{}}
    for scene in SCENES:
        for mode,flags in (('general',257),('clamped',1)):
            view=scene+'_'+mode;group=[r for r in raw if r['view']==view]
            require([r['sample'] for r in group]==list('12345'),f'{view}: missing/duplicate samples')
            groups[view]=group
            for r in group:
                require(int(r['sampler_flags'])==flags,f'{view}: incorrect sampler flags')
                require(int(r['sys_hz'])>0 and int(r['peri_hz'])>0,f'{view}: clocks unavailable')
                require(r['tick_us']==r['physical_ticks']=='0',f'{view}: world ticks executed')
                require(int(r['total_us'])==sum(int(r[k]) for k in PARTITION),f'{view}: inconsistent timing partition')
                require(int(r['ui_us'])==int(r['hud_us'])+int(r['present_us']),f'{view}: inconsistent UI timing')
                if 'hud_off' in scene:require(r['hud_us']=='0',f'{view}: HUD unexpectedly drawn')
                if scene.startswith('sky'):require(r['pitch_bits_hex']=='c2b40000',f'{view}: sky pitch is not -90')
                for suffix in ('spans','candidates'):
                    eligible='clamped_eligible_'+suffix
                    if mode=='general':require(int(r['clamped_'+suffix])==0,f'{view}: baseline used clamped sampler')
                    else:require(int(r['clamped_'+suffix])==int(r[eligible]),f'{view}: eligible spans bypassed optimization')
            result['views'][view]={metric:statistics.median(int(r[metric]) for r in group)/1000 for metric in METRICS}
        a=groups[scene+'_general'];b=groups[scene+'_clamped']
        for i,(old,new) in enumerate(zip(a,b)):
            require(all(old[k]==new[k] for k in WORK+('sys_hz','peri_hz')),f'{scene}: paired scene/workload/clocks differ')
            require({int(old['pair_order']),int(new['pair_order'])}=={0,1},f'{scene}: invalid pair ordering')
            expected=(SCENES.index(scene)+i)&1
            require(int(old['pair_order'])==expected,f'{scene}: pair order did not alternate')
            for suffix in ('spans','candidates'):
                require(int(old['general_'+suffix])==int(new['general_'+suffix])+int(new['clamped_'+suffix]),f'{scene}: sampler coverage changed')
                require(old['wrap16_'+suffix]==new['wrap16_'+suffix] and old['masked_'+suffix]==new['masked_'+suffix] and old['clamped_eligible_'+suffix]==new['clamped_eligible_'+suffix],f'{scene}: fallback/eligible coverage changed')
        require(all(len({r[k] for r in a+b})==1 for k in WORK),f'{scene}: scene changed between samples')
        delta={metric:statistics.median(int(y[metric])-int(x[metric]) for x,y in zip(a,b))/1000 for metric in METRICS}
        covered=sum(int(r[k]) for r in b for k in ('general_candidates','clamped_candidates','wrap16_candidates','masked_candidates'))
        optimized=sum(int(r['clamped_candidates']) for r in b)
        unmasked=covered-sum(int(r['masked_candidates']) for r in b)
        result['comparisons'][scene]={'paired_median_delta_ms':delta,
            'clamped_candidate_share_all_textured_pct':100*optimized/covered if covered else 0,
            'clamped_candidate_share_unmasked_pct':100*optimized/unmasked if unmasked else 0,
            'sys_hz':sorted({int(r['sys_hz']) for r in a+b}),
            'peri_hz':sorted({int(r['peri_hz']) for r in a+b})}
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('csv',type=Path);parser.add_argument('--json',action='store_true');args=parser.parse_args()
    report=read_report(args.csv)
    if args.json:print(json.dumps(report,indent=2));return
    print('40 paired samples validated. Same image/world/settings; configured SDK clocks, not physical frequency measurements.')
    print('Sampler counts describe covered candidates BEFORE depth/mask rejection, not actual texture reads.')
    for scene,data in report['comparisons'].items():
        print(scene)
        for metric in METRICS:
            a=report['views'][scene+'_general'][metric];b=report['views'][scene+'_clamped'][metric]
            print(f'  {metric}: {a:.3f} -> {b:.3f} ms; median paired change {data["paired_median_delta_ms"][metric]:+.3f} ms')
        print(f'  clamped candidate share: {data["clamped_candidate_share_all_textured_pct"]:.2f}% of textured; {data["clamped_candidate_share_unmasked_pct"]:.2f}% of unmasked')
        print('  configured sys/peri Hz:',data['sys_hz'],data['peri_hz'])

if __name__=='__main__':main()
