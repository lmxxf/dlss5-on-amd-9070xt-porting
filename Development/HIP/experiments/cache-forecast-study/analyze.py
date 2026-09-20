"""CPU-only causal predictor screening on captured ViT x/y; not RGB or GPU timing."""
from pathlib import Path
import csv,json,sys
import numpy as np
root,gainpath,out=map(Path,sys.argv[1:4]);out.mkdir(parents=True,exist_ok=True)
g=np.fromfile(gainpath,'<f4');assert g.shape==(1024,)
levels=np.array([(b&7)/512 if b<8 else (1+(b&7)/8)*2.**(((b>>3)&15)-7) for b in range(127)],np.float32)
def quant(x):
 x=np.clip(x.astype(np.float16).astype(np.float32),-448,448);v=np.abs(x);hi=np.minimum(np.searchsorted(levels,v),126);lo=np.maximum(hi-1,0)
 a=v-levels[lo];b=levels[hi]-v;idx=np.where((b<a)|((b==a)&((hi&1)==0)),hi,lo);return np.copysign(levels[idx],x)
def predict(x,xa,ya,correction):
 # Keep the R3 equal-component bypass; all-equal input therefore stays exact.
 return np.where(x==xa,ya,quant(ya+g*(x-xa)+correction))
rows=[];fits=[]
for seq in range(5):
 d=root/f'capture-900-s{seq}/features'
 xs=[np.fromfile(d/f'{i+1}-input.f32','<f4').reshape(400,1024) for i in range(12)]
 ys=[np.fromfile(d/f'{i+1}-output.f32','<f4').reshape(400,1024) for i in range(12)]
 assert all(np.isfinite(v).all() for v in xs+ys)
 t=np.arange(400);r=(t&~15)|((t&1)<<3)|((t&14)>>1);valid=r<375
 history=[];beta=0.0
 for i in range(12):
  if i%4==0:
   if len(history)>=2:
    b,c=history[-1],history[-2];direction=((ys[b]-g*xs[b])-(ys[c]-g*xs[c]))*((i-b)/(b-c))
    base=predict(xs[i],xs[b],ys[b],0);direction=np.where(xs[i]==xs[b],0,direction)
    e=(ys[i]-base)[valid].astype(np.float64);v=direction[valid].astype(np.float64)
    denom=np.sum(v*v)+.1*np.sum(ys[i][valid].astype(np.float64)**2)
    beta=float(np.clip(np.sum(e*v)/max(denom,1e-20),-.4,.4))
    fits.append(dict(sequence=seq,update_frame=i,anchors=[c,b],beta=beta))
   history.append(i);continue
  a=history[-1];direction=0
  if len(history)>=2:
   b=history[-2];direction=((ys[a]-g*xs[a])-(ys[b]-g*xs[b]))*((i-a)/(a-b))
  corrections={'r3_gain_hold':0,'linear_extrapolate':direction,'damped_extrapolate':.25*direction,'causal_bounded_fit':beta*direction}
  corrections['two_anchor_mean']=0 if len(history)<2 else -.5*((ys[a]-g*xs[a])-(ys[history[-2]]-g*xs[history[-2]]))
  for name,correction in corrections.items():
   predicted=predict(xs[i],xs[a],ys[a],correction);e=np.abs(predicted[valid]-ys[i][valid]);den=max(float(np.abs(ys[i][valid]).mean()),1e-12)
   rows.append(dict(sequence=seq,frame=i,anchor=a,method=name,relative_l1=float(e.mean()/den),absolute_l1=float(e.mean()),token_relative_p95=float(np.quantile(e.mean(1)/np.maximum(np.abs(ys[i][valid]).mean(1),1e-12),.95))))
summary=[]
for seq in range(5):
 for name in ['r3_gain_hold','linear_extrapolate','damped_extrapolate','causal_bounded_fit','two_anchor_mean']:
  subset=[r for r in rows if r['sequence']==seq and r['method']==name and r['frame']>=5]
  summary.append(dict(sequence=seq,method=name,frames=len(subset),mean_relative_l1=float(np.mean([r['relative_l1'] for r in subset])),max_relative_l1=max(r['relative_l1'] for r in subset)))
with (out/'frames.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
(out/'summary.json').write_text(json.dumps(dict(summary=summary,causal_fits=fits,scope='900 tier, no history, fixed exact anchors0/4/8; six reused frames5/6/7/9/10/11; feature error only'),indent=2)+'\n')
for seq in range(5):
 print(seq,{r['method']:round(r['mean_relative_l1'],5) for r in summary if r['sequence']==seq})
print('causal fit',fits)
