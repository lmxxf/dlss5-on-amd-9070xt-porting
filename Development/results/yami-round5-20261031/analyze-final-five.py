from pathlib import Path
import csv,json,re
import numpy as np
p=Path(__file__).parent;arms=['stock','base','norm','c256','full'];slots=[]
for d in sorted((p/'final-five-long/evidence').glob('perf-*')):
 m=re.fullmatch(r'perf-(\w+)-r([123])-([0-9])-(\w+)',d.name);assert m and m[4]in arms
 rows=list(csv.DictReader((d/'out.csv').open()));assert len(rows)==800,d
 chk=[x for x in rows if x['checked']=='1'];assert len(chk)==2 and all(int(x['invalid'])==0 and int(x['changed_half_values_from_first'])==0 for x in chk),d
 v=np.array([float(x['wall_ms'])for x in rows[160:]]);assert len(v)==640 and np.isfinite(v).all()and(v>0).all()
 h=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));assert len({x['sha256']for x in h})==1
 assert json.loads((d/'exit.json').read_text(encoding='utf-8-sig'))['exit_code']==0
 slots.append(dict(profile=m[1],round=int(m[2]),slot=int(m[3]),arm=m[4],values=v,sha=h[0]['sha256'],path=str(d)))
assert len(slots)==210
profiles=sorted({s['profile']for s in slots});assert len(profiles)==7
for prof in profiles:
 ss=[s for s in slots if s['profile']==prof];assert len(ss)==30 and len({s['sha']for s in ss})==1
 for rnd in [1,2,3]:
  seq=arms[rnd-1:]+arms[:rnd-1];seq+=seq[::-1];assert [s['arm']for s in sorted([s for s in ss if s['round']==rnd],key=lambda s:s['slot'])]==seq

def compare(a,b):
 out={}
 for prof in profiles:
  ss=[s for s in slots if s['profile']==prof];rr=[]
  for rnd in [1,2,3]:
   aa=sorted([s for s in ss if s['arm']==a and s['round']==rnd],key=lambda s:s['slot']);bb=sorted([s for s in ss if s['arm']==b and s['round']==rnd],key=lambda s:s['slot']);x=np.concatenate([s['values']for s in aa]);y=np.concatenate([s['values']for s in bb]);rr.append(dict(round=rnd,base=float(x.mean()),candidate=float(y.mean()),delta_ms=float(y.mean()-x.mean()),p99_delta_ms=float(np.quantile(y,.99)-np.quantile(x,.99)),base_drift=float(aa[1]['values'].mean()-aa[0]['values'].mean()),base_slots=[s['slot']for s in aa],candidate_slots=[s['slot']for s in bb]))
  x=np.concatenate([s['values']for s in ss if s['arm']==a]);y=np.concatenate([s['values']for s in ss if s['arm']==b]);assert len(x)==len(y)==3840
  out[prof]=dict(baseline_ms=float(x.mean()),candidate_ms=float(y.mean()),delta_ms=float(y.mean()-x.mean()),delta_percent=float(100*(y.mean()/x.mean()-1)),baseline_p99=float(np.quantile(x,.99)),candidate_p99=float(np.quantile(y,.99)),p99_delta_ms=float(np.quantile(y,.99)-np.quantile(x,.99)),samples_per_arm=len(x),rounds=rr,raw_sha=ss[0]['sha'])
 return out
allcmp={}
for a,b in [('stock',x)for x in arms[1:]]+[('base','norm'),('base','c256'),('norm','full'),('c256','full')]:
 t=compare(a,b);key=a+'-to-'+b;allcmp[key]=t;(p/('final-five-'+key+'-statistics.json')).write_text(json.dumps(dict(base_arm=a,candidate_arm=b,tiers=t,design='3rotated symmetric10-slot rounds;800discard160'),indent=2)+'\n')
def bounds(t):
 bad=[]
 for k,v in t.items():
  if v['delta_percent']>.5:bad.append(k+': mean >+0.5%')
  if v['p99_delta_ms']/v['baseline_p99']>.01:bad.append(k+': p99 >+1%')
  if all(r['delta_ms']/r['base']>.005 for r in v['rounds']):bad.append(k+': all3 means >+0.5%')
 return bad

def common(t,arm):
 bad=bounds(t)
 if sum(v['delta_ms']for v in t.values())>=0:bad.append('sum7 means not improved')
 for k in ['small1080']+(['original960']if arm in ['norm','full']else[]):
  if not all(r['delta_ms']<0 and r['p99_delta_ms']<0 for r in t[k]['rounds']):bad.append(k+': not all3 mean/p99 wins')
 if arm in ['c256','full']:
  v=t['original1088']
  if not all(r['delta_ms']<0 for r in v['rounds'])or v['p99_delta_ms']>0:bad.append('original1088 mean3/3 or pooled p99 failed')
 return bad

def inc(t,feature):
 bad=bounds(t)
 for k in ['original960']if feature=='norm'else['original1088','small1080']:
  v=t[k]
  if not all(r['delta_ms']<0 for r in v['rounds'])or (v['p99_delta_ms']>=0 if feature=='norm'else v['p99_delta_ms']>0):bad.append(k+': conditional mean3/3 or pooledp99 failed')
 return bad
checks={};incchecks={k:inc(allcmp[k],f)for k,f in [('base-to-norm','norm'),('c256-to-full','norm'),('base-to-c256','c256'),('norm-to-full','c256')]}
for arm in arms[1:]:
 t=allcmp['stock-to-'+arm];bad=common(t,arm)
 required=[]if arm=='base'else['base-to-norm']if arm=='norm'else['base-to-c256']if arm=='c256'else['c256-to-full','norm-to-full']
 for k in required:
  if incchecks[k]:bad.append('increment '+k+' failed')
 checks[arm]=dict(qualified=not bad,reasons=bad,sum7_delta_us=sum(v['delta_ms']for v in t.values())*1000)
eligible=[a for a in arms[1:]if checks[a]['qualified']];eligible.sort(key=lambda a:checks[a]['sum7_delta_us']);selected=eligible[0]if eligible else None
if selected:
 complexity={'base':0,'norm':1,'c256':1,'full':2};near=[a for a in eligible if checks[a]['sum7_delta_us']<=checks[selected]['sum7_delta_us']+1];selected=min(near,key=lambda a:(complexity[a],checks[a]['sum7_delta_us']))
decision=dict(selected=selected,checks=checks,conditional_checks=incchecks,scope='performance gate only; still requires final configuration/ABI/source-artifact verification',global_upper_bound_proven=False)
(p/'final-five-decision.json').write_text(json.dumps(decision,indent=2)+'\n')
for arm in arms[1:]:
 print(arm)
 for k,v in allcmp['stock-to-'+arm].items():print(k,round(v['delta_ms']*1000,3),round(v['p99_delta_ms']*1000,3),[round(r['delta_ms']*1000,3)for r in v['rounds']],[round(r['p99_delta_ms']*1000,3)for r in v['rounds']])
print(json.dumps(decision,indent=2))
