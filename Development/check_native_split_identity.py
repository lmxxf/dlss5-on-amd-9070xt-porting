"""Check split-Swin skip identity before using the four-call runner as an oracle."""
from pathlib import Path
import json,subprocess
import numpy as np
from encode_tinlayout_global import quantize
root=Path('release/native-c512');folder=root/'identity';folder.mkdir(parents=True,exist_ok=True)
for i,size in enumerate((524288,263168,917568,263168)):
 w=np.zeros(size,np.uint8)
 if i in (1,3):w.view('<f2')[262144//2:]=1
 if i==2:w.view('<f4')[917504//4:]=1
 w.tofile(folder/f'w{i}.bin')
for width,height in [(4,2),(4,4),(8,4),(4,8),(8,8),(16,8)]:
 count=width*height*512
 source=quantize(np.random.default_rng(431).normal(0,1,count).astype(np.float32))
 source.tofile(folder/'input.fp8');output=folder/f'output-{width}x{height}.fp8'
 run=subprocess.run(['/tmp/native-split-global-oracle',str(folder/'input.fp8'),str(output),*[str(folder/f'w{i}.bin') for i in range(4)],str(width),str(height),'0','native-inpview'],capture_output=True,text=True)
 raw=np.fromfile(output,np.uint8);valid=raw[:count].copy();source[(source&127)==0]=0;valid[(valid&127)==0]=0
 exact=np.array_equal(np.sort(source),np.sort(valid)) and not np.any(raw[count:])
 print(json.dumps({'extent':[width,height],'returncode':run.returncode,'identity_multiset_exact':exact,'output_nonzero':int(np.count_nonzero(valid))}),flush=True)
 if width>=4 and height>=4:assert exact and run.returncode==0, 'split identity changed values or output extent'
