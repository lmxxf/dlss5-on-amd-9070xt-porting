from pathlib import Path
import json,csv,re,sys
p=Path(__file__).parent;arm=sys.argv[1];case='final-config-'+arm;root=p/case/'evidence';manifest=json.loads((p/(case+'.json')).read_text());pairs={}
for j in manifest['jobs']:
 name=j['label'].split('/')[-1];key,side=name.rsplit('-',1);pairs.setdefault(key,{})[side]=j
assert len(pairs)==20
out=[]
for name,jobs in sorted(pairs.items()):
 dirs={side:root/j['label'].split('/')[-1]for side,j in jobs.items()};hashes={};row={'configuration':name}
 for side,d in dirs.items():
  assert json.loads((d/'exit.json').read_text(encoding='utf-8-sig'))['exit_code']==0
  h=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));h=h if isinstance(h,list)else[h];hashes[side]={x['name']:x['sha256']for x in h}
  rows=list(csv.DictReader((d/'out.csv').open()));assert len(rows)==jobs[side]['frame_count'];assert all(x['checked']=='1'and int(x['invalid'])==0 for x in rows)
 assert hashes['baseline']==hashes['candidate'],name;row.update(file_pairs=len(hashes['baseline']),bit_diff=0)
 if name.startswith('reuse-'):
  a=(dirs['baseline']/'adaptive.csv').read_text(encoding='utf-8-sig');b=(dirs['candidate']/'adaptive.csv').read_text(encoding='utf-8-sig');assert a==b,name;r=list(csv.reader(a.splitlines()))[-6:];states=[int(x[1])for x in r];reasons=[int(x[3])for x in r];assert 0 in states
  if 'ae3' in name:assert 1 in states and states[-1]==0 and 3 in reasons,name
  row.update(adaptive_csv_identical=True,states=states,reasons=reasons)
 if name.startswith('mode1-'):
  state=[]
  for side in ['baseline','candidate']:
   s=(dirs[side]/'stderr.log').read_text(encoding='utf-8-sig');h=[tuple(map(int,m))for m in re.findall(r'fast_enabled=(\d+) fast_use=(\d+) fast_seed=(\d+)',s)];assert len(h)==6 and all(x[0]==1 for x in h)and any(x[1]for x in h);state.append(h)
  assert state[0]==state[1],name;row.update(history_states=state[0],scope='MODE1 static fallback; no valid motion/depth metadata')
 if name.startswith('f9-'):
  unique=len({v for k,v in hashes['baseline'].items()if k.startswith('out-frame-')});assert unique>=3,name;row['distinct_frame_hashes']=unique
 out.append(row)
result=dict(arm=arm,configurations=len(out),file_pairs_compared=sum(x['file_pairs']for x in out),all_paired_bit_diff_zero=True,cases=out,history_harness_controls='selected-regression-verdict.json')
(p/'final-configuration-verdict.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
