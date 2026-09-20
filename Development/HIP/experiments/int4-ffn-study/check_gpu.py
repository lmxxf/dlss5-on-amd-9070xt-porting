from pathlib import Path
import sys,json,csv
import numpy as np
from common import fp8,rotate,q4,activate,metric,levels
root,data,out=map(Path,sys.argv[1:4]);out.mkdir(parents=True,exist_ok=True)
x=np.fromfile(data/'input.f32','<f4').reshape(400,1024);w=np.fromfile(data/'weight.f32','<f4').reshape(4096,1024);s=np.fromfile(data/'smooth.f32','<f4')
def decode8(p):
 a=np.fromfile(p,np.uint8);assert ((a&127)<127).all();return np.where(a&128,-1,1)*levels[a&127]
def unpack(p,m):
 a=np.fromfile(p,np.uint8).reshape(m,512);q=np.empty((m,1024),np.int8);q[:,::2]=a&15;q[:,1::2]=a>>4;return np.where(q>=8,q-16,q).astype(np.float32)
gold=decode8(root/'method0.fp8').reshape(400,4096).astype(np.float32);control=metric(gold,activate(x@w.T));assert control['relative_rmse']<.002,control
rows=[]
for method,name in [(1,'plain'),(2,'rotrow'),(3,'rotgroup'),(4,'balanced')]:
 a=unpack(root/f'method{method}-input.i4',400);sc=np.fromfile(root/f'method{method}-scale.f32','<f4');ng=16 if method==3 else 1
 a=(a.reshape(400,ng,-1)*sc.reshape(ng,400).T[:,:,None]).reshape(400,1024)
 packed=np.fromfile(data/f'{name}-frag.bin',np.uint8).reshape(256,16,4,16,8).transpose(0,3,1,2,4).copy().reshape(4096,512)
 q=np.empty((4096,1024),np.int8);q[:,::2]=packed&15;q[:,1::2]=packed>>4;q=np.where(q>=8,q-16,q).astype(np.float32);ws=np.fromfile(data/f'{name}-scale.f32','<f4')
 b=(q.reshape(4096,ng,-1)*ws.reshape(ng,4096).T[:,:,None]).reshape(4096,1024)
 actual=np.fromfile(root/f'method{method}-pre.f32','<f4').reshape(400,4096);assert np.isfinite(actual).all();oracle=metric(actual,a@b.T);assert oracle['relative_rmse']<1e-4,(method,oracle)
 act=decode8(root/f'method{method}.fp8').reshape(400,4096).astype(np.float32)
 target=x if method==1 else rotate(x/s) if method in (2,3) else x/s
 if method==4:
  clip=float(np.fromfile(data/'balanced-clip.f32','<f4')[0]);scale=np.maximum(np.abs(target).max(1,keepdims=True)*clip,1e-10)/7;target_q=np.clip(np.rint(target/scale),-7,7)*scale
 else:target_q=q4(target,64 if method==3 else 1024)
 quant_error=metric(a,target_q);assert quant_error['relative_rmse']<.02,(method,'quantizer mismatch',quant_error)
 rows.append(dict(method=method,name=name,matmul_oracle=oracle,quantizer_cpu_comparison=quant_error,post_activation=metric(act,gold)))
 print(name,'GPU oracle RMSE',oracle['relative_rmse'],'quantizer RMSE',quant_error['relative_rmse'],'post rel L1',rows[-1]['post_activation']['relative_l1'])
(out/'gpu-quality.json').write_text(json.dumps(dict(fp8_cpu_control=control,variants=rows),indent=2)+'\n')
t=list(csv.DictReader((root/'timing.csv').open()));timing=[]
for c in range(1,5):
 r=[v for v in t if int(v['candidate'])==c];a=(float(r[0]['mean_ms'])+float(r[3]['mean_ms']))/2;b=(float(r[1]['mean_ms'])+float(r[2]['mean_ms']))/2
 timing.append(dict(method=c,baseline_ms=a,candidate_ms=b,change_percent=(b-a)/a*100,runs=r));print('TIME',c,a,b,(b-a)/a*100)
(out/'gpu-timing.json').write_text(json.dumps(timing,indent=2)+'\n')
