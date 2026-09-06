"""Diagnose whether split ffwd's all-zero output depends on input extent."""
from pathlib import Path
import json,subprocess
import numpy as np
root=Path('release/native-c512');np.full(4*1024*1024,0x38,np.uint8).tofile(root/'ones-input.fp8')
for width,height in [(4,2),(8,8),(16,8)]:
 output=root/f'ones-{width}x{height}.fp8'
 result=subprocess.run(['/tmp/native-split-global-oracle',str(root/'ones-input.fp8'),str(output),*[str(root/f'block23-{i}.weights') for i in range(4)],str(width),str(height),'0','native-inpview'],capture_output=True,text=True)
 counts={}
 for suffix in ('.branch','.ffn','.attn',''):
  path=Path(str(output)+suffix)
  if path.exists():counts[suffix or 'final']=int(np.count_nonzero(np.fromfile(path,np.uint8)&127))
 print(json.dumps({'extent':[width,height],'returncode':result.returncode,'nonzero_by_stage':counts,'stderr':result.stderr.strip()}),flush=True)
