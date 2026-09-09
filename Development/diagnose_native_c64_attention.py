"""Separate nonuniform bias and QK arithmetic with an identity FFN."""
from pathlib import Path
import os,json,subprocess
import numpy as np
from native_c64_reference import block
from native_c32_reference import F
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c64');folder=root/'attention-layout'
parameters=np.load(folder/'full-matrices.npz');layout=np.load(folder/'matrix-layout.npz')
original=np.fromfile(root/'block5.weights',np.uint8)
inverse=np.argsort(np.load(root/'view/mapping.npz')['cell_output_to_hwc'])
c=np.arange(64);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
source=F(np.random.default_rng(79).normal(0,1,(16,32,64)).astype(np.float32))
quantize(source[...,perm]).reshape(16,32,4,16).transpose(2,0,1,3).copy().tofile(folder/'diagnostic-input.fp8')
tiles=source.reshape(2,8,4,8,64).transpose(0,2,1,3,4).reshape(8,64,64)
ffn={'W1':np.zeros((256,64),np.float32),'W2':np.zeros((64,256),np.float32),'W3':np.zeros((64,64),np.float32),'skip':np.ones(64,np.float32)}
environment={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
for mode in ('uniform','bias_only','qk_only','full','full_block'):
 w=original.copy()
 if mode!='full_block':w[:0x7000]=0;w.view('<f2')[0x7010//2:0x7090//2]=1
 qkv=[parameters[name].copy() for name in ('Q','K','V')];bias=parameters['bias'].copy()
 if mode in ('uniform','bias_only'):
  for offset in (-0x800,-0x400):w[layout['v_offsets']+offset]=0
  qkv[0][:]=0;qkv[1][:]=0
 if mode in ('uniform','qk_only'):w[0xa0a0:0xe0a0]=0;bias[:]=0
 w.tofile(folder/'diagnostic.weights')
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(folder/'diagnostic.weights'),str(folder/'diagnostic-input.fp8'),str(folder/'diagnostic-output.fp8'),str(folder/'diagnostic-aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_inpview_fp8','32','16','4','2','2','7','0'],env=environment,check=True,capture_output=True)
 target=e4m3fn(np.fromfile(folder/'diagnostic-output.fp8',np.uint8)[:32768].reshape(32,1024)[:,inverse]).reshape(4,8,4,4,64).transpose(0,2,1,3,4).reshape(source.shape)
 block_ffn=np.load(root/'ffn-layout/matrices.npz') if mode=='full_block' else ffn
 result=block(tiles,block_ffn,qkv,parameters['P'],bias,parameters['scales'],parameters['skip']).reshape(2,4,8,8,64).transpose(0,2,1,3,4).reshape(source.shape)
 error=np.abs(result-target)
 print(json.dumps({'mode':mode,'exact_fraction':float(np.mean(result==target)),'mae':float(error.mean()),'max_error':float(error.max())}),flush=True)
 assert np.array_equal(result,target), 'C64 isolated or full-block regression'
