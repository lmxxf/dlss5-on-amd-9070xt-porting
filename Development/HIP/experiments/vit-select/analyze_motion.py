from pathlib import Path
import sys,json
import numpy as np
root=Path(sys.argv[1]);out=Path(sys.argv[2])
def load(name):return np.fromfile(root/name,'<f2').reshape(720,1296,4).astype(np.float32)[:,:,:3]
def display(x):
 x=np.clip(x,0,1);return np.where(x<=.0031308,x*12.92,1.055*x**(1/2.4)-.055)
roi=(slice(32,-32),slice(32,-32),slice(None))
result={}
for v in ['0.5','1']:
 mae=[];excess=[];base_delta=[];prev_e=prev_a=None
 for i in range(12):
  raw_a=load(f'base-motion-{i}.f16');raw_b=load(f'{v}-motion-{i}.f16')
  assert np.isfinite(raw_a).all() and np.isfinite(raw_b).all()
  a,b=display(raw_a),display(raw_b);e=b-a
  mae.append(float(np.abs(e[roi]).mean()*255))
  if prev_e is not None:
   # Input content moves right by one output pixel. Exclude wrap-around edges.
   excess.append(float(np.abs((e-np.roll(prev_e,1,axis=1))[roi]).mean()*255))
   base_delta.append(float(np.abs((a-np.roll(prev_a,1,axis=1))[roi]).mean()*255))
  prev_e,prev_a=e,a
 r=np.loadtxt(root/f'{v}-route.csv',delimiter=',');frames=sorted(set(r[:,7].astype(int)));assert frames==list(range(1,13)),frames
 masks=[r[r[:,7]==i,4] for i in frames];assert len({len(m) for m in masks})==1
 flips=[float(np.mean(a!=b)) for a,b in zip(masks,masks[1:])]
 result[v]=dict(display_mae_255_mean=float(np.mean(mae)),display_mae_255_max=max(mae),motion_compensated_error_change_255_mean=float(np.mean(excess)),motion_compensated_error_change_255_max=max(excess),baseline_motion_compensated_change_255_mean=float(np.mean(base_delta)),route_flip_fraction_mean=float(np.mean(flips)),route_flip_fraction_max=max(flips),frame_mae_255=mae,frame_error_change_255=excess,frame_route_flip_fraction=flips)
for i in range(12):assert (root/f'base-motion-{i}.f16').read_bytes()==(root/f'-1-motion-{i}.f16').read_bytes()
out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
