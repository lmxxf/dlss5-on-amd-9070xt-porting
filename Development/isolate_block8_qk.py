"""Zero Q or K in diagnostic copies to isolate score-dependent differences."""
from pathlib import Path
import json,subprocess
import numpy as np
from native_c64_reference import block,unpack
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18')
base=np.fromfile(root/'attention-control.weights',np.uint8)
layout=np.load('release/native-c64/attention-layout/matrix-layout.npz')
inv=np.argsort(np.load('release/native-c64/view/mapping.npz')['cell_output_to_hwc'])
x=np.fromfile(root/'input.f32',np.float32).reshape(1,64,64);checks=[]
for name,delta in [('zero-q',-0x800),('zero-k',-0x400)]:
 raw=base.copy();raw[layout['v_offsets']+delta]=0;weights=root/f'{name}.weights';raw.tofile(weights)
 output=root/f'{name}.fp8'
 subprocess.run(['/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(weights),str(root/'input.fp8'),str(output),str(root/f'{name}-aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_fp8','8','8','1','1','2','7','0'],check=True,timeout=20)
 raw=np.fromfile(output,np.uint8);assert not raw[4096:].any() and not np.any((raw[:4096]&127)==127)
 actual=e4m3fn(raw[:4096].reshape(-1,1024)[:,inv]).reshape(2,2,4,4,64).transpose(0,2,1,3,4).reshape(1,64,64)
 expected=block(x,*unpack(weights));checks.append({'control':name,'different':int(np.count_nonzero(actual!=expected)),'finite':bool(np.isfinite(actual).all() and np.isfinite(expected).all())})
r={'scope':'modified weight controls, not original-model acceptance','checks':checks};(root/'qk-controls.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
