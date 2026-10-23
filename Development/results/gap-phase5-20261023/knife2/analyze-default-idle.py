#!/usr/bin/env python3
"""Record real default500ms AE behavior without misclassifying wall-dependent decisions."""
from pathlib import Path
import argparse,json,csv,re,math
p=argparse.ArgumentParser();p.add_argument('--evidence',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=Path(__file__).resolve().parent
read=lambda f:f.read_text(encoding='utf-8-sig');load=lambda f:json.loads(read(f));m=load(root/'ae-default-idle-jobs.json');pairs={}
for j in m['jobs']:
 d=a.evidence/j['label'].split('/')[-1]
 if not d.is_dir():d=a.evidence/j['label']
 assert load(d/'exit.json')['exit_code']==0
 rows=list(csv.DictReader((d/'out.csv').open()));assert len(rows)==6 and all(x['checked']=='1'and int(x['invalid'])==0 for x in rows)
 assert 'DLSS5_VIT_ADAPTIVE_IDLE_MS='not in read(d/'same.flags')
 hs={int(z[1]):x['sha256'].lower()for x in load(d/'raw-hashes.json')if(z:=re.fullmatch(r'out-frame-(\d+)\.f16',x['name']))};assert set(hs)==set(range(6))
 ae=list(csv.reader((d/'adaptive.csv').open()));assert len(ae)>=6 and all(len(x)==7 for x in ae)
 vf=re.search(r'VIT_VFRAG requested=(\d+) available=(\d+) explicit_ae0=(\d+) producers=(\d+) consumers=(\d+)',read(d/'stderr.log'));assert vf;v=list(map(int,vf.groups()));assert v[0]==v[1]==1 and v[3]==v[4]
 if j['review_arm']=='candidate':assert v[3]>=48
 else:assert v[3]==0
 states=[int(x[1])for x in ae[-6:]]
 pairs.setdefault(j['review_profile'],{})[j['review_arm']]={'hashes':hs,'ae_csv':ae,'actual_frame_states':states,'compute_frames':states.count(0),'reuse_frames':states.count(1),'wall_ms':[float(x['wall_ms'])for x in rows],'route_launches':v[3]}
for profile,x in pairs.items():
 assert set(x)=={'baseline','candidate'}
 x['outputs_equal']=x['baseline']['hashes']==x['candidate']['hashes'];x['decisions_and_metrics_equal']=x['baseline']['ae_csv']==x['candidate']['ae_csv']
 x['interpretation']='Default-idle observations only. If wall-driven decisions differ, do not attribute output/state differences to V layout without a controlled comparison.'
a.output.write_text(json.dumps({'default_idle_ms':500,'idle_override_absent':True,'profiles':pairs,'controlled_numerical_gate':'Use ae-jobs with fixed long idle separately. This report records wall-dependent default behavior, not a forced identical-decision gate.','performance_inference':False},indent=2)+'\n');print('RECORDED default-idle observations; no automatic layout-failure inference from state mismatch')
