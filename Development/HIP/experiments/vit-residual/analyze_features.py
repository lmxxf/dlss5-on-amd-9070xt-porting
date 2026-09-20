from pathlib import Path
import sys,csv,json
import numpy as np
root=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
# Native E4M3FN lattice, finite positive codes 0..126; nearest with even-code ties.
levels=np.array([(b&7)/512 if b<8 else (1+(b&7)/8)*2.**(((b>>3)&15)-7) for b in range(127)],np.float32)
def fp8(x):
 x=np.clip(x,-448,448);a=np.abs(x);hi=np.minimum(np.searchsorted(levels,a),126);lo=np.maximum(hi-1,0)
 dl=a-levels[lo];dh=levels[hi]-a;pick=np.where((dh<dl)|((dh==dl)&((hi&1)==0)),hi,lo)
 return np.copysign(levels[pick],x)
gain=np.fromfile(sys.argv[3],'<f4') if len(sys.argv)>3 else None
def predict(x,xa,ya):
 raw=x+(ya-xa) if gain is None else ya+gain*(x-xa)
 z=fp8(raw.astype(np.float16).astype(np.float32));return np.where(x==xa,ya,z)
def read(p):
 a=np.fromfile(p,'<f4').reshape(-1,1024);assert np.isfinite(a).all();return a
rows=[]
for sequence in range(5):
 d=root/f'capture-900-s{sequence}'/'features';xs=[read(d/f'{i+1}-input.f32') for i in range(12)];ys=[read(d/f'{i+1}-output.f32') for i in range(12)]
 n=len(xs[0]);assert n==400;t=np.arange(n);r=(t&~15)|((t&1)<<3)|((t&14)>>1);valid=r<375
 for policy in ['previous','period4','first']:
  for i in range(1,12):
   anchor=i-1 if policy=='previous' else i//4*4 if policy=='period4' else 0
   x,xa,y,ya=xs[i][valid],xs[anchor][valid],ys[i][valid],ys[anchor][valid];den=max(float(np.abs(y).mean()),1e-12);deltax=np.abs(x-xa)
   token=deltax.mean(1)/np.maximum(np.abs(xa).mean(1),1e-12)
   residual=y-x;old=ya-xa;pred=predict(x,xa,ya)
   rows.append(dict(sequence=sequence,policy=policy,frame=i,anchor=anchor,input_rel_l1=float(deltax.mean()/max(float(np.abs(xa).mean()),1e-12)),input_token_rel_p95=float(np.quantile(token,.95)),input_token_rel_max=float(token.max()),residual_rel_l1=float(np.abs(residual-old).mean()/max(float(np.abs(residual).mean()),1e-12)),pred_output_rel_l1=float(np.abs(pred-y).mean()/den),pred_token_rel_max=float((np.abs(pred-y).mean(1)/np.maximum(np.abs(y).mean(1),1e-12)).max()),stale_output_rel_l1=float(np.abs(ya-y).mean()/den)))
with (out/'features.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
summary=[]
for seq in range(5):
 a=[x for x in rows if x['sequence']==seq and x['policy']=='period4' and x['frame']!=x['anchor']]
 v=np.array([x['input_rel_l1'] for x in a]);e=np.array([x['pred_output_rel_l1'] for x in a])
 corr=float(np.corrcoef(v,e)[0,1]) if np.std(v)>1e-12 and np.std(e)>1e-12 else None
 summary.append(dict(sequence=seq,max_input_rel_l1=float(v.max()),max_pred_output_rel_l1=float(e.max()),mean_pred_output_rel_l1=float(e.mean()),mean_stale_output_rel_l1=float(np.mean([x['stale_output_rel_l1'] for x in a])),within_sequence_correlation=corr))
(out/'features-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
