"""Generate linear source only; encoded/network/final oracles must be external."""
from pathlib import Path
import hashlib,json
import numpy as np
root=Path('release/native-color-frame')
root.mkdir(exist_ok=True)
path=root/'source.f16'
assert not path.exists(),'Preserve existing fixture; inspect before replacement'
y,x=np.indices((1080,1920),dtype=np.uint32)
source=np.empty((1080,1920,4),np.float16)
for c in range(3):
    value=((x*13+y*7+c*311)%2048).astype(np.float32)/1024
    value[(x+y)%29==0]*=4
    value[(x+y+c)%31==0]=0
    source[:,:,c]=value
source[:,:,3]=((x+y)%1024).astype(np.float32)/1024
source.tofile(path)
(root/'source.json').write_text(json.dumps(dict(scope='synthetic linear FP16 input, not an oracle',
 shape=list(source.shape),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
 game_verified=False),indent=2)+'\n')
print(path)
