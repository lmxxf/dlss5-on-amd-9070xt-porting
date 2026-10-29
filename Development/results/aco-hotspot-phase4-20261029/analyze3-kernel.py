from pathlib import Path
import json,csv,re,sys,collections,numpy as np
p=Path(__file__).parent;role=sys.argv[1];root=p/role/'evidence';target='c64_wave2:c128_wave2_bi_bo';stage=json.loads((p/'step2-hotspot-breakdown.json').read_text());cal=json.loads((p/'clock-calibration.json').read_text());slots=[]
for d in sorted(root.glob('kernel-*-r*-*')):
 m=re.fullmatch(r'kernel-(original1088|small1080)-r(\d+)-(\d+)-(baseline|candidate)',d.name);assert m;profile,rnd,slot,arm=m.groups();expected=153 if profile=='original1088'else 83;a=list(csv.DictReader((d/'kernels.csv').open()));assert len(a)==expected*2 and all(int(x['dispatches_total'])==expected for x in a);s=(d/'stderr.log').read_text(encoding='utf-8-sig');rate=int(re.search(r'DEVICE_REALTIME rate_khz=(\d+)',s)[1]);assert rate==100000
 wanted=sorted({x['dispatch']for x in stage[profile]['hip_per_dispatch']if x['kernel']==target});rr=[x for x in a if x['kernel']==target];by=collections.defaultdict(list)
 for x in rr:by[int(x['dispatch'])].append((int(x['end_tick'])-int(x['begin_tick'])-cal[profile]['stamp_pair_overhead_ticks_median'])/rate)
 assert sorted(by)==wanted and all(len(x)==2 for x in by.values());vals=sum(by.values(),[]);assert np.isfinite(vals).all()
 slots.append({'profile':profile,'round':int(rnd),'slot':int(slot),'arm':arm,'mean_per_call_ms':float(np.mean(vals)),'family_per_frame_ms':sum(float(np.mean(v))for v in by.values()),'calls_per_frame':len(by),'observations':len(vals),'values':vals})
assert len(slots)==24;out={}
for profile in ['original1088','small1080']:
 ss=[x for x in slots if x['profile']==profile];rounds=[]
 for rnd in [1,2,3]:
  r=sorted([x for x in ss if x['round']==rnd],key=lambda x:x['slot']);assert [x['arm']for x in r]==['baseline','candidate','candidate','baseline'];a=np.mean([x['family_per_frame_ms']for x in r if x['arm']=='baseline']);b=np.mean([x['family_per_frame_ms']for x in r if x['arm']=='candidate']);rounds.append({'round':rnd,'baseline_family_ms':float(a),'candidate_family_ms':float(b),'delta_family_ms':float(b-a)})
 aa=sum([x['values']for x in ss if x['arm']=='baseline'],[]);bb=sum([x['values']for x in ss if x['arm']=='candidate'],[]);a=float(np.mean(aa));b=float(np.mean(bb));out[profile]={'baseline_per_call_ms':a,'candidate_per_call_ms':b,'delta_per_call_ms':b-a,'delta_percent':100*(b/a-1),'delta_family_per_frame_ms':float(np.mean([x['delta_family_ms']for x in rounds])),'samples_per_arm':len(aa),'rounds':rounds}
(p/(role+'-kernel-statistics.json')).write_text(json.dumps({'target':target,'profiles':out,'slots':[{k:v for k,v in x.items()if k!='values'}for x in slots]},indent=2));print(json.dumps(out,indent=2))
