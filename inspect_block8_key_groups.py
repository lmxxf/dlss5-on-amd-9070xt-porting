"""Diagnostic key-bit group search, not a fitted runtime tensor mapping."""
from pathlib import Path
import itertools,json
import numpy as np
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18');dump=json.loads((root/'qk-registers-5030.json').read_text());k=np.load(root/'query-reference.npz')['kh'][0]
groups=[]
for variable in itertools.combinations(range(6),3):
 fixed=[b for b in range(6) if b not in variable]
 for value in range(8):
  keys=[key for key in range(64) if all(((key>>bit)&1)==((value>>j)&1) for j,bit in enumerate(fixed))]
  groups.append(keys)
reference=np.stack([np.sort(k[g].ravel()) for g in groups]);best=[]
for base in (4,12,80,84,124,126,128,132):
 codes=[]
 for row in dump['rows']:
  for reg in (base,base+1):
   word=row['registers'][str(reg)];codes.extend((word>>(8*b))&255 for b in range(4))
 values=np.sort(e4m3fn(np.array(codes,np.uint8)));diff=np.count_nonzero(reference!=values,axis=1);idx=int(diff.argmin())
 best.append({'register_base':base,'keys':groups[idx],'sorted_different':int(diff[idx])})
(root/'key-bit-group-candidates.json').write_text(json.dumps({'scope':'histogram candidates only, no coordinate acceptance','best':best},indent=2)+'\n');print(json.dumps(best,indent=2))
