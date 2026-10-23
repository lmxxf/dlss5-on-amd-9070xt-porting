#!/usr/bin/env python3
"""Local Small-profile ABBA analysis. 1152 and free1088 remain separate even at n640."""
from pathlib import Path
import argparse,csv,json,math,statistics,re
p=argparse.ArgumentParser();p.add_argument('--evidence',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--profiles',nargs='*',default=['small900','small720','small1080','smallfree1088']);a=p.parse_args();root=Path(__file__).resolve().parent
read=lambda f:f.read_text(encoding='utf-8-sig');load=lambda f:json.loads(read(f))
def quant(v,q):
 s=sorted(v);t=(len(s)-1)*q;i=int(t);f=t-i;return s[i]if i+1==len(s)else s[i]*(1-f)+s[i+1]*f
def stat(v):return {'n':len(v),'mean_ms':statistics.fmean(v),'median_ms':quant(v,.5),'p90_ms':quant(v,.9),'p99_ms':quant(v,.99),'max_ms':max(v)}
m=load(root/'perf-jobs.json');allresults={}
for profile in a.profiles:
 jobs=[x for x in m['jobs']if x['review_profile']==profile];assert len(jobs)==12,profile
 slots=[];pooled={'baseline':[],'candidate':[]}
 for j in jobs:
  d=a.evidence/j['label'].split('/')[-1]
  if not d.is_dir():d=a.evidence/j['label']
  assert load(d/'exit.json')['exit_code']==0
  rr=list(csv.DictReader((d/'out.csv').open()));assert len(rr)==240 and [int(x['frame'])for x in rr]==list(range(240));checked=[x for x in rr if x['checked']=='1'];assert [int(x['frame'])for x in checked]==[0,239]and all(int(x['invalid'])==0 for x in checked)
  v=[float(x['wall_ms'])for x in rr[80:]];assert len(v)==160 and all(math.isfinite(x)and x>0 for x in v)
  hashes=load(d/'raw-hashes.json');ref=a.evidence/f'gold-{profile}-p0-baseline'/'raw-hashes.json';rh=next(x['sha256'].lower()for x in load(ref)if x['name']=='out-first.f16');assert all(x['sha256'].lower()==rh for x in hashes if x['name'].endswith('.f16'))
  st=read(d/'stderr.log');assert 'model requested=0.5x effective=0.5x blocks=31'in st
  vf=re.search(r'VIT_VFRAG requested=(\d+) available=(\d+) explicit_ae0=(\d+) producers=(\d+) consumers=(\d+)',st);assert vf;req,avail,aeoff,pr,co=map(int,vf.groups());assert req==avail==aeoff==1 and pr==co
  if j['review_arm']=='candidate':assert pr>=8*240
  else:assert pr==(8 if profile=='small1080'else 0)
  pooled[j['review_arm']]+=v;slots.append({'label':j['label'],'round':j['round'],'slot':j['slot'],'arm':j['review_arm'],'stats':stat(v),'samples':v})
 rounds=[]
 for r in (1,2,3):
  z=sorted([x for x in slots if x['round']==r],key=lambda x:x['slot']);assert [x['arm']for x in z]==['baseline','candidate','candidate','baseline'];b=stat(z[0]['samples']+z[3]['samples']);c=stat(z[1]['samples']+z[2]['samples']);rounds.append({'round':r,'baseline':b,'candidate':c,'delta_mean_ms':c['mean_ms']-b['mean_ms'],'delta_p99_ms':c['p99_ms']-b['p99_ms'],'baseline_drift_ms':z[3]['stats']['mean_ms']-z[0]['stats']['mean_ms'],'candidate_drift_ms':z[2]['stats']['mean_ms']-z[1]['stats']['mean_ms']})
 b=stat(pooled['baseline']);c=stat(pooled['candidate']);assert b['n']==c['n']==960
 allresults[profile]={'processing':jobs[0]['expected_processing'],'tokens':jobs[0]['expected_tokens'],'AE_mode':0,'baseline':b,'candidate':c,'delta_mean_ms':c['mean_ms']-b['mean_ms'],'delta_p99_ms':c['p99_ms']-b['p99_ms'],'rounds':rounds,'all_round_means_improve':all(x['delta_mean_ms']<0 for x in rounds),'all_round_p99_nonregress':all(x['delta_p99_ms']<=0 for x in rounds),'slots':[{k:v for k,v in x.items()if k!='samples'}for x in slots]}
a.output.write_text(json.dumps({'profiles':allresults,'warmup_frames_discarded_per_slot':80,'steady_frames_per_slot':160,'quantile':'linear interpolation (type7)','note':'Current complete Frame wall time; each profile separate. small1080=1152 and smallfree1088=free input1072 are not pooled. AE1 correctness/reuse is separate, these AE0 timings do not claim steady reuse speedup.'},indent=2)+'\n');print(json.dumps({k:{'delta_mean_ms':v['delta_mean_ms'],'delta_p99_ms':v['delta_p99_ms']}for k,v in allresults.items()},indent=2))
