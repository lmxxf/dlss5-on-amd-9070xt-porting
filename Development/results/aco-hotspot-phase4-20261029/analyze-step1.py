from pathlib import Path
import json,csv,re,numpy as np
p=Path(__file__).parent
load=lambda f:json.loads(f.read_text(encoding='utf-8-sig'))
slots=[]
for d in sorted((p/'step1-nmmn/evidence').iterdir()):
 m=re.fullmatch(r'(original1088|small1080|small900)-r(\d+)-(\d+)-(N|M)',d.name)
 if not m:continue
 assert load(d/'exit.json')['exit_code']==0;s=(d/'stdout.log').read_text(encoding='utf-8-sig');profile,rnd,slot,kind=m.groups();raw=load(d/'raw-hashes.json');raw=[raw]if isinstance(raw,dict)else raw;assert len({x['sha256']for x in raw})==1
 if kind=='N':
  a=list(csv.DictReader((d/'timing.csv').open()));assert len(a)==160 and 'raw_bitdiff=0 output_finite=1'in s;v=np.array([float(x['gpu_ms'])for x in a]);gpu=float(v.mean());cpu=float(np.mean([float(x['cpu_ms'])for x in a]));p99=float(np.quantile(v,.99));assert f'ACTUAL_MODEL requested={0 if profile.startswith("small")else 1} effective={0 if profile.startswith("small")else 1}'in s
 else:
  t=re.search(r'measurement wall ([0-9.]+) ms, GPU total ([0-9.]+) ms over (\d+) frames',s);assert t and int(t[3])==160 and 'ACO network: pipeline binaries on AMD Radeon RX 9070 XT'in s;gpu=float(t[2])/160;cpu=float(t[1])/160;p99=None
 slots.append({'profile':profile,'round':int(rnd),'slot':int(slot),'kind':kind,'gpu_ms':gpu,'cpu_ms':cpu,'p99_ms':p99,'raw_sha256':raw[0]['sha256']})
assert len(slots)==36;tiers={}
for name in ['original1088','small1080','small900']:
 ss=[s for s in slots if s['profile']==name];rounds=[]
 for rnd in [1,2,3]:
  x=sorted([s for s in ss if s['round']==rnd],key=lambda s:s['slot']);assert ''.join(s['kind']for s in x)=='NMMN';n=float(np.mean([s['gpu_ms']for s in x if s['kind']=='N']));m=float(np.mean([s['gpu_ms']for s in x if s['kind']=='M']));rounds.append({'round':rnd,'hip_ms':n,'aco_ms':m,'gap_ms':n-m})
 n=float(np.mean([x['hip_ms']for x in rounds]));m=float(np.mean([x['aco_ms']for x in rounds]));tiers[name]={'hip_ms':n,'aco_ms':m,'gap_ms':n-m,'hip_slower_percent':100*(n/m-1),'rounds':rounds,'steady_samples_per_arm':960}
 for kind in ['N','M']:assert len({x['raw_sha256']for x in ss if x['kind']==kind})==1
splits={}
for name in ['small900','small1080']:
 diag=[];control=[];rounds=[]
 for rnd in [1,2,3]:
  dr=[];br=[]
  for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
   d=p/'step1-split/evidence'/f'perf-{name}-r{rnd}-{slot}-{arm}';a=list(csv.DictReader((d/'out.csv').open()));assert len(a)==240;assert all(int(x['invalid'])==0 and int(x['changed_half_values_from_first'])==0 for x in a if x['checked']=='1');wall=np.array([float(x['wall_ms'])for x in a[80:]])
   if arm=='baseline':control.extend(wall);br.extend(wall)
   else:
    st=list(csv.DictReader((d/'out-stages.csv').open()));assert len(st)==240
    for x in st[80:]:diag.append({k:float(v)for k,v in x.items()if k!='frame'})
    dr.extend(wall)
  rounds.append({'round':rnd,'control_frame_wall_ms':float(np.mean(br)),'diagnostic_frame_wall_ms':float(np.mean(dr))})
 assert len(diag)==960 and len(control)==960
 means={k:float(np.mean([x[k]for x in diag]))for k in diag[0]};pp=sum(means[k]for k in ['encode_ms','input_ms','neural_ms','decode_ms','copy_ms']);splits[name]={'means_ms':means,'pre_post_gpu_ms':pp,'handoff_and_submission_gap_ms':means['bridge_network_ms']-means['nn_gpu_ms'],'frame_wall_minus_nn_gpu_ms':means['frame_wall_ms']-means['nn_gpu_ms'],'control_frame_wall_ms':float(np.mean(control)),'diagnostic_wall_delta_ms':means['frame_wall_ms']-float(np.mean(control)),'rounds':rounds,'samples_per_arm':960,'warning':'Frame-wall residual includes CPU/cross-API synchronization; not all GPU pre/post work. Matched capture split is separate from synthetic NMMN fixture.'}
(p/'step1-statistics.json').write_text(json.dumps({'nmmn':tiers,'slots':slots,'small_frame_split':splits},indent=2));print(json.dumps({'nmmn':tiers,'small_frame_split':splits},indent=2))
