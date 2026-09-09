"""Validate recovered V/P with original coefficients and uniform attention."""
from pathlib import Path
import json,subprocess,os
import numpy as np
from native_c32_reference import H,F
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c64');folder=root/'attention-layout'
layout=np.load(folder/'matrix-layout.npz');original=np.fromfile(root/'block5.weights',np.uint8)
v=np.empty((64,64),np.float32);p=np.empty_like(v)
v[layout['v_output'],layout['v_input']]=e4m3fn(original[layout['v_offsets']])
p[layout['p_output'],layout['p_input']]=e4m3fn(original[0xe0b0:0xf0b0])
weights=np.zeros_like(original);weights.view('<f2')[0x7010//2:0x7090//2]=1
weights.view('<f4')[0xe0a0//4:0xe0a0//4+2]=1
weights[layout['v_offsets']]=original[layout['v_offsets']];weights[0xe0b0:0xf0b0]=original[0xe0b0:0xf0b0]
weights.tofile(folder/'uniform.weights')
inverse=np.argsort(np.load(root/'view/mapping.npz')['cell_output_to_hwc'])
channels=np.arange(64);perm=(channels&~3)|((channels&1)<<1)|((channels&2)>>1)
reports=[]
environment={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
for seed,scale in [(31,.25),(47,3.)]:
 source=F(np.random.default_rng(seed).normal(0,scale,(16,32,64)).astype(np.float32))
 quantize(source[...,perm]).reshape(16,32,4,16).transpose(2,0,1,3).copy().tofile(folder/'uniform-input.fp8')
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(folder/'uniform.weights'),str(folder/'uniform-input.fp8'),str(folder/'uniform-output.fp8'),str(folder/'uniform-aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_inpview_fp8','32','16','4','2','2','7','0'],check=True,capture_output=True,env=environment)
 target=e4m3fn(np.fromfile(folder/'uniform-output.fp8',np.uint8)[:32768].reshape(32,1024)[:,inverse]).reshape(4,8,4,4,64).transpose(0,2,1,3,4).reshape(source.shape)
 values=F(H(H(source[...,:32]@v[:,:32].T)+source[...,32:]@v[:,32:].T))
 tiles=values.reshape(2,8,4,8,64).transpose(0,2,1,3,4).reshape(8,64,64)
 av=H(H(tiles[:,:32].sum(1)*np.float32(1/64))+tiles[:,32:].sum(1)*np.float32(1/64))
 av=F(av);out=F(H(H(av[:,:32]@p[:,:32].T)+av[:,32:]@p[:,32:].T))
 predicted=np.broadcast_to(out[:,None,None,:],(8,8,8,64)).reshape(2,4,8,8,64).transpose(0,2,1,3,4).reshape(source.shape)
 error=np.abs(predicted-target)
 report={'seed':seed,'scale':scale,'exact_fraction':float(np.mean(predicted==target)),'mae':float(error.mean()),'max_error':float(error.max())};reports.append(report)
 print(json.dumps(report),flush=True)
 assert np.array_equal(predicted,target), 'uniform attention V/P differs from original'
np.savez(folder/'vp-matrices.npz',V=v,P=p)
