"""Inspect complete standard MMA B fragments against reference K blocks."""
from pathlib import Path
import json
import numpy as np
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18');dump=json.loads((root/'qk-registers-5030.json').read_text());k=np.load(root/'query-reference.npz')['kh'][0]
checks=[];histograms=[]
for base in range(0,159,2):
 decoded=np.empty((8,32),np.float32)
 for row in dump['rows']:
  lane=row['lane']
  for reg in (0,1):
   word=row['registers'][str(base+reg)];codes=np.array([(word>>(8*b))&255 for b in range(4)],np.uint8)
   decoded[lane//4,(lane%4)*4+reg*16:(lane%4)*4+reg*16+4]=e4m3fn(codes)
 for start in range(0,64,8):
  diff=int(np.count_nonzero(decoded!=k[start:start+8]))
  sorted_diff=int(np.count_nonzero(np.sort(decoded.ravel())!=np.sort(k[start:start+8].ravel())))
  histograms.append({'register_base':base,'key_start':start,'sorted_different':sorted_diff})
  if diff<30:
   checks.append({'register_base':base,'key_start':start,'different':diff,'mismatches':[{'key_channel':[int(i[0])+start,int(i[1])],'original_fragment':float(decoded[tuple(i)]),'reference':float(k[start+i[0],i[1]])} for i in np.argwhere(decoded!=k[start:start+8])]})
(root/'k-fragment-candidates.json').write_text(json.dumps(checks,indent=2)+'\n');print(json.dumps(checks,indent=2))
best=sorted(histograms,key=lambda x:x['sorted_different'])[:12]
(root/'k-fragment-histograms.json').write_text(json.dumps({'scope':'histogram diagnostics only, no coordinate acceptance','best':best},indent=2)+'\n');print(best)
