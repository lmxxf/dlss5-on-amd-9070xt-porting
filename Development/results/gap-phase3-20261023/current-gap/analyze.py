from pathlib import Path
import csv,json,re,numpy as np
r=Path(__file__).resolve().parent;e=r/'evidence';rows=[]
for d in sorted(x for x in e.glob('[0-9][0-9]-r*') if x.is_dir()):
 c=json.loads((d/'command.json').read_text(encoding='utf-8-sig'));s=(d/'stdout.log').read_text(encoding='utf-8-sig');row={k:c[k] for k in ['label','round','kind']}
 if c['kind']=='M':
  m=re.search(r'measurement wall ([0-9.]+) ms, GPU total ([0-9.]+) ms over (\d+) frames',s);assert m and int(m[3])==160
  row.update(gpu_ms=float(m[2])/160,cpu_ms=float(m[1])/160)
 else:
  rr=list(csv.DictReader((d/'timing.csv').open()));assert len(rr)==160 and 'raw_bitdiff=0 output_finite=1' in s
  g=np.array([float(x['gpu_ms']) for x in rr]);row.update(gpu_ms=float(g.mean()),cpu_ms=float(np.mean([float(x['cpu_ms']) for x in rr])),p99_ms=float(np.quantile(g,.99)))
 rows.append(row)
assert len(rows)==12
rounds=[]
for n in [1,2,3]:
 q=[x for x in rows if x['round']==n];h=np.mean([x['gpu_ms'] for x in q if x['kind']=='N']);m=np.mean([x['gpu_ms'] for x in q if x['kind']=='M']);rounds.append(dict(round=n,hip_ms=float(h),mochi_ms=float(m),gap_ms=float(h-m)))
raw=json.loads((e/'raw-hashes.json').read_text(encoding='utf-8-sig'));groups={k:sorted({x['sha256'].lower() for x in raw if x['slot'].endswith('-'+k)}) for k in ['N','M']}
assert groups['N']==['153ac018f5dd5744cff9157661c46c469d01db6018a97c93aec2b1e2e05647f1'];assert groups['M']==['624d9d1a2f5cedb300182bb7766df1c3b421d57555c9edc20220baa14431a612']
h=float(np.mean([x['hip_ms'] for x in rounds]));m=float(np.mean([x['mochi_ms'] for x in rounds]));result=dict(hip_ms=h,mochi_ms=m,gap_ms=h-m,relative_percent=100*(h/m-1),rounds=rounds,slots=rows,raw_sha=groups,samples_per_arm=960,mochi_p99_available=False)
(r/'statistics.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
