from pathlib import Path
import argparse,csv,json,re
import numpy as np
p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('output',type=Path);p.add_argument('--replacement-root',type=Path);p.add_argument('--replace-round',type=int,default=3);a=p.parse_args();slots=[]
dirs=list(a.root.glob('perf-*-r*-*'))
if a.replacement_root:
 dirs=[d for d in dirs if f'-r{a.replace_round}-' not in d.name]+list(a.replacement_root.glob('perf-*-r*-*'))
for d in sorted(dirs,key=lambda p:p.name):
 m=re.match(r'perf-([a-zA-Z0-9]+)-r(\d+)-(\d+)-(baseline|candidate)',d.name)
 if not m:continue
 rows=list(csv.DictReader((d/'out.csv').open()));assert len(rows)==240,d
 checked=[x for x in rows if x['checked']=='1'];assert len(checked)==2 and all(int(x['invalid'])==0 and int(x['changed_half_values_from_first'])==0 for x in checked)
 vals=np.array([float(x['wall_ms']) for x in rows[80:]]);assert np.isfinite(vals).all() and (vals>0).all()
 raw=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));assert len({x['sha256'] for x in raw})==1
 slots.append(dict(name=d.name,source_path=str(d),height=m[1],round=int(m[2]),slot=int(m[3]),role=m[4],mean=float(vals.mean()),p99=float(np.quantile(vals,.99)),raw_sha=raw[0]['sha256'],values=vals.tolist()))
assert slots
result={}
for h in sorted({s['height'] for s in slots}):
 rr=[s for s in slots if s['height']==h];assert len(rr)==12,(h,len(rr));assert len({s['raw_sha'] for s in rr})==1
 rounds=[]
 for n in range(1,4):
  ss=sorted([s for s in rr if s['round']==n],key=lambda x:x['slot']);aa=np.array(sum([s['values'] for s in ss if s['role']=='baseline'],[]));bb=np.array(sum([s['values'] for s in ss if s['role']=='candidate'],[]))
  rounds.append(dict(round=n,base=float(aa.mean()),candidate=float(bb.mean()),delta_ms=float(bb.mean()-aa.mean()),p99_delta_ms=float(np.quantile(bb,.99)-np.quantile(aa,.99)),base_drift=ss[-1]['mean']-ss[0]['mean']))
 aa=np.array(sum([s['values'] for s in rr if s['role']=='baseline'],[]));bb=np.array(sum([s['values'] for s in rr if s['role']=='candidate'],[]))
 result[str(h)]=dict(baseline_ms=float(aa.mean()),candidate_ms=float(bb.mean()),delta_ms=float(bb.mean()-aa.mean()),delta_percent=float(100*(bb.mean()/aa.mean()-1)),baseline_p99=float(np.quantile(aa,.99)),candidate_p99=float(np.quantile(bb,.99)),p99_delta_ms=float(np.quantile(bb,.99)-np.quantile(aa,.99)),rounds=rounds,raw_sha=rr[0]['raw_sha'],samples_per_arm=len(aa))
a.output.write_text(json.dumps(dict(tiers=result,slots=[{k:v for k,v in s.items() if k!='values'} for s in slots]),indent=2));print(json.dumps(result,indent=2))
