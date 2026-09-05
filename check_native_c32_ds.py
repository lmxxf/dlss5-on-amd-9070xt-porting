"""Compare native block4 DS projection with independent original CUBIN output."""
import json
import subprocess
from pathlib import Path
import numpy as np
from native_c32_reference import block, unpack, H, F
from decode_tinlayout_global import e4m3fn

root=Path('release/native-c32')
mapping=np.argmax(np.abs(np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048)),axis=0).reshape(8,8,32)[:4,:4]
raw=np.fromfile(root/'block3-output.fp8',np.uint8)[:64*32*32]
source=e4m3fn(raw.reshape(-1,512)[:,mapping]).reshape(8,16,4,4,32).transpose(0,2,1,3,4).reshape(32,64,32)
padded=np.pad(source,((4,4),(0,0),(0,0)))
tiles=padded.reshape(5,8,8,8,32).transpose(0,2,1,3,4).reshape(-1,64,32)
result=block(tiles,unpack(root/'block4.weights'),raw_output=True).reshape(5,8,8,8,32).transpose(0,2,1,3,4).reshape(40,64,32)[4:36]
rows=H(result[:,::2]+result[:,1::2]);pooled=F(H(H(rows[::2]+rows[1::2])*.25))
layout=np.load('release/preblock-ffn-byte-layout/layout.npz')
weights=np.fromfile(root/'block4.weights',np.uint8)[20656:22704]
matrix=np.empty((64,32),np.float32)
matrix[layout['w1_hidden'][:2048],layout['w1_input'][:2048]]=e4m3fn(weights)
predicted=F(H(pooled@matrix.T))
raw=np.fromfile(root/'block4-aux.fp8',np.uint8)[:32*16*64]
reports={}
for bank in (8,16,32,64):
 target=e4m3fn(raw).reshape(64//bank,16,32,bank).transpose(1,2,0,3).reshape(predicted.shape)
 reports[str(bank)]=dict(exact_fraction=float(np.mean(predicted==target)),mae=float(np.abs(predicted-target).mean()),correlation=float(np.corrcoef(predicted.ravel(),target.ravel())[0,1]))
# Independent coded projection probe: every spatial input has channel0=1;
# output row r carries the unique FP8 byte 8+r. No image correlation fitting.
control=np.zeros(22720,np.uint8);half=control[:20656].view('<f2')
half[4104:4136]=1;half[10296:10328]=1
control.view('<f4')[19552//4]=1
slots=np.flatnonzero(layout['w1_input'][:2048]==0)
control[20656+slots]=8+layout['w1_hidden'][slots]
control.tofile(root/'ds-coded.weights')
cells=np.zeros((128,512),np.uint8);logical=np.zeros((4,4,32),np.uint8);logical[:,:,0]=0x38
cells[:,mapping]=logical;cells.tofile(root/'ds-coded-input.fp8')
subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',str(root/'ds-coded.weights'),str(root/'ds-coded-input.fp8'),str(root/'ds-coded-main.fp8'),str(root/'ds-coded-aux.fp8'),'cc_tinlayout_fused_swin_1h_32_1_ds_fp8','64','32','8','5','1','6','2'],check=True,capture_output=True)
codes=np.fromfile(root/'ds-coded-aux.fp8',np.uint8)[:32768].reshape(4,16,32,16).transpose(1,2,0,3).reshape(16,32,64)
physical_to_logical=codes[0,0].astype(np.int32)-8
assert sorted(physical_to_logical.tolist())==list(range(64))
assert np.all(codes==8+physical_to_logical), 'projection probe not spatially constant'
target=e4m3fn(raw).reshape(4,16,32,16).transpose(1,2,0,3).reshape(predicted.shape)
aligned=predicted[...,physical_to_logical]
reports['coded_projection_row_order']=physical_to_logical.tolist()
reports['correct_view']=dict(exact_fraction=float(np.mean(aligned==target)),mae=float(np.abs(aligned-target).mean()),max_error=float(np.abs(aligned-target).max()))
print(json.dumps(reports,indent=2))
assert np.array_equal(aligned,target), 'block4 DS differs from original CUBIN'
