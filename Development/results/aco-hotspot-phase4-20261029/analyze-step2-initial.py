from pathlib import Path
import csv,json,re,collections,numpy as np
p=Path(__file__).parent;out={}
for profile,expected in [('original1088',153),('small1080',83)]:
 family=collections.defaultdict(list);mfamily=collections.defaultdict(list);dispatch=[];over=[];mover=[]
 for rnd in [1,2,3]:
  root=p/'step2/evidence';nd=root/f'{profile}-r{rnd}-N-profile';rows=list(csv.DictReader((nd/'kernels.csv').open()));byframe=collections.defaultdict(list)
  for x in rows:byframe[int(x['frame'])].append(x)
  assert len(byframe)==32
  for frame,rr in byframe.items():
   assert len(rr)==expected,(profile,frame,len(rr));tot=collections.Counter()
   for x in rr:
    v=float(x['gpu_ms']);assert np.isfinite(v)and v>=0;tot[x['kernel']]+=v
   for k,v in tot.items():family[k].append(v)
  a=list(csv.DictReader((nd/'timing.csv').open()));b=list(csv.DictReader((root/f'{profile}-r{rnd}-N-control'/'timing.csv').open()));over.append({'round':rnd,'control_ms':float(np.mean([float(x['gpu_ms'])for x in b])),'profile_ms':float(np.mean([float(x['gpu_ms'])for x in a])),'dispatch_sum_ms':sum(float(x['gpu_ms'])for x in rows)/32})
  for i in range(expected):
   rr=[x for x in rows if int(x['dispatch'])==i];assert len(rr)==32 and len({x['kernel']for x in rr})==1;dispatch.append({'round':rnd,'dispatch':i,'kernel':rr[0]['kernel'],'gpu_ms':float(np.mean([float(x['gpu_ms'])for x in rr]))})
  md=root/f'{profile}-r{rnd}-M-profile';s=(md/'stdout.log').read_text(encoding='utf-8-sig');parsed=[]
  for line in s.splitlines():
   m=re.fullmatch(r'\s*(\d+)\s+(\S+)\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s*',line)
   if m:parsed.append({'dispatch':int(m[1]),'kernel':m[2],'stage':m[3],'channels':int(m[4]),'groups':int(m[8]),'gpu_ms':float(m[9])/1000})
  assert parsed,(profile,rnd,'missing Vulkan dispatch grid')
  totals=collections.Counter()
  for x in parsed:totals[f"{x['stage']}/{x['kernel']}/C{x['channels']}"]+=x['gpu_ms']
  for k,v in totals.items():mfamily[k].append(v)
  counts=re.search(r'(\d+) dispatches, one submit:',s);assert counts and len(parsed)==int(counts[1])
  mc=(root/f'{profile}-r{rnd}-M-control'/'stdout.log').read_text(encoding='utf-8-sig');mmean=lambda s:float(re.search(r'GPU total ([\d.]+) ms over (\d+) frames',s)[1])/32;mover.append({'round':rnd,'control_ms':mmean(mc),'profile_ms':mmean(s),'dispatch_sum_ms':sum(x['gpu_ms']for x in parsed),'dispatches':len(parsed)})
 rows=[{'kernel':k,'mean_per_frame_ms':float(np.mean(v)),'p99_per_frame_ms':float(np.quantile(v,.99)),'frames':len(v)}for k,v in family.items()];rows.sort(key=lambda x:x['mean_per_frame_ms'],reverse=True);total=sum(x['mean_per_frame_ms']for x in rows)
 for x in rows:x['share_percent']=100*x['mean_per_frame_ms']/total
 mrows=sorted([{'stage_kernel':k,'mean_per_frame_ms':float(np.mean(v)),'rounds_ms':v}for k,v in mfamily.items()],key=lambda x:x['mean_per_frame_ms'],reverse=True)
 out[profile]={'hip':rows,'aco':mrows,'hip_dispatches':expected,'hip_diagnostic_dispatch_sum_ms':total,'hip_probe_effect':over,'aco_probe_effect':mover,'hip_per_dispatch':dispatch,'note':'In-frame diagnostic timestamps perturb submission/overlap. Family sums are diagnostic shares, not uninstrumented kernel costs. ACO stages have different fusion/layout boundaries; compare grouped work, not arbitrary one-to-one kernel names.'}
(p/'step2-hotspot-breakdown.json').write_text(json.dumps(out,indent=2));print(json.dumps({k:{'hip_top':v['hip'][:6],'aco_top':v['aco'][:6],'probe':v['hip_probe_effect']}for k,v in out.items()},indent=2))
