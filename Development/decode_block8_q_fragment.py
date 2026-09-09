"""Inspect standard16x32 MMA A-fragment register order candidates."""
from pathlib import Path
import json
import numpy as np
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18');dump=json.loads((root/'qk-registers-5030.json').read_text());q=np.load(root/'query-reference.npz')['qh'][0]
checks=[]
for swap in (False,True):
 decoded=np.empty((16,32),np.float32)
 for row in dump['rows']:
  lane=row['lane']
  for reg in range(4):
   rr=lane//4+8*((reg>>1) if swap else (reg&1));cc=lane%4*4+16*((reg&1) if swap else (reg>>1))
   value=row['registers'][str(8+reg)];codes=np.array([(value>>(8*i))&255 for i in range(4)],np.uint8)
   decoded[rr,cc:cc+4]=e4m3fn(codes)
 for start in (0,16,32,48):checks.append({'register_bit_swap':swap,'query_start':start,'different':int(np.count_nonzero(decoded!=q[start:start+16]))})
print(json.dumps(checks,indent=2));(root/'q-fragment-candidates.json').write_text(json.dumps(checks,indent=2)+'\n')
