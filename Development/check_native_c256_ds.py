"""Check block22 H4 semantics and the padded C512 DS output view."""
from pathlib import Path
import json,os,subprocess
import numpy as np
from native_c64_reference import unpack,block,multiply
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c256');inverse=np.argsort(np.load(root/'view/mapping.npz')['cell_output_to_hwc'])
def main(name):
 raw=np.fromfile(root/name,np.uint8);assert not np.any(raw[8192:]) and not np.any((raw[:8192]&127)==127)
 return e4m3fn(raw[:8192].reshape(2,4096)[:,inverse]).reshape(1,2,4,4,256).transpose(0,2,1,3,4).reshape(4,8,256)
x=main('block21-output.fp8');target=main('block22-output.fp8');parameters=unpack(root/'block22.weights');matched=[]
for mode in ('zero','repeat'):
 padded=np.pad(x,((4,0),(0,0),(0,0))) if mode=='zero' else x[(np.arange(8)-4)%4]
 result=block(padded.reshape(1,64,256),*parameters,raw_output=True).reshape(8,8,256)[4:]
 exact=np.array_equal(F(result),target)
 print(json.dumps({'main_boundary':mode,'exact_fraction':float(np.mean(F(result)==target)),'max_error':float(np.abs(F(result)-target).max())}),flush=True)
 if exact:matched.append((mode,result))
assert len(matched)==1, 'DS main boundary must be uniquely identified'
mode,result=matched[0];rows=H(result[:,::2]+result[:,1::2]);pooled=F(H(H(rows[::2]+rows[1::2])*.25))
layout=np.load(root/'ds-layout/layout.npz');raw_weights=np.fromfile(root/'block22.weights',np.uint8)[0xa8440:]
matrix=np.empty((512,256),np.float32);matrix[layout['output'],layout['input']]=e4m3fn(raw_weights)
prediction=F(multiply(pooled,matrix));raw=np.fromfile(root/'block22-aux.fp8',np.uint8)
assert not np.any(raw[8192:]) and not np.any((raw[:8192]&127)==127)
full=e4m3fn(raw[:8192]).reshape(32,4,4,16).transpose(1,2,0,3).reshape(4,4,512)
assert not np.any(full[2:]), 'unexpected data in padded output rows'
channels=np.arange(512);perm=(channels&~3)|((channels&1)<<1)|((channels&2)>>1)
error=np.abs(prediction[...,perm]-full[:2])
print(json.dumps({'boundary':mode,'DS_exact_fraction':float(np.mean(prediction[...,perm]==full[:2])),'DS_mae':float(error.mean()),'DS_max_error':float(error.max())}),flush=True)
assert np.array_equal(prediction[...,perm],full[:2]), 'block22 DS differs'
# All 512 rows are distinguished using three hexadecimal digits; every
# digit uses only finite positive E4M3 values and must preserve padding holes.
cell=np.load(root/'view/mapping.npz')['cell_output_to_hwc']
logical=np.zeros((4,4,256),np.uint8);logical[:,:,0]=0x38
np.tile(logical.ravel()[cell],2).tofile(root/'ds512-coded-input.fp8')
slots=np.flatnonzero(layout['input']==0)
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
for digit in (0,1,2):
 control=np.zeros(820288,np.uint8);control.view('<f2')[0x58010//2:0x58210//2]=1
 control.view('<f2')[0xa8240//2:0xa8440//2]=1;control.view('<f4')[0x98220//4:0x98220//4+8]=1
 codes=(8+((channels>>(4*digit))&15)).astype(np.uint8)
 control[0xa8440+slots]=codes[layout['output'][slots]];control.tofile(root/'ds512-coded.weights')
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-03.cubin',str(root/'ds512-coded.weights'),str(root/'ds512-coded-input.fp8'),str(root/'ds512-coded-main.fp8'),str(root/'ds512-coded-aux.fp8'),'cc_tinlayout_fused_swin_8h_256_8_ds_fp8','8','4','1','1','8','8','2'],env=env,check=True,capture_output=True)
 observed=np.fromfile(root/'ds512-coded-aux.fp8',np.uint8);assert not np.any(observed[8192:])
 observed=observed[:8192].reshape(32,4,4,16).transpose(1,2,0,3).reshape(4,4,512)
 assert not np.any(observed[2:]) and np.all(observed[:2]==codes[perm]), 'C512 DS row codes/padding differ'
print(json.dumps({'coded_rows':512,'coded_probes':3,'valid_shape':[2,4,512],'physical_padded_shape':[4,4,512],'exact':True}))
from encode_tinlayout_global import quantize
for seed,scale in [(313,.25),(317,3.)]:
 source=F(np.random.default_rng(seed).normal(0,scale,(4,8,256)).astype(np.float32))
 encoded=quantize(source).reshape(1,4,2,4,256).transpose(0,2,1,3,4).reshape(2,4096)[:,cell]
 encoded.tofile(root/'ds512-random-input.fp8')
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-03.cubin',str(root/'block22.weights'),str(root/'ds512-random-input.fp8'),str(root/'ds512-random-main.fp8'),str(root/'ds512-random-aux.fp8'),'cc_tinlayout_fused_swin_8h_256_8_ds_fp8','8','4','1','1','8','8','2'],env=env,check=True,capture_output=True)
 raw_result=block(source[(np.arange(8)-4)%4].reshape(1,64,256),*parameters,raw_output=True).reshape(8,8,256)[4:]
 assert np.array_equal(F(raw_result),main('ds512-random-main.fp8'))
 pair=H(raw_result[:,::2]+raw_result[:,1::2]);pool=F(H(H(pair[::2]+pair[1::2])*.25))
 expected=F(multiply(pool,matrix))[...,perm]
 raw=np.fromfile(root/'ds512-random-aux.fp8',np.uint8);assert not np.any(raw[8192:])
 observed=e4m3fn(raw[:8192]).reshape(32,4,4,16).transpose(1,2,0,3).reshape(4,4,512)
 assert not np.any(observed[2:]) and np.array_equal(expected,observed[:2]), 'random block22 DS differs'
 print(json.dumps({'seed':seed,'scale':scale,'DS_values':4096,'exact_fraction':1.0}))
