"""Verify C64/C128 downsample main, projection, and independently coded view."""
from pathlib import Path
import json,os,subprocess,argparse
import numpy as np
from native_c64_reference import unpack,block,multiply,contract
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser();parser.add_argument('--channels',type=int,choices=(64,128),default=64);args=parser.parse_args()
C=args.channels;out_channels=2*C;heads=C//32
width,height,index,shift,size,ds_begin=(32,16,8,2,69936,0xf130) if C==64 else (16,8,14,3,229936,0x30230)
root=Path(f'release/native-c{C}');count=width*height*C;ds_count=count//2
cubin='/tmp/dlssnr-cubins/dlssnr-01.cubin' if C==64 else '/tmp/dlssnr-cubins/dlssnr-02.cubin'
symbol=f'cc_tinlayout_fused_swin_{heads}h_{C}_{heads}_ds_fp8'
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
inverse=np.argsort(np.load(root/'view/mapping.npz')['cell_output_to_hwc'])
def decode(name):
 raw=np.fromfile(root/name,np.uint8);assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
 return e4m3fn(raw[:count].reshape(-1,16*C)[:,inverse]).reshape(height//4,width//4,4,4,C).transpose(0,2,1,3,4).reshape(height,width,C)
subprocess.run(['/tmp/native-c32-global-oracle',cubin,str(root/f'block{index}.weights'),str(root/f'block{index-1}-output.fp8'),str(root/f'block{index}-output.fp8'),str(root/f'block{index}-aux.fp8'),symbol,str(width),str(height),str(width//8+bool(shift&1)),str(height//8+bool(shift&2)),str(heads),'8',str(shift)],env=env,check=True,capture_output=True)
x=decode(f'block{index-1}-output.fp8');px=4 if shift&1 else 0;py=4 if shift&2 else 0
padded=np.pad(x,((py,py),(px,px),(0,0)));hh,ww=padded.shape[:2]
tiles=padded.reshape(hh//8,8,ww//8,8,C).transpose(0,2,1,3,4).reshape(-1,64,C)
result=block(tiles,*unpack(root/f'block{index}.weights'),raw_output=True).reshape(hh//8,ww//8,8,8,C).transpose(0,2,1,3,4).reshape(padded.shape)[py:py+height,px:px+width]
assert np.array_equal(F(result),decode(f'block{index}-output.fp8')), 'DS block main differs'
rows=H(result[:,::2]+result[:,1::2]);pooled=F(H(H(rows[::2]+rows[1::2])*.25))
layout=np.load(root/'ds-layout/layout.npz');weights=np.fromfile(root/f'block{index}.weights',np.uint8)[ds_begin:]
N=out_channels*C;matrix=np.empty((out_channels,C),np.float32)
matrix[layout['output'],layout['input']]=e4m3fn(weights)
prediction=F(multiply(pooled,matrix))
channels=np.arange(out_channels);perm=(channels&~3)|((channels&1)<<1)|((channels&2)>>1)
raw=np.fromfile(root/f'block{index}-aux.fp8',np.uint8);assert not np.any(raw[ds_count:]) and not np.any((raw[:ds_count]&127)==127)
target=e4m3fn(raw[:ds_count]).reshape(out_channels//16,height//2,width//2,16).transpose(1,2,0,3).reshape(height//2,width//2,out_channels)
error=np.abs(prediction[...,perm]-target)
print(json.dumps({'block':index,'main_exact':True,'DS_exact_fraction':float(np.mean(prediction[...,perm]==target)),'DS_mae':float(error.mean()),'DS_max_error':float(error.max())}),flush=True)
# Two base-16 row-code probes support all 256 rows with distinct, finite,
# nonzero FP8 codes; neither NaN encodings nor signed-zero ambiguity is used.
fs,scale,p_begin,bias_begin,ats=contract(C)
cell_map=np.load(root/'view/mapping.npz')['cell_output_to_hwc']
logical=np.zeros((4,4,C),np.uint8);logical[:,:,0]=0x38
np.tile(logical.ravel()[cell_map],4).tofile(root/'ds-coded-input.fp8')
slots=np.flatnonzero(layout['input']==0)
observed_digits=[]
for digit in (0,1):
 control=np.zeros(size,np.uint8);control.view('<f2')[fs//2:fs//2+C]=1
 control.view('<f2')[ats//2:ats//2+C]=1;control.view('<f4')[scale//4:scale//4+heads]=1
 codes=(8+((channels>>(4*digit))&15)).astype(np.uint8)
 control[ds_begin+slots]=codes[layout['output'][slots]]
 control.tofile(root/'ds-coded.weights')
 subprocess.run(['/tmp/native-c32-global-oracle',cubin,str(root/'ds-coded.weights'),str(root/'ds-coded-input.fp8'),str(root/'ds-coded-main.fp8'),str(root/'ds-coded-aux.fp8'),symbol,'8','8',str(1+bool(shift&1)),str(1+bool(shift&2)),str(heads),'8',str(shift)],env=env,check=True,capture_output=True)
 observed=np.fromfile(root/'ds-coded-aux.fp8',np.uint8);assert not np.any(observed[16*out_channels:])
 observed=observed[:16*out_channels].reshape(out_channels//16,4,4,16).transpose(1,2,0,3).reshape(4,4,out_channels)
 print(json.dumps({'digit':digit,'observed_codes':np.unique(observed).tolist(),'spatially_constant':bool(np.all(observed==observed[0,0]))}),flush=True)
 assert np.all(observed==observed[0,0]) and np.all((observed>=8)&(observed<=23)), 'coded rows not uniquely observable'
 observed_digits.append(observed[0,0].astype(np.int32)-8)
actual_order=observed_digits[0]+16*observed_digits[1]
assert np.array_equal(np.sort(actual_order),channels), 'coded rows not bijective'
print(json.dumps({'coded_row_order':actual_order.tolist()}),flush=True)
assert np.array_equal(prediction[...,actual_order],target), 'DS projection differs with measured row order'
print(json.dumps({'coded_order_exact':True,'output_channels':out_channels,'coded_probes':2}))
