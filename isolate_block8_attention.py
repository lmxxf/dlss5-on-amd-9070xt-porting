"""Expose attention output using identity projection in a diagnostic copy."""
from pathlib import Path
import subprocess,json
import numpy as np
from native_c64_reference import unpack,block,contract
from decode_tinlayout_global import e4m3fn
base=Path('release/native-rgb-valid1080/encoder-c64');root=base/'window46-18'
raw=np.fromfile(base/'block8.weights',np.uint8).copy();_,_,p,_,ats=contract(64)
layout=np.load('release/native-c64/attention-layout/matrix-layout.npz')
raw[p:p+4096]=np.where(layout['p_output']==layout['p_input'],0x38,0).astype(np.uint8)
raw[ats:ats+128]=0;raw.tofile(root/'attention-control.weights')
subprocess.run(['/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(root/'attention-control.weights'),str(root/'input.fp8'),str(root/'attention-control.fp8'),str(root/'attention-control-aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_fp8','8','8','1','1','2','7','0'],check=True,timeout=20)
inv=np.argsort(np.load('release/native-c64/view/mapping.npz')['cell_output_to_hwc']);out=np.fromfile(root/'attention-control.fp8',np.uint8)
assert not out[4096:].any() and not np.any((out[:4096]&127)==127)
actual=e4m3fn(out[:4096].reshape(-1,1024)[:,inv]).reshape(2,2,4,4,64).transpose(0,2,1,3,4).reshape(1,64,64)
x=np.fromfile(root/'input.f32',np.float32).reshape(1,64,64)
params=unpack(root/'attention-control.weights');assert np.array_equal(params[2],np.eye(64)) and not params[-1].any()
expected=block(x,*params);idx=np.argwhere(actual!=expected)
r={'scope':'identity projection diagnostic; not original model acceptance','attention_different':len(idx),'mismatches':[{'index':i.tolist(),'original_control':float(actual[tuple(i)]),'reference':float(expected[tuple(i)])} for i in idx[:32]]}
actual.tofile(root/'attention-original.f32');expected.tofile(root/'attention-reference.f32')
(root/'attention-control.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
