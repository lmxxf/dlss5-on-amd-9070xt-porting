#!/usr/bin/env python3
"""
Analyze `r3a_r2d_4arm` (4-arm R2-D component ablation on R3-A + K5 baseline)
for both `small1080` and `original1088`.
Arms:
- A0_NoR2D: R3-A + K5 (no R2-D)
- A1_R2D_C32: R3-A + K5 + R2-D in C32 only
- A2_R2D_MH: R3-A + K5 + R2-D in MH/SP only
- A3_R2D_Both: 1005424b (R3-A + K5 + R2-D in both C32 and MH/SP)
"""
from pathlib import Path
import csv, json, re
import numpy as np

p = Path(__file__).parent
ev = p / "r3a_r2d_4arm/evidence"
slots = []
for d in sorted(ev.glob("perf-*-r*-*")):
    m = re.fullmatch(r"perf-(small1080|original1088)-r(\d+)-(\d+)-(A0_NoR2D|A1_R2D_C32|A2_R2D_MH|A3_R2D_Both)", d.name)
    assert m, d.name
    prof, rnd, slot, arm = m.groups()
    rows = list(csv.DictReader((d / "out.csv").open()))
    assert len(rows) == 240
    checked = [x for x in rows if x["checked"] == "1"]
    assert len(checked) == 2 and all(int(x["invalid"]) == 0 and int(x["changed_half_values_from_first"]) == 0 for x in checked)
    vals = np.array([float(x["wall_ms"]) for x in rows[80:]])
    raw = json.loads((d / "raw-hashes.json").read_text(encoding="utf-8-sig"))
    assert len({x["sha256"] for x in raw}) == 1
    slots.append({
        "name": d.name, "profile": prof, "round": int(rnd), "slot": int(slot), "arm": arm,
        "mean_ms": float(vals.mean()), "p99_ms": float(np.quantile(vals, 0.99)),
        "raw_sha": raw[0]["sha256"], "values": vals.tolist()
    })

assert len(slots) == 48
summary = {}
arms_order = ["A0_NoR2D", "A1_R2D_C32", "A2_R2D_MH", "A3_R2D_Both"]
for prof in ["small1080", "original1088"]:
    ps = [s for s in slots if s["profile"] == prof]
    assert len(ps) == 24 and len({s["raw_sha"] for s in ps}) == 1
    a0_all = np.array(sum([s["values"] for s in ps if s["arm"] == "A0_NoR2D"], []))
    a3_all = np.array(sum([s["values"] for s in ps if s["arm"] == "A3_R2D_Both"], []))
    prof_summary = {}
    print(f"=== r3a_r2d_4arm {prof} ===")
    for arm in arms_order:
        arm_all = np.array(sum([s["values"] for s in ps if s["arm"] == arm], []))
        rounds = []
        for rnd in [1, 2, 3]:
            r_a0 = np.array(sum([s["values"] for s in ps if s["round"] == rnd and s["arm"] == "A0_NoR2D"], []))
            r_a3 = np.array(sum([s["values"] for s in ps if s["round"] == rnd and s["arm"] == "A3_R2D_Both"], []))
            r_arm = np.array(sum([s["values"] for s in ps if s["round"] == rnd and s["arm"] == arm], []))
            rounds.append({
                "round": rnd,
                "a0_ms": float(r_a0.mean()),
                "a3_ms": float(r_a3.mean()),
                "arm_ms": float(r_arm.mean()),
                "delta_vs_A0_us": float((r_arm.mean() - r_a0.mean()) * 1000.0),
                "p99_delta_vs_A0_us": float((np.quantile(r_arm, 0.99) - np.quantile(r_a0, 0.99)) * 1000.0),
                "delta_vs_A3_us": float((r_arm.mean() - r_a3.mean()) * 1000.0),
                "p99_delta_vs_A3_us": float((np.quantile(r_arm, 0.99) - np.quantile(r_a3, 0.99)) * 1000.0),
            })
        prof_summary[arm] = {
            "mean_ms": float(arm_all.mean()),
            "p99_ms": float(np.quantile(arm_all, 0.99)),
            "delta_vs_A0_us": float((arm_all.mean() - a0_all.mean()) * 1000.0),
            "p99_delta_vs_A0_us": float((np.quantile(arm_all, 0.99) - np.quantile(a0_all, 0.99)) * 1000.0),
            "delta_vs_A3_us": float((arm_all.mean() - a3_all.mean()) * 1000.0),
            "p99_delta_vs_A3_us": float((np.quantile(arm_all, 0.99) - np.quantile(a3_all, 0.99)) * 1000.0),
            "rounds": rounds
        }
        r_str = " / ".join(f"{r['delta_vs_A0_us']:+.2f}" for r in rounds)
        print(f"  {arm:12s}: mean={arm_all.mean():.6f} ms (vs A0 {prof_summary[arm]['delta_vs_A0_us']:+7.2f} us, vs A3 {prof_summary[arm]['delta_vs_A3_us']:+7.2f} us) | p99={np.quantile(arm_all,0.99):.6f} ms (vs A0 {prof_summary[arm]['p99_delta_vs_A0_us']:+7.2f} us) | R1/R2/R3 vs A0: {r_str}")
    summary[prof] = prof_summary

(p / "r3a_r2d_4arm-statistics.json").write_text(json.dumps({
    "summary": summary,
    "slots": [{k: v for k, v in s.items() if k != "values"} for s in slots]
}, indent=2))
