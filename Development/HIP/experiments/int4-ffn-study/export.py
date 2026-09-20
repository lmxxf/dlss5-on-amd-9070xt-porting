from pathlib import Path
import sys,json
import numpy as np
from common import fp8,rotate,levels
root,weight,out=map(Path,sys.argv[1:4]);out.mkdir(parents=True,exist_ok=True)
w=fp8(np.fromfile(weight,'<f2').astype(np.float32).reshape(4096,1024))
x=np.fromfile(root/'capture-900-s0/features/1-input.f32','<f4').reshape(400,1024)
s=np.load('/tmp/int4-block31-calibration.npz')['smooth']
x.tofile(out/'input.f32');w.tofile(out/'weight.f32');s.tofile(out/'smooth.f32');(np.float32(1)/s).tofile(out/'smooth-inv.f32');np.ones(1024,np.float32).tofile(out/'ones.f32')
codes=(np.searchsorted(levels,np.abs(w)).astype(np.uint8)|(np.signbit(w).astype(np.uint8)*128))
codes.reshape(256,16,32,4,8).transpose(0,2,3,1,4).copy().tofile(out/'fp8-frag.bin')
for name,ww,group in [('plain',w,1024),('rotrow',rotate(w*s),1024),('rotgroup',rotate(w*s),64)]:
 a=ww.reshape(4096,-1,group);scale=np.maximum(np.abs(a).max(2),1.e-10)/np.float32(7)
 q=np.clip(np.rint(a/scale[:,:,None]),-7,7).astype(np.int8).reshape(4096,1024)
 packed=((q[:,::2]&15)|((q[:,1::2]&15)<<4)).astype(np.uint8)
 packed.reshape(256,16,16,4,8).transpose(0,2,3,1,4).copy().tofile(out/f'{name}-frag.bin')
 scale.T.copy().tofile(out/f'{name}-scale.f32')
print(out)

clip=json.loads(Path(sys.argv[4]).read_text())['selected_clip']
a=w*s;scale=np.maximum(np.abs(a).max(1)*np.float32(clip),1.e-10)/np.float32(7)
q=np.clip(np.rint(a/scale[:,None]),-7,7).astype(np.int8)
packed=((q[:,::2]&15)|((q[:,1::2]&15)<<4)).astype(np.uint8)
packed.reshape(256,16,16,4,8).transpose(0,2,3,1,4).copy().tofile(out/'balanced-frag.bin')
scale.tofile(out/'balanced-scale.f32');np.array([clip],np.float32).tofile(out/'balanced-clip.f32')
