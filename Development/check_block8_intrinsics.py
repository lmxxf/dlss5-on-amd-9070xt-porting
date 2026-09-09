"""Compare hardware approximate inverses at actual query normalization inputs."""
from pathlib import Path
import numpy as np,json,subprocess
from native_c32_reference import H
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18');d=np.load(root/'query-reference.npz')
def sums(v):
 s=H(v[...,:16]*v[...,:16]+H(v[...,16:]*v[...,16:]))
 s=H(s[...,::2]+s[...,1::2])
 for w in (4,2,1):s=H(s[...,:w]+s[...,w:2*w])
 return np.maximum(s,6.198883056640625e-5).ravel()
values=np.concatenate([sums(d['q']),sums(d['k']),d['den'].ravel()]).astype(np.float32);values.tofile(root/'intrinsic-input.f32')
subprocess.run(['/tmp/probe-normalize-intrinsics',str(root/'intrinsic-input.f32'),str(root/'intrinsic-output.f32')],check=True,timeout=10)
gpu=np.fromfile(root/'intrinsic-output.f32',np.float32).reshape(-1,2)
ref=np.stack([1/values,1/np.sqrt(values)],axis=1);idx=np.argwhere(H(gpu)!=H(ref))
r={'scope':'local GPU approximate instruction control, not original full kernel trace','different_half_results':len(idx),'mismatches':[{'input_index':int(i),'operation':'rcp' if c==0 else 'rsqrt','input':float(values[i]),'gpu_half':float(H(gpu)[i,c]),'reference_half':float(H(ref)[i,c])} for i,c in idx]}
(root/'intrinsic-comparison.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
