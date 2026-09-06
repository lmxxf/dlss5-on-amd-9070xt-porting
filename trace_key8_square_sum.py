"""Replay the observed HMUL/HFMA/HADD/shuffle norm using captured operands."""
from pathlib import Path
import json
import numpy as np
from native_c32_reference import H
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18');dump=json.loads((root/'qk-registers-3a40.json').read_text())['rows']
def reg(n):
 return np.array([[r['registers'][str(n)]&65535,r['registers'][str(n)]>>16] for r in dump],np.uint16).view(np.float16).astype(np.float32)
a,b,c,d=reg(96),reg(100),reg(102),reg(98)
s=H(H(a*a+H(b*b))+H(c*c+H(d*d)))
exact=H(H(a.astype(np.float64)**2+H(b*b))+H(c.astype(np.float64)**2+H(d*d)))
exact=H(exact+exact[np.arange(32)^2]);exact=H(exact+exact[np.arange(32)^1]);exact=H(exact[:,0]+exact[:,1])
s=H(s+s[np.arange(32)^2]);s=H(s+s[np.arange(32)^1]);total=H(s[:,0]+s[:,1])
reference=np.load(root/'query-reference.npz')['k'][0,8]
components=[]
for lane in range(16,20):
 for name,values in [('R96',a),('R100',b),('R102',c),('R98',d)]:
  for half in range(2):
   value=values[lane,half];components.append({'lane':lane,'reg':name,'half':half,'value':float(value),'matching_channels':np.flatnonzero(reference==value).tolist()})
r={'scope':'captured norm operands replay; channel mapping still to verify','replayed_lane16_sum':float(total[16]),'fused_float64_lane16_sum':float(exact[16]),'original_sum':383.5,'components':components}
(root/'norm-operand-replay.json').write_text(json.dumps(r,indent=2)+'\n');print({'sum':float(total[16]),'unmatched_components':sum(not c['matching_channels'] for c in components)});print(components)
print({'fused_float64_sum':float(exact[16])})
