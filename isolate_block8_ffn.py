"""Expose original FFN residual with zero attention projection and unit skip."""
from pathlib import Path
import json,subprocess
import numpy as np
from native_c64_reference import unpack,multiply,contract
from native_c32_reference import F,H
from decode_tinlayout_global import e4m3fn
base=Path('release/native-rgb-valid1080/encoder-c64');root=base/'window46-18'
raw=np.fromfile(base/'block8.weights',np.uint8).copy();_,_,projection,_,ats=contract(64)
raw[projection:projection+64*64]=0;raw[ats:ats+128].view('<f2')[:]=1
raw.tofile(root/'ffn-control.weights')
subprocess.run(['/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(root/'ffn-control.weights'),str(root/'input.fp8'),str(root/'ffn-control.fp8'),str(root/'ffn-control-aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_fp8','8','8','1','1','2','7','0'],check=True,timeout=20)
inv=np.argsort(np.load('release/native-c64/view/mapping.npz')['cell_output_to_hwc'])
out=np.fromfile(root/'ffn-control.fp8',np.uint8);assert not out[4096:].any() and not np.any((out[:4096]&127)==127)
actual=e4m3fn(out[:4096].reshape(-1,1024)[:,inv]).reshape(2,2,4,4,64).transpose(0,2,1,3,4).reshape(1,64,64)
x=np.fromfile(root/'input.f32',np.float32).reshape(1,64,64);ffn=unpack(base/'block8.weights')[0]
expanded=multiply(F(x),ffn['W1']);gate=np.clip(expanded,-4,4)
poly=H(gate*H(abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))
hidden=F(H(expanded*poly));middle=F(multiply(hidden,ffn['W2']));expected=F(multiply(middle,ffn['W3'],H(x*ffn['skip'])))
idx=np.argwhere(actual!=expected);r={'scope':'modified-weight diagnostic only, not original model acceptance','ffn_different':len(idx),'mismatches':[{'index':i.tolist(),'original_control':float(actual[tuple(i)]),'reference':float(expected[tuple(i)])} for i in idx[:32]]}
(root/'ffn-control.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
