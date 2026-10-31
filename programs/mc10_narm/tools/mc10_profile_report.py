"""Read and validate Build 14/15/17 SD benchmark CSVs; never modifies a log."""
import argparse
import csv
import json
import statistics
from pathlib import Path

TIMINGS=["total_us","gen_us","draw_us","input_us","tick_us","yield_us","setup_us","ui_us","other_us","hud_us","present_us"]
PARTITION=["gen_us","draw_us","input_us","tick_us","yield_us","setup_us","ui_us","other_us"]

def read_report(path):
    with Path(path).open(encoding="utf-8-sig",newline="") as file:
        reader=csv.DictReader(file);rows=list(reader)
    if not rows or any(None in row or any(value is None for value in row.values()) for row in rows):
        raise ValueError("Truncated or inconsistent CSV columns")
    metadata={row["key"]:row["value"] for row in rows if row["record"]=="meta"}
    schema=metadata.get("schema")
    if schema not in ("mc10_autoprofile_v1","mc10_autoprofile_v2") or rows[-1]["key"]!="complete" or rows[-1]["value"]!="1":
        raise ValueError("Unsupported or incomplete benchmark log")
    result={"metadata":metadata,"views":{}}
    views=("saved","sky") if schema=="mc10_autoprofile_v1" else ("saved_hud_on","saved_hud_off","sky_hud_on","sky_hud_off")
    for view in views:
        samples=[row for row in rows if row["record"]=="sample" and row["view"]==view]
        if len(samples)!=5 or [row["sample"] for row in samples]!=list("12345"):
            raise ValueError(f"{view}: expected exactly five ordered samples")
        yaw={row["yaw_bits_hex"] for row in samples};pitch={row["pitch_bits_hex"] for row in samples}
        if len(yaw)!=1 or len(pitch)!=1 or (view.startswith("sky") and pitch!={"c2b40000"}):
            raise ValueError(f"{view}: camera changed or sky pitch was not -90 degrees")
        for row in samples:
            if view.endswith("_hud_off") and int(row["hud_us"]):
                raise ValueError("HUD-off case unexpectedly contains HUD drawing time")
            if int(row["tick_us"]) or int(row["physical_ticks"]):
                raise ValueError("Render-only test unexpectedly executed player ticks")
            if int(row["total_us"])!=sum(int(row[key]) for key in PARTITION):
                raise ValueError("Frame timing partition is inconsistent")
            if int(row["ui_us"])!=int(row["hud_us"])+int(row["present_us"]):
                raise ValueError("HUD/Present do not sum to UI")
        timings={}
        for kind,reduce in (("min",min),("median",statistics.median),("max",max)):
            summaries=[row for row in rows if row["record"]==kind and row["view"]==view]
            if len(summaries)!=1:raise ValueError(f"{view}: missing/duplicate {kind} summary")
            summary=summaries[0]
            for key in TIMINGS:
                value=int(reduce([int(row[key]) for row in samples]))
                if int(summary[key])!=value:raise ValueError(f"{view}/{kind}/{key}: summary differs from raw samples")
                timings.setdefault(key.removesuffix("_us"),{})[kind+"_ms"]=value/1000
        result["views"][view]={"timings":timings,"camera_bits":{"yaw":next(iter(yaw)),"pitch":next(iter(pitch))},"faces":[int(row["faces"]) for row in samples],"visited":[int(row["visited"]) for row in samples],"skipped":[int(row["skipped"]) for row in samples]}
    if schema=="mc10_autoprofile_v2":
        result["hud_ab"]={}
        for scene in ("saved","sky"):
            on=result["views"][scene+"_hud_on"];off=result["views"][scene+"_hud_off"]
            if on["camera_bits"]!=off["camera_bits"]:raise ValueError(f"{scene}: HUD-on/off camera changed")
            for metric in ("faces","visited","skipped"):
                if len(set(on[metric]+off[metric]))!=1:
                    raise ValueError(f"{scene}: HUD-on/off terrain workload changed ({metric})")
            result["hud_ab"][scene]={"median_total_difference_ms":on["timings"]["total"]["median_ms"]-off["timings"]["total"]["median_ms"]}
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("csv",type=Path);parser.add_argument("--json",action="store_true");args=parser.parse_args()
    report=read_report(args.csv)
    if args.json:
        print(json.dumps(report,indent=2));return
    print("Build:",report["metadata"]["build"],"World:",report["metadata"]["world_folder"])
    print("Render-only: ticks paused; settings/inputs recorded in log; no hardware limit inferred.")
    for view,data in report["views"].items():
        print(view.upper(),"timings in ms (min / median / max):")
        for key,values in data["timings"].items():
            print(f"  {key:8} {values['min_ms']:.3f} / {values['median_ms']:.3f} / {values['max_ms']:.3f}")
        print("  Faces:",data["faces"],"Visited:",data["visited"],"Skipped:",data["skipped"])
    if "hud_ab" in report:print("HUD on/off median total differences (ms):",report["hud_ab"])

if __name__=="__main__":main()
