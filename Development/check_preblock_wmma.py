"""Compare actual HMMA input mixing, exact sums and independent original views."""
from pathlib import Path
import subprocess,json
import numpy as np
from native_c32_reference import block,H,F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb512');case=np.load(root/'tile-diagnostic.npz')
fw=np.fromfile(root/'amd/block0-ffn.f32','<f4');aw=np.fromfile(root/'amd/block0-attention.f32','<f4')
case['features'].astype('<f2').tofile(root/'wmma-input.f16');fw[:512].astype('<f2').tofile(root/'wmma-weights.f16')
subprocess.run(['/tmp/probe-preblock-wmma',str(root/'wmma-input.f16'),str(root/'wmma-weights.f16'),str(root/'wmma-output.f16')],check=True)
prefix=np.fromfile(root/'wmma-output.f16','<f2').astype(np.float32).reshape(8,8,32)
original=np.memmap(root/'stage-audit/mix.fp8',dtype=np.uint8,mode='r');base=47*64*2048+48*1024
record=np.concatenate([original[base:base+1024],original[base+64*1024:base+65*1024]])
pm=np.argmax(np.abs(np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048)),axis=0)
mixed=e4m3fn(record[pm]).reshape(8,8,32)
w=(fw[512:4608].reshape(128,32),fw[4608:8704].reshape(32,128),*aw[:4096].reshape(4,32,32),aw[4096:8192].reshape(64,64),aw[8192],fw[8704:],aw[8193:])
raw=block(prefix.reshape(1,64,32),w,raw_output=True).reshape(8,8,32)
rows=H(raw[:,::2]+raw[:,1::2]);down=F(H(H(rows[::2]+rows[1::2])*.25))
for name,a,b in [('WMMA-half/exact-half',prefix,case['prefix']),('WMMA-mix/original',F(prefix),mixed),('WMMA-driven-main/original',F(raw),case['original_main']),('WMMA-driven-down/original',down,case['original_down'])]:
 error=np.abs(a-b);print(json.dumps({'comparison':name,'different':int(np.count_nonzero(error)),'max_error':float(error.max())}),flush=True)
print(json.dumps({'target_pixel_wmma_half':float(prefix[6,0,0]),'target_pixel_exact_half':float(case['prefix'][6,0,0])}))
