"""Fast-path error report: compare a run's final RGB against the frozen exact-chain oracle.
No pass/fail threshold; the picture is judged by the user. Also copies the GPU timing report."""
from pathlib import Path
import json,re,argparse
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--frames',type=int,default=15);args=p.parse_args()
root=args.root;log=(root/'network.stdout.log').read_text()
rows=re.findall(r'network70 frame=(\d+) history=(\d+) values=(\d+) different=(\d+)',log)
report={'scope':'fast path vs exact chain; hardware arithmetic differences only','frames':[]}
refs={'gpu-network70.f32':'release/native-rgb-valid1080/post70/shift-full-oracle.f32','gpu-network70-temporal.f32':'release/native-temporal-valid1080/post70/shift-full-oracle.f32'}
for name,ref in refs.items():
 a=np.fromfile(root/name,np.float32);b=np.fromfile(ref,np.float32);assert a.shape==b.shape==(6635520,)
 d=a-b;fin=bool(np.isfinite(a).all());absd=np.abs(d)
 mse=float(np.mean(d*d));psnr=float(10*np.log10(1.0/mse)) if mse>0 else float('inf')
 report[name]=dict(finite=fin,max_abs=float(absd.max()),rmse=float(np.sqrt(mse)),psnr_db=psnr,
  frac_ge_1_255=float(np.mean(absd>=1/255)),frac_ge_4_255=float(np.mean(absd>=4/255)),identical_values=int(np.sum(d==0)),values=int(a.size))
stages=[];profiles=[]
for line in log.splitlines():
 m=re.fullmatch(r'network_gpu_interval stage=(\S+) ms=([\d.]+)',line)
 if m:stages.append((m[1],float(m[2])))
 m=re.fullmatch(r'network_gpu_interval total_ms=([\d.]+) includes_inter_submission_gaps=1',line)
 if m:profiles.append(dict(total_ms=float(m[1]),stages=stages));stages=[]
if profiles:
 report['warm_total_ms']=float(np.mean([f['total_ms'] for f in profiles[1:]]));names=[k for k,_ in profiles[0]['stages']]
 means=np.mean([[v for _,v in f['stages']] for f in profiles[1:]],axis=0);report['warm_ranked']=sorted(zip(names,map(float,means)),key=lambda x:-x[1])
report['frame_rows']=[tuple(map(int,r)) for r in rows]
(root/'fast-validation.json').write_text(json.dumps(report,indent=2)+'\n')
for name in refs:print(name,{k:(round(v,6) if isinstance(v,float) else v) for k,v in report[name].items()})
print('warm_total_ms',round(report.get('warm_total_ms',0),3))
