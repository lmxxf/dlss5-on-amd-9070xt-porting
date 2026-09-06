"""Compare original ordinary and DS main outputs using identical weights/input."""
from pathlib import Path
import json
import numpy as np
root=Path('release/native-rgb-valid1080/encoder-c64');n=288*480*64
a=np.fromfile(root/'block8-main.fp8',np.uint8);b=np.fromfile(root/'block8-ordinary-main.fp8',np.uint8)
assert a.size>=n and b.size>=n and not a[n:].any() and not b[n:].any()
ids=np.flatnonzero(a[:n]!=b[:n]);r={'scope':'original DS versus ordinary main; same input/weights','different':int(ids.size),'first_offsets':ids[:32].tolist()}
(root/'variant-comparison.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
