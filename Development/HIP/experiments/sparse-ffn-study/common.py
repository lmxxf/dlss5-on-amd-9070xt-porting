import numpy as np
levels=np.array([(b&7)/512 if b<8 else (1+(b&7)/8)*2.**(((b>>3)&15)-7) for b in range(127)],np.float32)
def fp8(x):
 x=np.clip(x,-448,448);v=np.abs(x);hi=np.minimum(np.searchsorted(levels,v),126);lo=np.maximum(hi-1,0);a=v-levels[lo];b=levels[hi]-v
 index=np.where((b<a)|((b==a)&((hi&1)==0)),hi,lo);return np.copysign(levels[index],x)
def q4(x,group):
 shape=x.shape;a=x.reshape(shape[0],-1,group);scale=np.maximum(np.max(np.abs(a),axis=-1,keepdims=True),1.e-10)/np.float32(7)
 return (np.clip(np.rint(a/scale),-7,7)*scale).reshape(shape)
def rotate(x,group=64):
 a=x.reshape(-1,group).copy();h=1
 while h<group:
  v=a.reshape(-1,group//(2*h),2,h);left=v[:,:,0,:].copy();right=v[:,:,1,:].copy();v[:,:,0,:]=left+right;v[:,:,1,:]=left-right;h*=2
 return (a/np.float32(np.sqrt(group))).reshape(x.shape)
def activate(v):
 gate=np.clip(v,-4,4);inner=np.abs(gate)*np.float32(-.055908203125)+np.float32(.447265625)
 poly=(gate.astype(np.float64)*inner.astype(np.float64)+.89453125).astype(np.float32)
 return fp8(v*poly)
def metric(a,b):
 diff=(a-b).astype(np.float64);ref=b.astype(np.float64);den=max(float(np.mean(np.abs(ref))),1.e-20)
 return dict(relative_l1=float(np.mean(np.abs(diff))/den),relative_rmse=float(np.sqrt(np.mean(diff*diff)/max(float(np.mean(ref*ref)),1.e-30))),abs_p99=float(np.quantile(np.abs(diff),.99)),max_abs=float(np.max(np.abs(diff))),cosine=float(np.sum(a.astype(np.float64)*ref)/max(float(np.linalg.norm(a)*np.linalg.norm(b)),1.e-30)))
