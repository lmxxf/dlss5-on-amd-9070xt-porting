"""Original preblock FFN-only reference using basis-recovered FP8 weights."""
import argparse,json
from pathlib import Path
import numpy as np
from preblock_mix_reference import inputs,unpack_mix
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('--prepare',action='store_true');a=p.parse_args()
a.folder.mkdir(parents=True,exist_ok=True)
original=np.fromfile('/tmp/block0.weights','<f2').copy()
if a.prepare:
 w=original.copy();w[4656:6192]=0;w[10296:10808]=0;w[10808:10840]=1;w.view('<f4')[10288//2]=1
 w.tofile(a.folder/'ffn-only.weights')
 v=np.zeros_like(w);v[10808:10840]=1;v.view('<f4')[10288//2]=1
 s=np.arange(512);feature=(s//8%4)*4+s%4;v[4104+s[feature==5]]=0.5
 v.tofile(a.folder/'skip-probe.weights')
else:
 h=lambda x:np.asarray(x,dtype=np.float16).astype(np.float32)
 q=lambda x:e4m3fn(quantize(x))
 layout=np.load('release/preblock-ffn-byte-layout/layout.npz')
 skipmat=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048)
 perm=np.argmax(np.abs(skipmat),axis=0)
 probes=np.fromfile(a.folder/'skip-main.fp8',np.uint8).reshape(32,2048)[:,perm].reshape(32,64,32)
 active=np.any(probes!=0,axis=1);assert np.all(active.sum(1)==1)
 channel=np.argmax(active,axis=1);assert np.unique(channel).size==32
 fs=np.empty(32,np.float32);fs[channel]=original[4616:4648]
 rgb=np.fromfile('release/preblock-branch-audit/input.rgba32f','<f4').reshape(-1,8,8,4)
 prefix=h(np.stack([inputs(t[:,:,:3])@unpack_mix(original).T for t in rgb]))
 expanded=h(q(prefix)@layout['W1'].T)
 gate=np.clip(expanded,-4,4)
 polynomial=h(gate*h(np.abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))
 hidden=h(expanded*polynomial)
 hidden_q=q(hidden)
 projected=h(prefix*fs)
 for start in range(0,128,32):
  projected=h(projected+hidden_q[...,start:start+32]@layout['W2'][:,start:start+32].T)
 predicted=q(projected)
 target=e4m3fn(np.fromfile(a.folder/'ffn-main.fp8',np.uint8).reshape(-1,2048)[:,perm]).reshape(predicted.shape)
 error=np.abs(predicted-target)
 print(json.dumps({'correlation':float(np.corrcoef(predicted.ravel(),target.ravel())[0,1]),'exact_fraction':float(np.mean(predicted==target)),'mae':float(error.mean()),'max_error':float(error.max()),'skip_slot_to_channel':channel.tolist()},indent=2))
 assert np.array_equal(predicted,target), 'FFN-only original-CUBIN regression'
