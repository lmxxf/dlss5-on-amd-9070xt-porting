"""Infer diagnostic bit permutation on a clean K fragment, cross-check others."""
from pathlib import Path
import json,itertools
import numpy as np
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18');dump=json.loads((root/'qk-registers-5030.json').read_text());k=np.load(root/'query-reference.npz')['kh'][0]
groups=json.loads((root/'key-bit-group-candidates.json').read_text())['best']
def values(base):
 return e4m3fn(np.array([(row['registers'][str(base+r)]>>(8*b))&255 for row in dump['rows'] for r in (0,1) for b in range(4)],np.uint8))
physical=values(4);keys=groups[0]['keys'];reference=k[keys].ravel();indices=np.arange(256);bits=[((indices>>b)&1) for b in range(8)];matches=[]
for order in itertools.permutations(range(8)):
 address=sum(bits[b]<<dst for dst,b in enumerate(order))
 if np.array_equal(physical,reference[address]):matches.append((order,address))
assert len(matches)==1, len(matches)
order,address=matches[0];checks=[]
for group in groups:
 p=values(group['register_base']);ref=k[group['keys']].ravel()[address];idx=np.flatnonzero(p!=ref)
 checks.append({'register_base':group['register_base'],'different':len(idx),'mismatches':[{'key':group['keys'][int(address[i])//32],'channel':int(address[i])%32,'original':float(p[i]),'reference':float(ref[i])} for i in idx]})
r={'scope':'data-inferred diagnostic mapping, corroborated across clean fragments; needs instruction provenance','physical_bits_for_logical':list(order),'checks':checks}
(root/'k-bitlayout.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
