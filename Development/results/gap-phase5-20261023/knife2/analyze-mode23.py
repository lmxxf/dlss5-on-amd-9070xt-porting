#!/usr/bin/env python3
"""Local AE2/3 actual-state checks. Do not require reuse from force-compute mode2."""
from pathlib import Path
import argparse,json,csv,re,math,hashlib
p=argparse.ArgumentParser();p.add_argument('--evidence',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();P=Path(__file__).resolve().parent
read=lambda f:f.read_text(encoding='utf-8-sig');load=lambda f:json.loads(read(f));m=load(P/'ae-mode23-jobs.json');pairs={};results=[]
for j in m['jobs']:
 d=a.evidence/j['label'].split('/')[-1]
 if not d.is_dir():d=a.evidence/j['label']
 assert load(d/'exit.json')['exit_code']==0,j['label']
 rows=list(csv.DictReader((d/'out.csv').open()));assert len(rows)==6 and [int(x['frame'])for x in rows]==list(range(6));assert all(x['checked']=='1'and int(x['invalid'])==0 and math.isfinite(float(x['wall_ms']))for x in rows)
 flags={x.split('=',1)[0]:x.split('=',1)[1]for x in read(d/'same.flags').splitlines()if '='in x and not x.startswith('#')};mode=j['review_ae_mode'];assert flags['DLSS5_VIT_ADAPTIVE']==str(mode)and flags['DLSS5_VIT_ADAPTIVE_IDLE_MS']=='86400000'
 hs={}
 for x in load(d/'raw-hashes.json'):
  q=re.fullmatch(r'out-frame-(\d+)\.f16',x['name'])
  if q:
   hs[int(q[1])]=x['sha256'].lower();f=d/x['name']
   if f.exists():assert hashlib.sha256(f.read_bytes()).hexdigest()==x['sha256'].lower()
 assert set(hs)==set(range(6))
 st=read(d/'stderr.log');expected='model requested=0.5x effective=0.5x blocks=31'if j['expected_small']else'model requested=1x effective=1x blocks=71';assert expected in st and 'model requested=0.5x effective=1x'not in st
 hip=read(d/'DLSS5-AMD/logs/native-hip.txt');proc=re.findall(r'\bprocessing=(\d+x\d+)',hip);proc=[v for i,v in enumerate(proc)if i==0 or v!=proc[i-1]];assert proc==['x'.join(map(str,j['expected_processing']))]
 vf=re.search(r'VIT_VFRAG requested=(\d+) available=(\d+) explicit_ae0=(\d+) producers=(\d+) consumers=(\d+)',st);assert vf;req,av,ae0,pr,co=map(int,vf.groups());assert req==av==1 and ae0==0 and pr==co
 if j['review_arm']=='candidate':assert pr>=48
 else:assert pr==0
 ae=list(csv.reader((d/'adaptive.csv').open()));assert len(ae)>=6 and all(len(x)==7 for x in ae);actual=ae[-6:];states=[int(x[1])for x in actual];reasons=[int(x[3])for x in actual]
 if mode==2:
  assert states==[0]*6,(j['label'],'mode2 must force actual compute',states)
  assert 2 in reasons and all(x in (1,2)for x in reasons),(j['label'],'mode2 reason evidence',reasons)
 else:
  assert 0 in states and 1 in states,(j['label'],'mode3 must observe both actual compute and reuse',states)
  assert all(reason==0 for state,reason in zip(states,reasons)if state==1),(j['label'],'mode3 reuse reason must use original automatic state',reasons)
 result={'label':j['label'],'profile':j['review_profile'],'arm':j['review_arm'],'AE_mode':mode,'frames':6,'processing':proc[0],'frame_sha256':hs,'actual_frame_states':states,'actual_frame_reasons':reasons,'compute_frames':states.count(0),'reuse_frames':states.count(1),'route_launches':pr,'route_launches_are_not_execution_count':True,'ae_csv':ae}
 pairs.setdefault((mode,j['review_profile']),{})[j['review_arm']]=result;results.append(result)
for key,x in pairs.items():
 assert set(x)=={'baseline','candidate'}
 assert x['baseline']['frame_sha256']==x['candidate']['frame_sha256'],(key,'cross-arm per-frame output mismatch')
 assert x['baseline']['ae_csv']==x['candidate']['ae_csv'],(key,'original AE decision/statistics changed')
report={'pass':True,'paired_cases':len(pairs),'jobs':len(results),'checked_frames':48,'controlled_idle_ms':86400000,'production_idle_unchanged':True,'note':'Mode2 all actual frames compute; mode3 contains compute and reuse. Per-frame cross-arm SHA plus AE CSV, never all-frames-to-first. Route counts are launches. No GPU is run by this analyzer.','cases':results};a.output.write_text(json.dumps(report,indent=2)+'\n');print('MODE23_PASS 4 paired cases / 8 jobs / 48 checked frames')
