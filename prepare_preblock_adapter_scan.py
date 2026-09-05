"""Input-adapter activity scan, preserving the actual downstream kernel weights."""
from pathlib import Path
import numpy as np
p=Path('release/preblock-adapter-scan');p.mkdir(parents=True,exist_ok=True)
w=np.fromfile('/tmp/block0.weights','<f2').copy();assert len(w)==10848
w[:4096]=0
w[4616:4648]=1
w[4656:6192]=0
w[10296:10808]=0
w[10808:10840]=1
w.view('<f4')[10288//2]=1
w.tofile(p/'probe.weights')
x=np.ones((8,8,4),'<f4');x[:,:,:3]=[0.125,0.25,0.5]
x.tofile(p/'input.rgba32f')
