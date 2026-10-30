from pathlib import Path
import json, csv, re, sys, collections
import numpy as np

p = Path(__file__).parent
role = sys.argv[1]
slots = []
for d in sorted((p / role / 'evidence').glob('kernel-*-r*-*')):
    m = re.fullmatch(r'kernel-(\w+)-r(\d+)-(\d+)-(baseline|candidate)', d.name)
    assert m
    profile, rnd, slot, arm = m.groups()
    a = list(csv.DictReader((d / 'kernels.csv').open()))
    num = int(a[0]['dispatches_total'])
    assert len(a) == num * 2 and all(int(x['dispatches_total']) == num for x in a)
    s = (d / 'stderr.log').read_text(encoding='utf-8-sig')
    rate = int(re.search(r'DEVICE_REALTIME rate_khz=(\d+)', s)[1])
    assert rate == 100000
    by = collections.defaultdict(list)
    for x in a:
        by[int(x['dispatch'])].append(x)
    assert len(by) == num and all(len(v) == 2 for v in by.values())
    fn = collections.Counter()
    c512_ffn_proj_ms = 0.0
    c512_attn_proj_ms = 0.0
    c32_post_ms = 0.0
    c32_stage_ms = 0.0
    total_ms = 0.0
    for rr in by.values():
        assert len({x['kernel'] for x in rr}) == 1
        k = rr[0]['kernel'].replace('c32_norm900:', 'c32_wave1:')
        v = float(np.mean([(int(x['end_tick']) - int(x['begin_tick']) - 80) / rate for x in rr]))
        fn[k] += v
        total_ms += v
        if 'split_projection_frag' in k:
            c512_ffn_proj_ms += v
        if 'mh_attention_project_frag_c512' in k:
            c512_attn_proj_ms += v
        if 'c32_wave1_post' in k:
            c32_post_ms += v
        if k.startswith('c32_wave1:') or k == 'mh_fast:mh_pool_project_c32_b8_out8':
            c32_stage_ms += v
    slots.append({
        'profile': profile,
        'round': int(rnd),
        'slot': int(slot),
        'arm': arm,
        'total_ms': total_ms,
        'c512_ffn_proj_ms': c512_ffn_proj_ms,
        'c512_attn_proj_ms': c512_attn_proj_ms,
        'c512_pair_ms': c512_ffn_proj_ms + c512_attn_proj_ms,
        'c32_post_ms': c32_post_ms,
        'c32_stage_ms': c32_stage_ms,
        'kernels_ms': dict(fn)
    })

assert slots
out = {}
for profile in sorted({x['profile'] for x in slots}):
    ss = [x for x in slots if x['profile'] == profile]
    assert len(ss) == 12
    rounds = []
    for rnd in [1, 2, 3]:
        z = sorted([x for x in ss if x['round'] == rnd], key=lambda x: x['slot'])
        assert [x['arm'] for x in z] == ['baseline', 'candidate', 'candidate', 'baseline']
        row = {'round': rnd}
        for metric in ['total_ms', 'c512_ffn_proj_ms', 'c512_attn_proj_ms', 'c512_pair_ms', 'c32_post_ms', 'c32_stage_ms']:
            a_val = float(np.mean([x[metric] for x in z if x['arm'] == 'baseline']))
            b_val = float(np.mean([x[metric] for x in z if x['arm'] == 'candidate']))
            row[metric] = {'baseline': a_val, 'candidate': b_val, 'delta_ms': b_val - a_val, 'delta_us': (b_val - a_val) * 1000.0}
        rounds.append(row)
    summary = {}
    for metric in ['total_ms', 'c512_ffn_proj_ms', 'c512_attn_proj_ms', 'c512_pair_ms', 'c32_post_ms', 'c32_stage_ms']:
        a_val = float(np.mean([r[metric]['baseline'] for r in rounds]))
        b_val = float(np.mean([r[metric]['candidate'] for r in rounds]))
        summary[metric] = {
            'baseline_ms': a_val,
            'candidate_ms': b_val,
            'delta_ms': b_val - a_val,
            'delta_us': (b_val - a_val) * 1000.0,
            'rounds_improve': sum(r[metric]['delta_ms'] < 0 for r in rounds)
        }
    out[profile] = {'summary': summary, 'rounds': rounds}

(p / f'{role}-kernel-statistics.json').write_text(json.dumps({'profiles': out, 'slots': slots}, indent=2))
for profile, d in out.items():
    print(f"=== {role} {profile} ===")
    for m_name, m_val in d['summary'].items():
        print(f"  {m_name:20s}: {m_val['baseline_ms']*1000:8.3f} -> {m_val['candidate_ms']*1000:8.3f} us (delta {m_val['delta_us']:+8.3f} us, {m_val['rounds_improve']}/3 rounds)")
