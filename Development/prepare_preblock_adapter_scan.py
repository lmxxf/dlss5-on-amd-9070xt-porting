"""Input-adapter activity scan, preserving the actual downstream kernel weights."""
from pathlib import Path
import argparse
import numpy as np
parser=argparse.ArgumentParser();parser.add_argument('--rgb',nargs=3,type=float,default=[0.125,0.25,0.5]);parser.add_argument('--gradient',action='store_true');args=parser.parse_args()
p=Path('release/preblock-adapter-scan');p.mkdir(parents=True,exist_ok=True)
w=np.fromfile('/tmp/block0.weights','<f2').copy();assert len(w)==10848
w[:4096]=0
w[4616:4648]=1
w[4656:6192]=0
w[10296:10808]=0
w[10808:10840]=1
w.view('<f4')[10288//2]=1
w.tofile(p/'probe.weights')
x=np.ones((8,8,4),'<f4');x[:,:,:3]=args.rgb
if args.gradient:
 yy,xx=np.indices((8,8));x[:,:,0]=xx/7;x[:,:,1]=yy/7;x[:,:,2]=(xx+yy)/14
x.tofile(p/'input.rgba32f')
