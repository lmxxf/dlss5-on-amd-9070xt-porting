"""Check both recovered original byte permutations compose to identity."""
from pathlib import Path
import json,hashlib
import numpy as np
root=Path('release/native-vit/repack640');n=640*1024
forward=np.fromfile(root/'forward.i32',np.uint32);inverse=np.fromfile(root/'inverse.i32',np.uint32)
for mapping in (forward,inverse):
 assert mapping.size==n and mapping.max()<n and np.unique(mapping).size==n
identity=np.arange(n,dtype=np.uint32)
report={'entries':n,'forward_then_inverse_different':int(np.count_nonzero(forward[inverse]!=identity)),
 'inverse_then_forward_different':int(np.count_nonzero(inverse[forward]!=identity)),
 'scope':'original 32x20 byte permutations; does not prove pool/head padding or decoder crop',
 'sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in ('forward.i32','inverse.i32')}}
print(json.dumps(report,indent=2));(root/'roundtrip.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['forward_then_inverse_different']==report['inverse_then_forward_different']==0
