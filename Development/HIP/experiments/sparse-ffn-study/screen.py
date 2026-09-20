from pathlib import Path
import sys,json
import numpy as np
from common import fp8,activate,metric,levels
root,weight,out=map(Path,sys.argv[1:4]);out.mkdir(parents=True,exist_ok=True)
w=fp8(np.fromfile(weight,'<f2').astype(np.float32).reshape(4096,1024))
g=w.reshape(4096,256,4);ix=np.sort(np.argsort(-np.abs(g),axis=-1,kind='stable')[...,:2],axis=-1)
mask=np.zeros_like(g);np.put_along_axis(mask,ix,1,axis=-1);pruned=(g*mask).reshape(w.shape)
def encode(x):
 a=np.searchsorted(levels,np.abs(x)).astype(np.uint8);return a|((x<0).astype(np.uint8)*128)
encode(pruned).tofile(out/'pruned.fp8');encode(w).tofile(out/'dense.fp8')
compressed=encode(np.take_along_axis(g,ix,axis=-1)).reshape(4096,32,2,8)
compressed.reshape(256,16,32,2,8).transpose(0,2,3,1,4).copy().tofile(out/'fragments.fp8')
compressed.tofile(out/'compressed.fp8')
idx=(ix[...,0]|(ix[...,1]<<2)).astype(np.uint32).reshape(4096,64,4)
idx=np.sum(idx << (np.arange(4,dtype=np.uint32)*4),axis=-1,dtype=np.uint32);idx.tofile(out/'indices.u32')
idx.reshape(256,16,32,2).transpose(0,2,3,1).copy().tofile(out/'fragment-indices.u32')
t=np.arange(400);r=(t&~15)|((t&1)<<3)|((t&14)>>1);valid=np.flatnonzero(r<375);pick=valid[np.linspace(0,len(valid)-1,64,dtype=int)]
cases=[(0,0),(1,1),(1,5),(1,9),(2,1),(2,5),(2,9),(3,6),(3,9),(4,6),(4,9)];results=[]
for s,f in cases:
 x=fp8(np.fromfile(root/f'capture-900-s{s}/features/{f+1}-input.f32','<f4').reshape(400,1024))
 if s==0 and f==0:
  encode(x).tofile(out/'input.fp8');(x@pruned.T).tofile(out/'oracle.f32')
 x=x[pick];a=x@w.T;b=x@pruned.T;results.append(dict(sequence=s,frame=f,pre=metric(b,a),post=metric(activate(b),activate(a))))
summary=dict(shape=list(w.shape),zero_fraction=float(np.mean(pruned==0)),mean_post_relative_l1=float(np.mean([a['post']['relative_l1'] for a in results])),cases=results)
(out/'quality.json').write_text(json.dumps(summary,indent=2)+'\n');print('mean post L1',summary['mean_post_relative_l1'],flush=True)
