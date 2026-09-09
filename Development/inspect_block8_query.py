"""Record the isolated query's reference arithmetic without changing the model."""
from pathlib import Path
import json
import numpy as np
from native_c64_reference import unpack,multiply
from native_c32_reference import H,F
from native_c32_normalize import normalize
from native_c32_softmax_sum import denominator
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18')
x=np.fromfile(root/'input.f32',np.float32).reshape(1,64,64)
ffn,qkv,projection,bias,scales,skip=unpack(root.parent/'block8.weights')
expanded=multiply(F(x),ffn['W1']);g=np.clip(expanded,-4,4)
hidden=F(H(expanded*H(g*H(abs(g)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))))
middle=F(multiply(hidden,ffn['W2']));feature=F(multiply(middle,ffn['W3'],H(x*ffn['skip'])))
q,k,v=[multiply(feature,m)[...,32:] for m in qkv]
qh=F(H(normalize(q)*H(scales[1])));kh=F(normalize(k));vh=F(v)
scores=H(qh@kh.transpose(0,2,1)+bias[1]);affine=np.clip(H(scores*.044921875+1.30078125),1.03125,1.5693359375)
b=affine.astype(np.float16).view(np.uint16).astype(np.uint32);exp=(((b<<5)+0x8000)&65535).astype(np.uint16).view(np.float16).astype(np.float32)
den=denominator(exp);inverse=H(1/den);prob=F(H(exp*inverse));av=F(H(H(prob[:,:,:32]@vh[:,:32])+prob[:,:,32:]@vh[:,32:]))
original=np.fromfile(root/'attention-original.f32',np.float32).reshape(1,64,64)[...,32:]
np.savez(root/'query-reference.npz',feature=feature,q=q,k=k,v=v,qh=qh,kh=kh,vh=vh,scores=scores,exp=exp,den=den,prob=prob,av=av,original=original)
r={'query':18,'head':1,'denominator':float(den[0,18,0]),'inverse_half':float(inverse[0,18,0]),'inverse_exact':float(1/float(den[0,18,0])),'score_range':[float(scores[0,18].min()),float(scores[0,18].max())],'prob_sum':float(prob[0,18].sum()),'av_different':int(np.count_nonzero(av!=original))}
print(json.dumps(r,indent=2));(root/'query-reference.json').write_text(json.dumps(r,indent=2)+'\n')
# Sensitivity only: matching a neighboring reciprocal is not a valid correction.
bits=np.array([inverse[0,18,0]],np.float16).view(np.uint16)[0];candidates=[]
for delta in (-2,-1,0,1,2):
 inv=np.array([int(bits)+delta],np.uint16).view(np.float16).astype(np.float32)[0]
 p=F(H(exp[:,18:19]*inv));a=F(H(H(p[:,:,:32]@vh[:,:32])+p[:,:,32:]@vh[:,32:]))
 candidates.append({'half_ulp_delta':delta,'inverse':float(inv),'different':int(np.count_nonzero(a!=original[:,18:19]))})
(root/'reciprocal-sensitivity.json').write_text(json.dumps({'scope':'sensitivity diagnostic, never a fitted runtime correction','candidates':candidates},indent=2)+'\n');print(candidates)
