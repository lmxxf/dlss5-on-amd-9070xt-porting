"""Input-adapter activity scan, preserving the actual downstream kernel weights."""
from pathlib import Path
import numpy as np
p=Path('release/preblock-adapter-scan');p.mkdir(parents=True,exist_ok=True)
w=np.fromfile('/tmp/block0.weights','<f2').copy();assert len(w)==10848
w.tofile(p/'probe.weights')
x=np.ones((8,8,4),'<f4');x[:,:,:3]=[0.125,0.25,0.5]
x.tofile(p/'input.rgba32f')
