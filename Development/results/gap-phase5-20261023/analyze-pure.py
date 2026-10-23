from pathlib import Path
import json,csv,re,sys
import numpy as np
root=Path(sys.argv[1]);slots=[]
for d in sorted(root.glob('pure-r*-*-*')):
 m=re.match(r'pure-r(\d+)-(\d+)-(baseline|candidate)$',d.name)
 if not m:continue
 rows=list(csv.DictReader((d/'timing.csv').open(encoding='utf-8-sig')));assert len(rows)==160
 vals=np.array([float(x['gpu_ms']) for x in rows]);assert np.isfinite(vals).all() and (vals>0).all()
 hs=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));assert hs and all(x['sha256']=='153ac018f5dd5744cff9157661c46c469d01db6018a97c93aec2b1e2e05647f1' for x in hs)
 slots.append(dict(case=d.name,round=int(m[1]),role=m[3],values=vals.tolist(),mean=float(vals.mean()),p99=float(np.quantile(vals,.99))))
assert len(slots)==12
rounds=[]
for n in range(1,4):
 aa=np.array(sum([s['values'] for s in slots if s['round']==n and s['role']=='baseline'],[]));bb=np.array(sum([s['values'] for s in slots if s['round']==n and s['role']=='candidate'],[]));assert len(aa)==len(bb)==320
 rounds.append(dict(round=n,baseline_ms=float(aa.mean()),candidate_ms=float(bb.mean()),delta_ms=float(bb.mean()-aa.mean()),p99_delta_ms=float(np.quantile(bb,.99)-np.quantile(aa,.99))))
aa=np.array(sum([s['values'] for s in slots if s['role']=='baseline'],[]));bb=np.array(sum([s['values'] for s in slots if s['role']=='candidate'],[]))
out=dict(baseline_ms=float(aa.mean()),candidate_ms=float(bb.mean()),delta_ms=float(bb.mean()-aa.mean()),baseline_p99_ms=float(np.quantile(aa,.99)),candidate_p99_ms=float(np.quantile(bb,.99)),rounds=rounds,samples_per_arm=len(aa),slots=[{k:v for k,v in s.items() if k!='values'} for s in slots]);Path(sys.argv[2]).write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({k:v for k,v in out.items() if k!='slots'},indent=2))
