#!/usr/bin/env python3
"""Local-only postcheck; runner ALLDONE is not cross-arm numerical acceptance."""
from pathlib import Path
import argparse,csv,json,re,hashlib,math
p=argparse.ArgumentParser();p.add_argument('--evidence',type=Path,required=True);p.add_argument('--kind',choices=['gold','ae','all'],default='all');p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=Path(__file__).resolve().parent
read=lambda f:f.read_text(encoding='utf-8-sig');load=lambda f:json.loads(read(f))
def case(label):
 d=a.evidence/label.split('/')[-1]
 return d if d.is_dir()else a.evidence/label
pairs={};results=[]
for kind in (['gold','ae']if a.kind=='all'else[a.kind]):
 m=load(root/(kind+'-jobs.json'))
 for j in m['jobs']:
  d=case(j['label']);assert load(d/'exit.json')['exit_code']==0,j['label'];rr=list(csv.DictReader((d/'out.csv').open()));n=j['frame_count'];assert len(rr)==n and [int(x['frame'])for x in rr]==list(range(n));assert all(x['checked']=='1'and int(x['invalid'])==0 and math.isfinite(float(x['wall_ms']))for x in rr)
  hs={}
  for x in load(d/'raw-hashes.json'):
   z=re.fullmatch(r'out-frame-(\d+)\.f16',x['name'])
   if z:
    i=int(z[1]);hs[i]=x['sha256'].lower();local=d/x['name']
    if local.exists():assert hashlib.sha256(local.read_bytes()).hexdigest()==hs[i]
  assert set(hs)==set(range(n)),(j['label'],'missing per-frame SHA')
  st=read(d/'stderr.log');models=re.findall(r'model requested=(\S+) effective=(\S+) blocks=(\d+)',st)
  expected=('0.5x','0.5x','31')if j['expected_small']else('1x','1x','71');assert expected in models;assert ('0.5x','1x','71')not in models
  hip=read(d/'DLSS5-AMD/logs/native-hip.txt');proc=re.findall(r'\bprocessing=(\d+x\d+)',hip);proc=[v for i,v in enumerate(proc)if i==0 or v!=proc[i-1]];assert proc==['x'.join(map(str,j['expected_processing']))],(j['label'],'actual processing',proc)
  vf=re.findall(r'VIT_VFRAG requested=(\d+) available=(\d+) explicit_ae0=(\d+) producers=(\d+) consumers=(\d+)',st);assert vf and len(vf)==1
  req,avail,aeoff,pr,co=map(int,vf[0]);assert req==avail==1 and pr==co
  if j['review_arm']=='candidate':assert pr>=8*n,(j['label'],'paired route not observed',pr)
  elif kind=='ae':assert pr==0,(j['label'],'baseline AE should retain old V format')
  elif j['review_profile']=='small1080':assert pr==8 and 'model_prepared mode=1 'in st
  else:assert pr==0
  result={'label':j['label'],'kind':kind,'profile':j['review_profile'],'arm':j['review_arm'],'pattern':j['review_pattern'],'frames':n,'processing':proc[0],'expected_tokens_by_verified_geometry':j['expected_tokens'],'frame_sha256':hs,'vfrag_route_launches':pr,'route_launches_are_not_execution_count':True}
  if kind=='ae':
   ae=list(csv.reader((d/'adaptive.csv').open()));assert len(ae)>=n and all(len(x)==7 for x in ae)
   actual=ae[-n:];states=[int(x[1])for x in actual];assert 0 in states and 1 in states,(j['label'],'must observe both compute and reuse among actual last N frames',states)
   if j['expected_small']:assert 'Small AE gain logical_blocks=11..18 asset_roles=31..38'in st
   result.update(ae_csv=ae,actual_frame_ae_states=states,actual_compute_frames=states.count(0),actual_reuse_frames=states.count(1))
  key=(kind,j['review_profile'],j['review_pattern']);pairs.setdefault(key,{})[j['review_arm']]=result;results.append(result)
for key,arms in pairs.items():
 assert set(arms)=={'baseline','candidate'},(key,'missing arm')
 assert arms['baseline']['frame_sha256']==arms['candidate']['frame_sha256'],(key,'cross-arm frame-index mismatch')
 if key[0]=='ae':assert arms['baseline']['ae_csv']==arms['candidate']['ae_csv'],(key,'AE decisions/metrics/anchors changed')
report={'pass':True,'kind':a.kind,'paired_cases':len(pairs),'jobs':len(results),'checked_frames':sum(x['frames']for x in results),'note':'Cross-arm each-frame SHA required. AE may change between frames; never compares all AE frames to first. Last N AE CSV rows establish actual compute/reuse; route_calls count launches only. No performance inference.','cases':results};a.output.write_text(json.dumps(report,indent=2)+'\n');print(f'PASS local cross-arm {len(pairs)} pairs / {report["checked_frames"]} frames')
