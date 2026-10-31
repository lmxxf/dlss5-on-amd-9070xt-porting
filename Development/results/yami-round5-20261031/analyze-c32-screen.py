from pathlib import Path
import csv,json,re,sys
import numpy as np
p=Path(__file__).parent;role=sys.argv[1]if len(sys.argv)>1 else'c32-reduction-screen';profile=sys.argv[2]if len(sys.argv)>2 else'small1080';slots=[];arms=['stock','dotseq','dotpar','wmma2','wmma4']
for d in sorted((p/role/'evidence').glob('perf-*')):
 m=re.fullmatch(r'perf-'+re.escape(profile)+r'-r([123])-([0-9])-(\w+)',d.name);assert m and m[3]in arms
 rows=list(csv.DictReader((d/'out.csv').open()));assert len(rows)==240
 chk=[r for r in rows if r['checked']=='1'];assert len(chk)==2 and all(int(r['invalid'])==0 and int(r['changed_half_values_from_first'])==0 for r in chk)
 v=np.array([float(r['wall_ms'])for r in rows[80:]]);assert np.isfinite(v).all()and(v>0).all()
 hashes=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));assert len({h['sha256']for h in hashes})==1
 assert json.loads((d/'exit.json').read_text(encoding='utf-8-sig'))['exit_code']==0
 slots.append(dict(round=int(m[1]),slot=int(m[2]),arm=m[3],values=v,sha=hashes[0]['sha256'],source=str(d)))
assert len(slots)==30 and len({s['sha']for s in slots})==1
for rnd in [1,2,3]:
 seq=arms[rnd-1:]+arms[:rnd-1];seq+=seq[::-1];assert [s['arm']for s in sorted([s for s in slots if s['round']==rnd],key=lambda s:s['slot'])]==seq
out={}
for arm in arms[1:]:
 rr=[]
 for rnd in [1,2,3]:
  a=np.concatenate([s['values']for s in slots if s['round']==rnd and s['arm']=='stock']);b=np.concatenate([s['values']for s in slots if s['round']==rnd and s['arm']==arm]);rr.append(dict(round=rnd,base_ms=float(a.mean()),candidate_ms=float(b.mean()),delta_us=float((b.mean()-a.mean())*1000),p99_delta_us=float((np.quantile(b,.99)-np.quantile(a,.99))*1000)))
 a=np.concatenate([s['values']for s in slots if s['arm']=='stock']);b=np.concatenate([s['values']for s in slots if s['arm']==arm]);assert len(a)==len(b)==960
 out[arm]=dict(base_ms=float(a.mean()),mean_ms=float(b.mean()),delta_us=float((b.mean()-a.mean())*1000),p99_delta_us=float((np.quantile(b,.99)-np.quantile(a,.99))*1000),rounds=rr,samples_per_arm=len(a))
 out[arm]['advance']=all(r['delta_us']<0 for r in rr)and out[arm]['p99_delta_us']<0
ranked=sorted([k for k in out if out[k]['advance']],key=lambda k:out[k]['mean_ms'])
(p/(role+'-statistics.json')).write_text(json.dumps(dict(methods=out,ranked_for_independent_validation=ranked),indent=2)+'\n');print(json.dumps(out,indent=2));print('RANKED',ranked)
