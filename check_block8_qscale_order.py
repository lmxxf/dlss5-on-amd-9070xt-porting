"""Diagnostic Q normalization scaling orders; no production correction."""
from pathlib import Path
import json
import numpy as np
from native_c32_reference import H,F
from native_c64_reference import unpack
from native_c32_softmax_sum import denominator
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18');d=np.load(root/'query-reference.npz')
q=d['q'];s=H(q[...,:16]*q[...,:16]+H(q[...,16:]*q[...,16:]));s=H(s[...,::2]+s[...,1::2])
for w in (4,2,1):s=H(s[...,:w]+s[...,w:2*w])
inv=H(1/np.sqrt(np.maximum(s,6.198883056640625e-5)));params=unpack(root.parent/'block8.weights');scale=H(params[4][1]);bias=params[3][1]
candidates={'normalize_then_scale':F(H(H(q*inv)*scale)),'scale_inverse_first':F(H(q*H(inv*scale))),'single_round':F(H(q*inv*scale))}
checks=[]
for name,qh in candidates.items():
 scores=H(qh@d['kh'].transpose(0,2,1)+bias);a=np.clip(H(scores*.044921875+1.30078125),1.03125,1.5693359375)
 bits=a.astype(np.float16).view(np.uint16).astype(np.uint32);e=(((bits<<5)+0x8000)&65535).astype(np.uint16).view(np.float16).astype(np.float32)
 p=F(H(e*H(1/denominator(e))));v=d['vh'];out=F(H(H(p[:,:,:32]@v[:,:32])+p[:,:,32:]@v[:,32:]))
 checks.append({'candidate':name,'q_changes':int(np.count_nonzero(qh!=d['qh'])),'attention_different':int(np.count_nonzero(out!=d['original']))})
(root/'qscale-order.json').write_text(json.dumps(checks,indent=2)+'\n');print(json.dumps(checks,indent=2))
