from pathlib import Path
import json,csv,re,sys,numpy as np
R=Path(__file__).parent;E=Path(sys.argv[1]);m=json.loads((R/'perf-jobs.json').read_text());out={}
for prof in sorted({j['profile'] for j in m['jobs']}):
 slots=[]
 for j in [x for x in m['jobs'] if x['profile']==prof]:
  d=E/j['label'].split('/')[-1];rows=list(csv.DictReader((d/'out.csv').open()));assert len(rows)==240
  checked=[x for x in rows if x['checked']=='1'];assert len(checked)==2 and all(int(x['invalid'])==0 and int(x['changed_half_values_from_first'])==0 for x in checked)
  raw=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));assert len(raw)>=2 and len({x['sha256'] for x in raw})==1
  vals=np.array([float(x['wall_ms']) for x in rows[80:]]);assert np.isfinite(vals).all() and (vals>0).all()
  if j['arm']=='candidate':
   s=(d/'stderr.log').read_text(encoding='utf-8-sig');q=re.search(r'VIT_QKFRAG requested=1 available=1 producers=(\d+) consumers=(\d+)',s);assert q and int(q[1])==int(q[2])== (1936 if prof=='small1080' else 1928)
  slots.append(dict(label=j['label'],round=j['round'],slot=j['slot'],arm=j['arm'],mean_ms=float(vals.mean()),p99_ms=float(np.quantile(vals,.99)),sha=raw[0]['sha256'],values=vals.tolist()))
 assert len(slots)==12 and len({s['sha']for s in slots})==1
 rounds=[]
 for n in range(1,4):
  ss=sorted([s for s in slots if s['round']==n],key=lambda x:x['slot']);assert [s['arm'] for s in ss]==['baseline','candidate','candidate','baseline'];aa=np.array(ss[0]['values']+ss[3]['values']);bb=np.array(ss[1]['values']+ss[2]['values']);rounds.append(dict(round=n,baseline_ms=float(aa.mean()),candidate_ms=float(bb.mean()),delta_ms=float(bb.mean()-aa.mean()),p99_delta_ms=float(np.quantile(bb,.99)-np.quantile(aa,.99))))
 aa=np.array(sum([s['values']for s in slots if s['arm']=='baseline'],[]));bb=np.array(sum([s['values']for s in slots if s['arm']=='candidate'],[]))
 out[prof]=dict(baseline_ms=float(aa.mean()),candidate_ms=float(bb.mean()),delta_ms=float(bb.mean()-aa.mean()),p99_delta_ms=float(np.quantile(bb,.99)-np.quantile(aa,.99)),samples_per_arm=len(aa),rounds=rounds,slots=[{k:v for k,v in s.items()if k!='values'}for s in slots])
Path(sys.argv[2]).write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({k:{x:v[x]for x in ['baseline_ms','candidate_ms','delta_ms','p99_delta_ms']}for k,v in out.items()},indent=2))
