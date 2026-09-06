from pathlib import Path
import json
import numpy as np
from native_c32_reference import H
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18');d=np.load(root/'query-reference.npz');v=d['k'][0,8]
s=H(v[:16]*v[:16]+H(v[16:]*v[16:]));s=H(s[::2]+s[1::2])
for w in (4,2,1):s=H(s[:w]+s[w:2*w])
inv=H(1/np.sqrt(s))[0];operands=json.loads((root/'key8-normalize-operands.json').read_text());original=operands['138'][1]
r={'reference_sum':float(s[0]),'reference_inverse':float(inv),'original_inverse':original,'same_inverse':bool(inv==original),'half_product_original_inverse':float(H(v[3]*original)),'half_product_reference_inverse':float(H(v[3]*inv))}
regs=json.loads((root/'qk-registers-4030.json').read_text())['rows'][16]['registers']
word=regs['142'];r['original_R142_sum_halves']=np.array([word&65535,word>>16],np.uint16).view(np.float16).astype(np.float32).tolist()
(root/'key8-norm-sum.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
