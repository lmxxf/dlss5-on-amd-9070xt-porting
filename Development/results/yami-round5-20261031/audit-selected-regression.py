from pathlib import Path
import json,csv,re
p=Path(__file__).parent
cases=[('selected-regression',n)for n in ['fast0-original1088','fast0-small1080','ae1-original1088','ae1-small1080','ae3-original1088','ae3-small1080']]+[('selected-regression-rest',n)for n in ['mode1-original1088','mode1-small1080','f9-switch-1080','free-small-896x512','free-orig-896x512','reuse-ae1-original1088','reuse-ae1-small1080','reuse-ae3-original1088','reuse-ae3-small1080']]
def hashes(d):
 assert json.loads((d/'exit.json').read_text(encoding='utf-8-sig'))['exit_code']==0
 a=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));a=a if isinstance(a,list)else[a];return{x['name']:x['sha256']for x in a}
def history(d):
 s=(d/'stderr.log').read_text(encoding='utf-8-sig');r=[dict(zip(['enabled','use','seed'],map(int,m)))for m in re.findall(r'fast_enabled=(\d+) fast_use=(\d+) fast_seed=(\d+)',s)];assert len(r)==6 and all(x['enabled']==1 for x in r),d;return r
rows=[]
for case,name in cases:
 root=p/case/'evidence';a=root/(name+'-baseline');b=root/(name+'-candidate');ha,hb=hashes(a),hashes(b);assert ha==hb,(case,name)
 row=dict(configuration=name,source_case=case,file_pairs=len(ha),bit_diff=0)
 if name.startswith('reuse-'):
  ca=(a/'adaptive.csv').read_text(encoding='utf-8-sig');cb=(b/'adaptive.csv').read_text(encoding='utf-8-sig');assert ca==cb,name
  x=list(csv.reader(ca.splitlines()))[-6:];assert len(x)==6;states=[int(z[1])for z in x];reasons=[int(z[3])for z in x];assert 0 in states and 1 in states and states[-1]==0 and any(k in reasons for k in [3,6]),name
  row.update(adaptive_csv_identical=True,states=states,reasons=reasons)
 if name.startswith('mode1-'):
  x,y=history(a),history(b);assert x==y and any(z['use']for z in x);row['history_states']=x;row['scope']='MODE1 static fallback without valid depth/motion metadata'
 rows.append(row)
controls={};root=p/'selected-regression-rest/evidence'
for prof in ['original1088','small1080']:
 a=root/('history-fixed-'+prof);b=root/('history-reset-'+prof);ha,hb=hashes(a),hashes(b);x,y=history(a),history(b);assert all(z['seed']==0 for z in x+y);assert any(z['use']for z in x)and not any(z['use']for z in y)
 assert ha['out-frame-0.f16']==hb['out-frame-0.f16'];different=[i for i in range(1,6)if ha[f'out-frame-{i}.f16']!=hb[f'out-frame-{i}.f16']];assert different,prof
 controls[prof]=dict(fixed_seed=0,continuous_use=[z['use']for z in x],reset_use=[z['use']for z in y],frame0_equal=True,later_different_frames=different)
out=dict(configurations=len(rows),file_pairs_compared=sum(r['file_pairs']for r in rows),cases=rows,history_reset_controls=controls,scope='combined ViT perm fix + C512 probability clamp removal; no C32 guard or C256 single-plane in this run')
(p/'selected-regression-verdict.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
