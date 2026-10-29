from pathlib import Path
import csv,json,re,collections,numpy as np
p=Path(__file__).parent;out={}
for profile,expected in [('original1088',153),('small1080',83)]:
 family=collections.defaultdict(list);mfamily=collections.defaultdict(list);dispatch=[];over=[];mover=[]
 for rnd in [1,2,3]:
  root=p/'step2-device/evidence';nd=root/f'{profile}-r{rnd}-N-profile';rows=list(csv.DictReader((nd/'kernels.csv').open()));byframe=collections.defaultdict(list)
  for x in rows:byframe[int(x['frame'])].append(x)
  assert len(byframe)==expected*6 and len(rows)==expected*6
  assert all(len(rr)==1 and int(rr[0]['dispatches_total'])==expected for rr in byframe.values())
  log=(nd/'stderr.log').read_text(encoding='utf-8-sig');rate_match=re.search(r'DEVICE_REALTIME rate_khz=(\d+) attribute=10017',log);assert rate_match;rate=int(rate_match[1]);assert rate==100000
  calibration=json.loads((p/'clock-calibration.json').read_text())[profile];bias=calibration['stamp_pair_overhead_ticks_median']
  totals=collections.Counter()
  for i in range(expected):
   rr=[x for x in rows if int(x['dispatch'])==i];assert len(rr)==6 and len({x['kernel']for x in rr})==1
   raw=[(int(x['end_tick'])-int(x['begin_tick']))/rate for x in rr];vals=[x-bias/rate for x in raw];assert np.isfinite(vals).all() and min(raw)>=0
   avg=float(np.mean(vals));k=rr[0]['kernel'];totals[k]+=avg;dispatch.append({'round':rnd,'dispatch':i,'kernel':k,'gpu_ms':avg,'raw_bracket_ms':float(np.mean(raw)),'observations':len(vals)})
  for k,v in totals.items():family[k].append(v)
  a=list(csv.DictReader((nd/'timing.csv').open()));b=list(csv.DictReader((root/f'{profile}-r{rnd}-N-control'/'timing.csv').open()));assert len(a)==expected*6 and len(b)==160
  network_clock=float(np.mean([(int(x['network_end_tick'])-int(x['network_begin_tick']))/rate for x in rows]))
  over.append({'round':rnd,'control_gpu_event_ms':float(np.mean([float(x['gpu_ms'])for x in b])),'control_cpu_ms':float(np.mean([float(x['cpu_ms'])for x in b])),'profile_cpu_ms':float(np.mean([float(x['cpu_ms'])for x in a])),'profile_gpu_event_ms':float(np.mean([float(x['gpu_ms'])for x in a])),'profile_network_realtime_ms':network_clock,'reconstructed_dispatch_sum_ms':sum(totals.values()),'reconstruction_ratio':sum(totals.values())/network_clock,'clock_rate_khz':rate,'stamp_overhead_ms':bias/rate})
  root=p/'step2-native/evidence'
  md=root/f'{profile}-r{rnd}-M-profile';s=(md/'stdout.log').read_text(encoding='utf-8-sig');parsed=[]
  for line in s.splitlines():
   m=re.fullmatch(r'\s*(\d+)\s+(\S+)\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s*',line)
   if m:parsed.append({'dispatch':int(m[1]),'kernel':m[2],'stage':m[3],'channels':int(m[4]),'groups':int(m[8]),'gpu_ms':float(m[9])/1000})
  assert parsed,(profile,rnd,'missing Vulkan dispatch grid')
  totals=collections.Counter()
  for x in parsed:totals[f"{x['stage']}/{x['kernel']}/C{x['channels']}"]+=x['gpu_ms']
  for k,v in totals.items():mfamily[k].append(v)
  counts=re.search(r'(\d+) dispatches, one submit:',s);assert counts and len(parsed)==int(counts[1])
  mc=(root/f'{profile}-r{rnd}-M-control'/'stdout.log').read_text(encoding='utf-8-sig');mmean=lambda s:float(re.search(r'GPU total ([\d.]+) ms over (\d+) frames',s)[1])/160;mover.append({'round':rnd,'control_ms':mmean(mc),'profile_ms':mmean(s),'dispatch_sum_ms':sum(x['gpu_ms']for x in parsed),'dispatches':len(parsed)})
 rows=[{'kernel':k,'mean_per_frame_ms':float(np.mean(v)),'rounds':len(v),'rounds_ms':v}for k,v in family.items()];rows.sort(key=lambda x:x['mean_per_frame_ms'],reverse=True);total=sum(x['mean_per_frame_ms']for x in rows)
 for x in rows:x['share_percent']=100*x['mean_per_frame_ms']/total
 mrows=sorted([{'stage_kernel':k,'mean_per_frame_ms':float(np.mean(v)),'rounds_ms':v}for k,v in mfamily.items()],key=lambda x:x['mean_per_frame_ms'],reverse=True)
 out[profile]={'hip':rows,'aco':mrows,'hip_dispatches':expected,'hip_diagnostic_dispatch_sum_ms':total,'hip_probe_effect':over,'aco_probe_effect':mover,'hip_per_dispatch':dispatch,'note':'Direct MSG_RTN_GET_REALTIME bracket kernels sample one business dispatch per frame; rate from hipDeviceAttributeWallClockRate, adjacent-stamp median subtracted without clipping. Raw brackets retained. Four diagnostic dispatches added per profiled frame. Family sums reconstruct separate frames; not one simultaneous per-frame trace. CPU wall controls quantify probe effects. Family sums are diagnostic shares, not uninstrumented kernel costs. ACO stages have different fusion/layout boundaries; compare grouped work, not arbitrary one-to-one kernel names.'}
(p/'step2-hotspot-breakdown.json').write_text(json.dumps(out,indent=2));print(json.dumps({k:{'hip_top':v['hip'][:6],'aco_top':v['aco'][:6],'probe':v['hip_probe_effect']}for k,v in out.items()},indent=2))
