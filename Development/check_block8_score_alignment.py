"""Diagnostic aligned-product QK+bias hypothesis; never fitted output correction."""
from pathlib import Path
import json
import numpy as np
from native_c64_reference import unpack
from native_c32_reference import H,F
from native_post70_reference import aligned
from native_c32_softmax_sum import denominator
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18');d=np.load(root/'query-reference.npz')
bias=unpack(root.parent/'block8.weights')[3][1]
score=aligned(d['qh'][0],d['kh'][0],bias)
affine=np.clip(H(score*.044921875+1.30078125),1.03125,1.5693359375)
b=affine.astype(np.float16).view(np.uint16).astype(np.uint32)
exp=(((b<<5)+0x8000)&65535).astype(np.uint16).view(np.float16).astype(np.float32)
prob=F(H(exp*H(1/denominator(exp))))[None]
v=d['vh'];result=F(H(H(prob[:,:,:32]@v[:,:32])+prob[:,:,32:]@v[:,32:]))
changes=np.argwhere(score!=d['scores'][0])
r={'scope':'aligned QK score candidate, not validated kernel implementation','score_changes':len(changes),'attention_different':int(np.count_nonzero(result!=d['original'])),'changed_scores':[{'qk':i.tolist(),'baseline':float(d['scores'][0][tuple(i)]),'aligned':float(score[tuple(i)])} for i in changes[:32]]}
(root/'score-alignment.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
