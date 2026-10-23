import csv,json,re,argparse,hashlib,statistics
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('evidence',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
rows=[]
for d in sorted(a.evidence.glob('[0-9][0-9]-r*')):
    command=json.loads((d/'command.json').read_text(encoding='utf-8-sig'));s=(d/'stdout.log').read_text(encoding='utf-8-sig');kind=command['kind']
    row={k:command[k] for k in ['label','round','pair','kind']}
    if kind=='M':
        m=re.search(r'measurement wall ([0-9.]+) ms, GPU total ([0-9.]+) ms over (\d+) frames',s);assert m,d
        n=int(m[3]);row.update(frames=n,gpu_mean_ms=float(m[2])/n,cpu_mean_ms=float(m[1])/n)
        assert 'native working extent 1920x1088' in s
    else:
        rr=list(csv.DictReader((d/'timing.csv').open()));g=np.array([float(x['gpu_ms']) for x in rr]);cpu=np.array([float(x['cpu_ms']) for x in rr]);assert (g>0).all() and np.isfinite(g).all()
        row.update(frames=len(rr),gpu_mean_ms=float(g.mean()),cpu_mean_ms=float(cpu.mean()),gpu_p99_ms=float(np.quantile(g,.99)),gpu_median_ms=float(np.median(g)))
        assert 'raw_bitdiff=0 output_finite=1' in s and 'proc_h=1088 vit_tokens=640 full71=1 MP1=1 AE0=1 historyoff=1 Style1=1 seed0=1' in s
    assert row['frames']==160
    rows.append(row)
assert len(rows)==24,len(rows)
pairs={}
for pair,kinds in [('current-v-mochi',('N','M')),('old-v-current',('O','N'))]:
    per=[]
    for rnd in range(1,4):
        rr=[x for x in rows if x['pair']==pair and x['round']==rnd];assert len(rr)==4
        ga=statistics.mean(x['gpu_mean_ms'] for x in rr if x['kind']==kinds[0]);gb=statistics.mean(x['gpu_mean_ms'] for x in rr if x['kind']==kinds[1]);per.append(dict(round=rnd,a_ms=ga,b_ms=gb,a_minus_b_ms=ga-gb,a_slot_drift_ms=rr[3]['gpu_mean_ms']-rr[0]['gpu_mean_ms'],b_slot_drift_ms=rr[2]['gpu_mean_ms']-rr[1]['gpu_mean_ms']))
    aa=statistics.mean(x['a_ms'] for x in per);bb=statistics.mean(x['b_ms'] for x in per)
    pairs[pair]=dict(a=kinds[0],b=kinds[1],a_gpu_ms=aa,b_gpu_ms=bb,a_minus_b_ms=aa-bb,rounds=per,steady_frames_per_arm=960)
raw=json.loads((a.evidence/'raw-hashes.json').read_text(encoding='utf-8-sig'))
raw_groups={k:sorted({x['sha256'].lower() for x in raw if x['slot'].endswith('-'+k)}) for k in ['O','N','M']}
assert all(len(v)==1 for v in raw_groups.values()),raw_groups
historical={'old_hip_ms':8.931545125,'mochi_ms':7.885259375,'gap_ms':1.046285750}
res=dict(slots=rows,pairs=pairs,raw_groups=raw_groups,historical_20261006=historical,old_mochi_historical_sha_match=raw_groups['M']==['624d9d1a2f5cedb300182bb7766df1c3b421d57555c9edc20220baa14431a612'],old_hip_historical_sha_match=raw_groups['O']==['153ac018f5dd5744cff9157661c46c469d01db6018a97c93aec2b1e2e05647f1'])
a.output.write_text(json.dumps(res,indent=2));print(json.dumps({'pairs':pairs,'raw_groups':raw_groups},indent=2))
