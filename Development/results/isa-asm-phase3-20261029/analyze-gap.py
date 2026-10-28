from pathlib import Path
import json,csv,re,numpy as np
P=Path(__file__).parent;e=P/'gap/evidence';slots=[];raw={}
for d in sorted(e.glob('r*-*-*')):
 m=re.fullmatch(r'r(\d+)-(\d+)-(N|M)',d.name);assert m;kind=m[3];stdout=(d/'stdout.log').read_text(encoding='utf-8-sig');assert json.loads((d/'exit.json').read_text(encoding='utf-8-sig'))['exit_code']==0
 if kind=='N':
  rows=list(csv.DictReader((d/'timing.csv').open()));assert len(rows)==160 and 'raw_bitdiff=0 output_finite=1' in stdout;g=np.array([float(x['gpu_ms'])for x in rows]);cpu=np.array([float(x['cpu_ms'])for x in rows]);gpu=float(g.mean());wall=float(cpu.mean());p99=float(np.quantile(g,.99))
 else:
  a=re.search(r'measurement wall ([0-9.]+) ms, GPU total ([0-9.]+) ms over (\d+) frames',stdout);assert a and int(a[3])==160;gpu=float(a[2])/160;wall=float(a[1])/160;p99=None
 hashes=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));hashes=[hashes]if isinstance(hashes,dict)else hashes;raw.setdefault(kind,set()).update(x['sha256']for x in hashes);slots.append({'round':int(m[1]),'slot':int(m[2]),'kind':kind,'gpu_ms':gpu,'cpu_ms':wall,'p99_ms':p99})
assert raw=={'N':{'153ac018f5dd5744cff9157661c46c469d01db6018a97c93aec2b1e2e05647f1'},'M':{'624d9d1a2f5cedb300182bb7766df1c3b421d57555c9edc20220baa14431a612'}},raw
assert len(slots)==12;rounds=[]
for rnd in [1,2,3]:
 x=[s for s in slots if s['round']==rnd];assert [s['kind']for s in sorted(x,key=lambda s:s['slot'])]==['N','M','M','N'];n=float(np.mean([s['gpu_ms']for s in x if s['kind']=='N']));m=float(np.mean([s['gpu_ms']for s in x if s['kind']=='M']));rounds.append({'round':rnd,'hip_ms':n,'mochi_ms':m,'gap_ms':n-m})
n=float(np.mean([r['hip_ms']for r in rounds]));m=float(np.mean([r['mochi_ms']for r in rounds]));out={'hip_ms':n,'mochi_ms':m,'gap_ms':n-m,'relative_percent':100*(n/m-1),'rounds':rounds,'slots':slots,'samples_per_arm':960,'mochi_commit':'d1185d2','mochi_p99_available':False,'raw_sha':{k:sorted(v)for k,v in raw.items()},'old_gap_ms':0.478631198,'cross_batch_change_not_an_optimization_measurement':True};(P/'gap-statistics.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
