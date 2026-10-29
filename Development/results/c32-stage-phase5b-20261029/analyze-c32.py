from pathlib import Path
import json,csv,re,sys,collections,numpy as np
p=Path(__file__).parent;role=sys.argv[1];slots=[]
for d in sorted((p/role/'evidence').glob('kernel-*-r*-*')):
 m=re.fullmatch(r'kernel-(\w+)-r(\d+)-(\d+)-(baseline|candidate)',d.name);assert m;profile,rnd,slot,arm=m.groups();a=list(csv.DictReader((d/'kernels.csv').open()));num=int(a[0]['dispatches_total']);assert len(a)==num*2 and all(int(x['dispatches_total'])==num for x in a);s=(d/'stderr.log').read_text(encoding='utf-8-sig');rate=int(re.search(r'DEVICE_REALTIME rate_khz=(\d+)',s)[1]);assert rate==100000;by=collections.defaultdict(list)
 for x in a:by[int(x['dispatch'])].append(x)
 assert len(by)==num and all(len(v)==2 for v in by.values());fn=collections.Counter()
 for rr in by.values():
  assert len({x['kernel']for x in rr})==1;k=rr[0]['kernel'];v=float(np.mean([(int(x['end_tick'])-int(x['begin_tick'])-80)/rate for x in rr]));k=k.replace('c32_norm900:','c32_wave1:')
  if k.startswith('c32_wave1:')or k=='mh_fast:mh_pool_project_c32_b8_out8':fn[k]+=v
 slots.append({'profile':profile,'round':int(rnd),'slot':int(slot),'arm':arm,'stage_ms':sum(fn.values()),'kernels_ms':dict(fn)})
assert slots;out={}
for profile in sorted({x['profile']for x in slots}):
 ss=[x for x in slots if x['profile']==profile];assert len(ss)==12;rr=[]
 for rnd in [1,2,3]:
  z=sorted([x for x in ss if x['round']==rnd],key=lambda x:x['slot']);assert [x['arm']for x in z]==['baseline','candidate','candidate','baseline'];a=float(np.mean([x['stage_ms']for x in z if x['arm']=='baseline']));b=float(np.mean([x['stage_ms']for x in z if x['arm']=='candidate']));rr.append({'round':rnd,'baseline_ms':a,'candidate_ms':b,'delta_ms':b-a})
 funcs={}
 for k in sorted(set().union(*(set(x['kernels_ms'])for x in ss))):
  a=float(np.mean([x['kernels_ms'].get(k,0)for x in ss if x['arm']=='baseline']));b=float(np.mean([x['kernels_ms'].get(k,0)for x in ss if x['arm']=='candidate']));funcs[k]={'baseline_ms':a,'candidate_ms':b,'delta_ms':b-a}
 out[profile]={'baseline_stage_ms':float(np.mean([x['baseline_ms']for x in rr])),'candidate_stage_ms':float(np.mean([x['candidate_ms']for x in rr])),'delta_stage_ms':float(np.mean([x['delta_ms']for x in rr])),'rounds':rr,'kernels':funcs}
(p/(role+'-c32-statistics.json')).write_text(json.dumps({'profiles':out,'slots':slots,'method':'device realtime 100MHz, adjacent stamp correction80ticks, separate-frame reconstruction'},indent=2));print(json.dumps(out,indent=2))
