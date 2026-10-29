from pathlib import Path
import json,csv,re,sys,numpy as np
p=Path(__file__).parent;case=sys.argv[1]if len(sys.argv)>1 else'final-nmmn';slots=[]
for d in sorted((p/case/'evidence').iterdir()):
 m=re.fullmatch(r'(original1088|small1080|small900)-r(\d+)-(\d+)-(N|M)',d.name)
 if not m:continue
 profile,rnd,slot,kind=m.groups();assert json.loads((d/'exit.json').read_text(encoding='utf-8-sig'))['exit_code']==0;s=(d/'stdout.log').read_text(encoding='utf-8-sig');raw=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));raw=[raw]if isinstance(raw,dict)else raw;assert len({x['sha256']for x in raw})==1
 if kind=='N':
  a=list(csv.DictReader((d/'timing.csv').open()));assert len(a)==160 and'raw_bitdiff=0 output_finite=1'in s;v=np.array([float(x['gpu_ms'])for x in a]);assert np.isfinite(v).all()and(v>0).all();gpu=float(v.mean());cpu=float(np.mean([float(x['cpu_ms'])for x in a]));p99=float(np.quantile(v,.99));assert f'ACTUAL_MODEL requested={0 if profile.startswith("small")else 1} effective={0 if profile.startswith("small")else 1}'in s
 else:
  t=re.search(r'measurement wall ([0-9.]+) ms, GPU total ([0-9.]+) ms over (\d+) frames',s);assert t and int(t[3])==160 and'ACO network: pipeline binaries on AMD Radeon RX 9070 XT'in s;gpu=float(t[2])/160;cpu=float(t[1])/160;p99=None
 slots.append({'profile':profile,'round':int(rnd),'slot':int(slot),'kind':kind,'gpu_ms':gpu,'cpu_ms':cpu,'p99_ms':p99,'raw_sha256':raw[0]['sha256']})
assert len(slots)==36;tiers={}
for name in ['original1088','small1080','small900']:
 ss=[s for s in slots if s['profile']==name];rounds=[]
 for rnd in [1,2,3]:
  x=sorted([s for s in ss if s['round']==rnd],key=lambda s:s['slot']);assert ''.join(s['kind']for s in x)=='NMMN';n=float(np.mean([s['gpu_ms']for s in x if s['kind']=='N']));m=float(np.mean([s['gpu_ms']for s in x if s['kind']=='M']));rounds.append({'round':rnd,'hip_ms':n,'aco_ms':m,'gap_ms':n-m})
 n=float(np.mean([x['hip_ms']for x in rounds]));m=float(np.mean([x['aco_ms']for x in rounds]));tiers[name]={'hip_ms':n,'aco_ms':m,'gap_ms':n-m,'hip_slower_percent':100*(n/m-1),'rounds':rounds,'steady_samples_per_arm':960}
 for kind in ['N','M']:assert len({x['raw_sha256']for x in ss if x['kind']==kind})==1
(p/(case+'-statistics.json')).write_text(json.dumps({'nmmn':tiers,'slots':slots},indent=2));print(json.dumps(tiers,indent=2))
