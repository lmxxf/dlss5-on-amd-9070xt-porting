from pathlib import Path
import numpy as np,json,csv,re
r=Path(__file__).resolve().parent;e=r/'compact-evidence';rows=[]
for d in sorted(e.glob('r[1-3]-[0-3]-*')):
 rr=list(csv.DictReader((d/'timing.csv').open()));g=np.array([float(x['gpu_ms']) for x in rr]);assert len(g)==160 and np.isfinite(g).all() and (g>0).all()
 s=(d/'stdout.log').read_text(encoding='utf-8-sig');assert 'raw_bitdiff=0 output_finite=1' in s
 rows.append(dict(slot=d.name,round=int(d.name[1]),kind=d.name[-1],mean=float(g.mean()),p99=float(np.quantile(g,.99)),median=float(np.median(g)),values=g.tolist()))
assert len(rows)==12
rounds=[]
for rnd in range(1,4):
 x=[v for v in rows if v['round']==rnd];a=np.array(sum([v['values'] for v in x if v['kind']=='N'],[]));b=np.array(sum([v['values'] for v in x if v['kind']=='C'],[]))
 rounds.append(dict(round=rnd,base_ms=float(a.mean()),candidate_ms=float(b.mean()),saved_ms=float(a.mean()-b.mean()),p99_delta_ms=float(np.quantile(b,.99)-np.quantile(a,.99)),a_drift_ms=x[3]['mean']-x[0]['mean'],b_drift_ms=x[2]['mean']-x[1]['mean']))
a=np.array(sum([v['values'] for v in rows if v['kind']=='N'],[]));b=np.array(sum([v['values'] for v in rows if v['kind']=='C'],[]))
raw=json.loads((e/'timing-raw.json').read_text(encoding='utf-8-sig'));assert len(raw)==24 and len({x['sha256'] for x in raw})==1
trace={}
for name in ['trace-current','trace-candidate']:
 s=(e/name/'stdout.log').read_text(encoding='utf-8-sig');ds={}
 for frame,idx,kind,key in re.findall(r'GAP_DISPATCH frame=(\d+) index=(\d+) (\w+) (\S+)',s):ds.setdefault(frame,[]).append(dict(index=int(idx),kind=kind,key=key))
 assert len(ds)==3 and ds['0']==ds['1']==ds['2']
 trace[name]=dict(profiles=re.findall(r'^GAP_PROFILE.*$',s,re.M),counts=re.findall(r'^GAP_COUNT.*$',s,re.M),dispatch=ds['0'])
res=dict(baseline_gpu_ms=float(a.mean()),candidate_gpu_ms=float(b.mean()),saved_ms=float(a.mean()-b.mean()),relative_percent=float(100*(a.mean()-b.mean())/a.mean()),p99_baseline_ms=float(np.quantile(a,.99)),p99_candidate_ms=float(np.quantile(b,.99)),rounds=rounds,slots=[{k:v for k,v in x.items() if k!='values'} for x in rows],gold_sha=raw[0]['sha256'],samples_per_arm=len(a),trace=trace)
(r/'compact-statistics.json').write_text(json.dumps(res,indent=2));print(json.dumps({k:v for k,v in res.items() if k not in ['slots','trace']},indent=2))
