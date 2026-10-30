#!/usr/bin/env python3
"""
Post-process Round 3 final verification runs:
- final-gold-reference.json
- final-statistics.json & final-statistics.txt
- final-pure-abba-statistics.json
- final-nmmn-statistics.json & final-nmmn-statistics.txt
- final-kernel-statistics.json & final-kernel-statistics.txt
- final-stage-ledger.json
- final-decision.json
- evidence indices (*-evidence-index.json)
"""
from pathlib import Path
import json, csv, re, subprocess, hashlib
import numpy as np

p = Path(__file__).parent
load = lambda f: json.loads(f.read_text(encoding='utf-8-sig'))

# 1. final-gold-reference.json
window_ref = load(p.parent / 'c32-stage-phase5-20261029/1a/evidence/gold-smallwindow-baseline/raw-hashes.json')[0]['sha256']
refs = {}
manifest_gold = load(p / 'final-gold.json')
assert len(manifest_gold['jobs']) == 14
for j in manifest_gold['jobs']:
    d = p / 'final/evidence' / j['label'].split('/')[1]
    assert load(d / 'exit.json')['exit_code'] == 0, d
    rows = list(csv.DictReader((d / 'out.csv').open()))
    assert len(rows) == 3 and all(int(x['invalid']) == 0 and int(x['changed_half_values_from_first']) == 0 for x in rows), d
    raw = load(d / 'raw-hashes.json')
    raw = [raw] if isinstance(raw, dict) else raw
    ref = j.get('gold') or window_ref
    assert len({x['sha256'] for x in raw}) == 1 and raw[0]['sha256'] == ref, (d, raw, ref)
    refs[j['label']] = ref
    if j['label'].endswith('-candidate'):
        s_err = (d / 'stderr.log').read_text(encoding='utf-8-sig')
        assert re.search(r'C32_POOL_FUSED calls=[1-9]\d*', s_err), d
(p / 'final-gold-reference.json').write_text(json.dumps(refs, indent=2))
print("Verified 14 Gold jobs (42 frames, 0 bit-diff) -> final-gold-reference.json")

# 2. final-statistics.json & final-statistics.txt
out_txt = subprocess.check_output(['python3', str(p / 'analyze.py'), str(p / 'final/evidence'), str(p / 'final-statistics.json')], text=True)
(p / 'final-statistics.txt').write_text(out_txt)
print("Generated final-statistics.json")

# 3. final-pure-abba-statistics.json
pure_slots = []
for d in sorted((p / 'final-pure-abba/evidence').iterdir()):
    m = re.fullmatch(r'(original1088|small1080|small900)-r(\d+)-(\d+)-(baseline|candidate)', d.name)
    if not m: continue
    profile, rnd, slot, arm = m.groups()
    assert load(d / 'exit.json')['exit_code'] == 0
    s_out = (d / 'stdout.log').read_text(encoding='utf-8-sig')
    assert 'raw_bitdiff=0 output_finite=1' in s_out
    raw = load(d / 'raw-hashes.json')
    raw = [raw] if isinstance(raw, dict) else raw
    assert len({x['sha256'] for x in raw}) == 1
    a = list(csv.DictReader((d / 'timing.csv').open()))
    assert len(a) == 160
    v = np.array([float(x['gpu_ms']) for x in a])
    assert np.isfinite(v).all() and (v > 0).all()
    pure_slots.append({
        'profile': profile, 'round': int(rnd), 'slot': int(slot), 'arm': arm,
        'gpu_ms': float(v.mean()), 'p99_ms': float(np.quantile(v, .99)),
        'raw_sha256': raw[0]['sha256'], 'values': v.tolist()
    })
assert len(pure_slots) == 36
pure_tiers = {}
for name in ['original1088', 'small1080', 'small900']:
    ss = [s for s in pure_slots if s['profile'] == name]
    assert len(ss) == 12 and len({s['raw_sha256'] for s in ss}) == 1
    rounds = []
    for rnd in [1, 2, 3]:
        x = sorted([s for s in ss if s['round'] == rnd], key=lambda s: s['slot'])
        assert [s['arm'] for s in x] == ['baseline', 'candidate', 'candidate', 'baseline']
        aa = np.array(sum([s['values'] for s in x if s['arm'] == 'baseline'], []))
        bb = np.array(sum([s['values'] for s in x if s['arm'] == 'candidate'], []))
        rounds.append({
            'round': rnd, 'base_ms': float(aa.mean()), 'cand_ms': float(bb.mean()),
            'delta_ms': float(bb.mean() - aa.mean()), 'delta_us': float((bb.mean() - aa.mean()) * 1000.0),
            'p99_delta_us': float((np.quantile(bb, .99) - np.quantile(aa, .99)) * 1000.0)
        })
    aa = np.array(sum([s['values'] for s in ss if s['arm'] == 'baseline'], []))
    bb = np.array(sum([s['values'] for s in ss if s['arm'] == 'candidate'], []))
    pure_tiers[name] = {
        'baseline_ms': float(aa.mean()), 'candidate_ms': float(bb.mean()),
        'delta_ms': float(bb.mean() - aa.mean()), 'delta_us': float((bb.mean() - aa.mean()) * 1000.0),
        'delta_percent': float(100.0 * (bb.mean() / aa.mean() - 1.0)),
        'baseline_p99': float(np.quantile(aa, .99)), 'candidate_p99': float(np.quantile(bb, .99)),
        'p99_delta_us': float((np.quantile(bb, .99) - np.quantile(aa, .99)) * 1000.0),
        'rounds': rounds, 'raw_sha256': ss[0]['raw_sha256'], 'samples_per_arm': len(aa)
    }
(p / 'final-pure-abba-statistics.json').write_text(json.dumps({
    'tiers': pure_tiers,
    'slots': [{k: v for k, v in s.items() if k != 'values'} for s in pure_slots]
}, indent=2))
print("Generated final-pure-abba-statistics.json")

# 4. final-nmmn-statistics.json & final-nmmn-statistics.txt
nmmn_slots = []
for d in sorted((p / 'final-nmmn/evidence').iterdir()):
    m = re.fullmatch(r'(original1088|small1080|small900)-r(\d+)-(\d+)-(N|M)', d.name)
    if not m: continue
    profile, rnd, slot, kind = m.groups()
    assert load(d / 'exit.json')['exit_code'] == 0
    s = (d / 'stdout.log').read_text(encoding='utf-8-sig')
    raw = load(d / 'raw-hashes.json')
    raw = [raw] if isinstance(raw, dict) else raw
    assert len({x['sha256'] for x in raw}) == 1
    if kind == 'N':
        a = list(csv.DictReader((d / 'timing.csv').open()))
        assert len(a) == 160 and 'raw_bitdiff=0 output_finite=1' in s
        v = np.array([float(x['gpu_ms']) for x in a])
        assert np.isfinite(v).all() and (v > 0).all()
        gpu = float(v.mean())
        cpu = float(np.mean([float(x['cpu_ms']) for x in a]))
        p99 = float(np.quantile(v, .99))
        assert f'ACTUAL_MODEL requested={0 if profile.startswith("small") else 1} effective={0 if profile.startswith("small") else 1}' in s
    else:
        t = re.search(r'measurement wall ([0-9.]+) ms, GPU total ([0-9.]+) ms over (\d+) frames', s)
        assert t and int(t[3]) == 160 and 'ACO network: pipeline binaries on AMD Radeon RX 9070 XT' in s
        gpu = float(t[2]) / 160.0
        cpu = float(t[1]) / 160.0
        p99 = None
    nmmn_slots.append({'profile': profile, 'round': int(rnd), 'slot': int(slot), 'kind': kind, 'gpu_ms': gpu, 'cpu_ms': cpu, 'p99_ms': p99, 'raw_sha256': raw[0]['sha256']})
assert len(nmmn_slots) == 36
nmmn_tiers = {}
for name in ['original1088', 'small1080', 'small900']:
    ss = [s for s in nmmn_slots if s['profile'] == name]
    rounds = []
    for rnd in [1, 2, 3]:
        x = sorted([s for s in ss if s['round'] == rnd], key=lambda s: s['slot'])
        assert ''.join(s['kind'] for s in x) == 'NMMN'
        n_val = float(np.mean([s['gpu_ms'] for s in x if s['kind'] == 'N']))
        m_val = float(np.mean([s['gpu_ms'] for s in x if s['kind'] == 'M']))
        rounds.append({'round': rnd, 'hip_ms': n_val, 'aco_ms': m_val, 'gap_ms': n_val - m_val})
    n_val = float(np.mean([x['hip_ms'] for x in rounds]))
    m_val = float(np.mean([x['aco_ms'] for x in rounds]))
    nmmn_tiers[name] = {'hip_ms': n_val, 'aco_ms': m_val, 'gap_ms': n_val - m_val, 'hip_slower_percent': 100.0 * (n_val / m_val - 1.0), 'rounds': rounds, 'steady_samples_per_arm': 960}
    for kind in ['N', 'M']:
        assert len({x['raw_sha256'] for x in ss if x['kind'] == kind}) == 1
(p / 'final-nmmn-statistics.json').write_text(json.dumps({'nmmn': nmmn_tiers, 'slots': nmmn_slots}, indent=2))
(p / 'final-nmmn-statistics.txt').write_text(json.dumps(nmmn_tiers, indent=2) + '\n')
print("Generated final-nmmn-statistics.json")

# 5. final-kernel-statistics.json & final-kernel-statistics.txt
k_txt = subprocess.check_output(['python3', str(p / 'analyze-kernels.py'), 'final-kernel'], text=True)
if (p / 'final-kernel-kernel-statistics.json').exists():
    (p / 'final-kernel-kernel-statistics.json').rename(p / 'final-kernel-statistics.json')
(p / 'final-kernel-statistics.txt').write_text(k_txt)
print("Generated final-kernel-statistics.json")

# 6. final-stage-ledger.json
k_stats = load(p / 'final-kernel-statistics.json')
r2_ledger = load(p.parent / 'ksearch-round2-20261031/final-stage-ledger.json')
fam_map = {}
for prof in ['original1088', 'small1080']:
    for kname, kinfo in r2_ledger[prof]['per_kernel_mapping'].items():
        fam_map[kname] = kinfo['family']

ledger = {}
for prof in ['original1088', 'small1080']:
    ss = [s for s in k_stats['slots'] if s['profile'] == prof]
    base_slots = [s for s in ss if s['arm'] == 'baseline']
    cand_slots = [s for s in ss if s['arm'] == 'candidate']
    all_k = sorted(base_slots[0]['kernels_ms'].keys())
    per_k = {}
    fam_totals = {f: {'base': 0.0, 'cand': 0.0} for f in ['1_c32_family', '2_c256_family', '3_c64_c128_family', '4_c512_family', '5_vit_family']}
    for kname in all_k:
        b_us = float(np.mean([s['kernels_ms'][kname] for s in base_slots]) * 1000.0)
        c_us = float(np.mean([s['kernels_ms'][kname] for s in cand_slots]) * 1000.0)
        fam = fam_map[kname]
        per_k[kname] = {
            'family': fam,
            'base_8a9c60fd_us': b_us,
            'cand_round3_us': c_us,
            'delta_us': c_us - b_us
        }
        fam_totals[fam]['base'] += b_us
        fam_totals[fam]['cand'] += c_us

    fams_out = {}
    for fam in ['1_c32_family', '2_c256_family', '3_c64_c128_family', '4_c512_family', '5_vit_family']:
        aco_us = r2_ledger[prof]['families'][fam]['aco_step2_query_us']
        b_us = fam_totals[fam]['base']
        c_us = fam_totals[fam]['cand']
        fams_out[fam] = {
            'hip_8a9c60fd_rtc_us': b_us,
            'hip_round3_rtc_us': c_us,
            'hip_rtc_delta_us': c_us - b_us,
            'aco_step2_query_us': aco_us,
            'stage_diff_round3_minus_aco_us': c_us - aco_us
        }
    b_sum = sum(x['hip_8a9c60fd_rtc_us'] for x in fams_out.values())
    c_sum = sum(x['hip_round3_rtc_us'] for x in fams_out.values())
    aco_sum = sum(x['aco_step2_query_us'] for x in fams_out.values())
    hip_nmmn_us = nmmn_tiers[prof]['hip_ms'] * 1000.0
    aco_nmmn_us = nmmn_tiers[prof]['aco_ms'] * 1000.0
    nmmn_gap_us = nmmn_tiers[prof]['gap_ms'] * 1000.0

    ledger[prof] = {
        'trace_evidence_dir': f'Development/results/ksearch-round3-20261031/final-kernel/evidence/kernel-{prof}-r*-*',
        'aco_reference_file': 'Development/results/aco-hotspot-phase4-20261029/step2-hotspot-breakdown.json',
        'families': fams_out,
        'sum_of_stages_us': {
            'hip_8a9c60fd_rtc_sum_us': b_sum,
            'hip_round3_rtc_sum_us': c_sum,
            'hip_rtc_sum_delta_us': c_sum - b_sum,
            'aco_step2_sum_us': aco_sum,
            'stage_sum_diff_us': c_sum - aco_sum
        },
        'whole_network_nmmn_event_us': {
            'hip_round3_nmmn_us': hip_nmmn_us,
            'aco_round3_nmmn_us': aco_nmmn_us,
            'nmmn_gap_us': nmmn_gap_us
        },
        'unexplained_remainder_and_clock_domain_diff_us': {
            'hip_nmmn_minus_rtc_sum_us': hip_nmmn_us - c_sum,
            'aco_nmmn_minus_step2_sum_us': aco_nmmn_us - aco_sum,
            'total_gap_minus_stage_sum_diff_us': nmmn_gap_us - (c_sum - aco_sum)
        },
        'per_kernel_mapping': per_k
    }
(p / 'final-stage-ledger.json').write_text(json.dumps(ledger, indent=2))
print("Generated final-stage-ledger.json")

# 7. Evidence indices
for case in ['smoke', 'final', 'final-pure-abba', 'final-nmmn', 'final-kernel', 'r3a', 'r3b', 'r3c', 'probes_sm', 'r2d_primitive', 's1080_4arm', 'multi_reg']:
    ev_dir = p / case / 'evidence'
    if ev_dir.exists():
        idx = []
        for f in sorted(ev_dir.rglob('*')):
            if f.is_file():
                idx.append({
                    'path': f.relative_to(p).as_posix(),
                    'bytes': f.stat().st_size,
                    'sha256': hashlib.sha256(f.read_bytes()).hexdigest()
                })
        (p / f'{case}-evidence-index.json').write_text(json.dumps(idx, indent=2))

# 8. final-decision.json
f_stats = load(p / 'final-statistics.json')['tiers']
r3a_stats = load(p / 'r3a-statistics.json')['tiers']
decision = {
    'accepted': all(x['delta_ms'] < 0 and x['p99_delta_ms'] < 0 for x in f_stats.values()),
    'candidate': 'Round 3 final (b1af6240 + R3-A CW_FMED3_CLAMP=1 in C32 Softmax + explicit C512W16Fb8ShapeEligible history gates)',
    'final_frame_abba_vs_8a9c60fd': f_stats,
    'r3a_frame_abba_vs_b1af6240': r3a_stats,
    'final_pure_abba_vs_8a9c60fd': pure_tiers,
    'final_nmmn_vs_v008_aco': nmmn_tiers,
    'final_hardware_clock_vs_8a9c60fd': {prof: k_stats['profiles'][prof]['summary'] for prof in ['original1088', 'small1080']}
}
(p / 'final-decision.json').write_text(json.dumps(decision, indent=2))
print("Generated final-decision.json, accepted =", decision['accepted'])
