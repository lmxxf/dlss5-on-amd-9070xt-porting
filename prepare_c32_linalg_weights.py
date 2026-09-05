import argparse
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser()
p.add_argument('source',type=Path)
p.add_argument('output',type=Path)
a=p.parse_args()
w=np.fromfile(a.source,dtype='<f4')
assert w.size==10305
packed=w[:4096].astype('<f2')
assert np.isfinite(packed).all()
a.output.write_bytes(packed.tobytes())
print('bytes',packed.nbytes,'changed_weights',np.count_nonzero(w[:4096]!=packed.astype(np.float32)))
