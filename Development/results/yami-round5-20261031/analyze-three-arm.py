from pathlib import Path
import csv,json,re
import numpy as np
p=Path(__file__).parent;root=p/'three-arm-long/evidence';slots=[]
for d in sorted(root.glob('perf-*-r*-*')):
 m=re.fullmatch(r'perf-(\w+)-r([123])-([0-5])-(stock|clamp|combined)',d.name);assert m,d
 assert json.loads((d/'exit.json').read_text(encoding='utf-8-sig'))['exit_code']==0
 rows=list(csv.DictReader((d/'out.csv').open()));assert len(rows)==800,d
 checked=[r for r in rows if r['checked']=='1'];assert len(checked)==2 and all(int(r['invalid'])==0 and int(r['changed_half_values_from_first'])==0 for r in checked),d
 v=np.array([float(r['wall_ms']) for r in rows[160:]]);assert len(v)==640 and np.isfinite(v).all() and (v>0).all()
 hashes=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));assert len({h['sha256']for h in hashes})==1
 slots.append(dict(profile=m[1],round=int(m[2]),slot=int(m[3]),arm=m[4],values=v,sha=hashes[0]['sha256'],source=str(d)))
assert len(slots)==126,len(slots)
profiles=sorted({s['profile']for s in slots});assert len(profiles)==7
for prof in profiles:
 ss=[s for s in slots if s['profile']==prof];assert len(ss)==18 and len({s['sha']for s in ss})==1
 for rnd in [1,2,3]:
  rr=sorted([s for s in ss if s['round']==rnd],key=lambda s:s['slot']);seq=['stock','clamp','combined'];seq=seq[rnd-1:]+seq[:rnd-1];seq+=seq[::-1];assert [s['arm']for s in rr]==seq

def comparison(base,cand,file):
 tiers={}
 for prof in profiles:
  ss=[s for s in slots if s['profile']==prof];rounds=[]
  for rnd in [1,2,3]:
   aa=sorted([s for s in ss if s['round']==rnd and s['arm']==base],key=lambda s:s['slot']);bb=sorted([s for s in ss if s['round']==rnd and s['arm']==cand],key=lambda s:s['slot']);a=np.concatenate([s['values']for s in aa]);b=np.concatenate([s['values']for s in bb]);assert len(a)==len(b)==1280
   rounds.append(dict(round=rnd,base=float(a.mean()),candidate=float(b.mean()),delta_ms=float(b.mean()-a.mean()),p99_delta_ms=float(np.quantile(b,.99)-np.quantile(a,.99)),base_drift=float(aa[1]['values'].mean()-aa[0]['values'].mean()),base_slots=[s['slot']for s in aa],candidate_slots=[s['slot']for s in bb]))
  a=np.concatenate([s['values']for s in ss if s['arm']==base]);b=np.concatenate([s['values']for s in ss if s['arm']==cand]);assert len(a)==len(b)==3840
  tiers[prof]=dict(baseline_ms=float(a.mean()),candidate_ms=float(b.mean()),delta_ms=float(b.mean()-a.mean()),delta_percent=float(100*(b.mean()/a.mean()-1)),baseline_p99=float(np.quantile(a,.99)),candidate_p99=float(np.quantile(b,.99)),p99_delta_ms=float(np.quantile(b,.99)-np.quantile(a,.99)),rounds=rounds,raw_sha=ss[0]['sha'],samples_per_arm=len(a))
 out=dict(base_arm=base,candidate_arm=cand,design='3 rotated symmetric 6-slot rounds;800frames discard160',tiers=tiers)
 (p/file).write_text(json.dumps(out,indent=2)+'\n');return tiers

clamp=comparison('stock','clamp','three-clamp-statistics.json');combined=comparison('stock','combined','three-combined-statistics.json');increment=comparison('clamp','combined','three-increment-statistics.json')
def bounds(t):
 reasons=[]
 for k,v in t.items():
  if v['delta_percent']>.5:reasons.append(k+': pooled mean >+0.5%')
  if v['p99_delta_ms']/v['baseline_p99']>.01:reasons.append(k+': pooled p99 >+1%')
  if all(r['delta_ms']/r['base']>.005 for r in v['rounds']):reasons.append(k+': all3 means >+0.5%')
 return reasons

def gate(t):
 reasons=bounds(t);s=t['small1080']
 if not all(r['delta_ms']<0 and r['p99_delta_ms']<0 for r in s['rounds']):reasons.append('small1080 not all3 mean/p99 wins')
 if t['original1088']['delta_ms']>=0:reasons.append('original1088 pooled mean not improved')
 if sum(v['delta_ms']for v in t.values())>=0:reasons.append('sum7 pooled means not improved')
 return dict(pass_gate=not reasons,reasons=reasons)
a,b=gate(clamp),gate(combined);inc=bounds(increment)
if not all(r['delta_ms']<0 for r in increment['small1080']['rounds']) or increment['small1080']['p99_delta_ms']>=0:inc.append('incremental small1080 mean3/3 or pooledp99 failed')
selected='clamp'if a['pass_gate']else 'combined'if b['pass_gate']else None
if a['pass_gate']and b['pass_gate']and not inc:selected='combined'
decision=dict(clamp=a,combined=b,increment_reasons=inc,selected_for_further_correctness_validation=selected,not_a_deployment_or_upper_bound_proof=True)
(p/'three-arm-decision.json').write_text(json.dumps(decision,indent=2)+'\n')
for name,t in [('clamp',clamp),('combined',combined),('increment',increment)]:
 print(name)
 for k,v in t.items():print(k,round(v['delta_ms']*1000,3),round(v['p99_delta_ms']*1000,3),[round(r['delta_ms']*1000,3)for r in v['rounds']],[round(r['p99_delta_ms']*1000,3)for r in v['rounds']])
print(json.dumps(decision,indent=2))
