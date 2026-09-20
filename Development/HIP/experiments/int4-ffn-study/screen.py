"""CPU screening of the real block31 expansion; not a GPU oracle or speed benchmark."""
from pathlib import Path
import sys,json,hashlib,time
import numpy as np
root,weight_path,out=map(Path,sys.argv[1:4]);out.mkdir(parents=True,exist_ok=True)
from common import fp8,q4,rotate,activate,metric
w=np.fromfile(weight_path,'<f2').astype(np.float32);assert w.size==4096*1024;w=fp8(w.reshape(4096,1024))
t=np.arange(400);r=(t&~15)|((t&1)<<3)|((t&14)>>1);valid=np.flatnonzero(r<375);pick=valid[np.linspace(0,len(valid)-1,64,dtype=int)]
def read(seq,frame):
 a=np.fromfile(root/f'capture-900-s{seq}/features/{frame+1}-input.f32','<f4').reshape(400,1024);assert np.isfinite(a).all();return fp8(a)
cal=read(0,0)[valid];cases=[(0,0),(1,1),(1,5),(1,9),(2,1),(2,5),(2,9),(3,6),(3,9),(4,6),(4,9)]
x=np.concatenate([read(s,f)[pick] for s,f in cases]);reference=x@w.T;ref_act=activate(reference);results=[]
def evaluate(name,y,**metadata):
 assert np.isfinite(y).all(),name
 act=activate(y);per_case=[]
 for i,(s,f) in enumerate(cases):
  sl=slice(i*64,(i+1)*64);per_case.append(dict(sequence=s,frame=f,**metric(act[sl],ref_act[sl])))
 item=dict(name=name,pre_activation=metric(y,reference),post_activation=metric(act,ref_act),cases=per_case,**metadata);results.append(item)
 (out/'results.json').write_text(json.dumps(dict(weight_sha256=hashlib.sha256(weight_path.read_bytes()).hexdigest(),shape=list(w.shape),tokens_per_case=64,calibration='375 valid tokens, frozen sequence0 frame0 only',results=results),indent=2)+'\n')
 print(name,'pre RMSE',round(item['pre_activation']['relative_rmse'],5),'post L1',round(item['post_activation']['relative_l1'],5),'post RMSE',round(item['post_activation']['relative_rmse'],5),flush=True)
evaluate('int4_row',q4(x,1024)@q4(w,1024).T)
evaluate('int4_group64',q4(x,64)@q4(w,64).T)
xr,wr=rotate(x),rotate(w)
rot_control=xr@wr.T;err=metric(rot_control,reference)['relative_rmse'];assert err<1.e-5,err
evaluate('rotate64_int4_row',q4(xr,1024)@q4(wr,1024).T,rotation_control_rmse=err)
evaluate('rotate64_int4_group64',q4(xr,64)@q4(wr,64).T)
smooth=np.clip(np.sqrt(np.maximum(np.abs(cal).max(0),1.e-6)/np.maximum(np.abs(w).max(0),1.e-6)),1/32,32).astype(np.float32)
xs=x/smooth;ws=w*smooth
evaluate('smooth_int4_group64',q4(xs,64)@q4(ws,64).T)
xsr,wsr=rotate(xs),rotate(ws)
evaluate('smooth_rotate64_int4_row',q4(xsr,1024)@q4(wsr,1024).T)
evaluate('smooth_rotate64_int4_group64',q4(xsr,64)@q4(wsr,64).T)
# Randomized rank32 spectral approximation; rank8/16 cuts share that basis. Not DeepCompressor calibration.
rng=np.random.default_rng(2718);omega=rng.standard_normal((1024,48)).astype(np.float32);q=np.linalg.qr(ws@omega,mode='reduced')[0]
for _ in range(3):q=np.linalg.qr(ws@(ws.T@q),mode='reduced')[0]
u,s,vt=np.linalg.svd(q.T@ws,full_matrices=False);u=q@u
for rank in [8,16,32]:
 up=u[:,:rank]*s[:rank];down=vt[:rank];residual=ws-up@down
 y=q4(xs,64)@q4(residual,64).T+(xs@down.T)@up.T
 evaluate(f'smooth_lowrank{rank}_int4_group64',y,rank=rank,lowrank_weight_energy=float(np.sum(s[:rank].astype(np.float64)**2)/np.sum(ws.astype(np.float64)**2)))
 y_rot=q4(xsr,64)@q4(rotate(residual),64).T+(xs@down.T)@up.T
 evaluate(f'smooth_rotate64_lowrank{rank}_int4_group64',y_rot,rank=rank)
np.savez(Path('/tmp/int4-block31-calibration.npz'),smooth=smooth,up=u[:,:32]*s[:32],down=vt[:32])
