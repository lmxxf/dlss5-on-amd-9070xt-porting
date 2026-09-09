"""Recover original FP8 FFN connectivity with binary probes; no fitting."""
import argparse,subprocess,json
from pathlib import Path
import numpy as np
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('--oracle',default='/tmp/preblock-branch-oracle');p.add_argument('--cubin',default='/tmp/dlssnr-cubins/dlssnr-00.cubin');p.add_argument('--skip-matrix',type=Path,default=Path('release/post-skip-basis/matrix.f32'));a=p.parse_args()
a.folder.mkdir(parents=True,exist_ok=True)
s=np.arange(512);channel=(s//64)*4+(s//32%2)+2*(s//4%2);feature=(s//8%4)*4+s%4
x=np.ones((8,8,4),'<f4');x[:,:,:3]=0.5;x.tofile(a.folder/'input.rgba32f')
perm=np.argmax(np.abs(np.fromfile(a.skip_matrix,'<f4').reshape(2048,2048)),axis=0)
assert np.unique(perm).size==2048
def control(selected):
 v=np.zeros(10848,'<f2');v[10808:10840]=1;v.view('<f4')[10288//2]=1
 v[4104+s[(feature==5)&np.isin(channel,selected)]]=0.5
 return v
def scan(tag,v,mode):
 path=a.folder/(tag+'.weights');main=a.folder/(tag+'-main.fp8');ds=a.folder/(tag+'-ds.fp8')
 v.tofile(path)
 subprocess.run([a.oracle,a.cubin,str(path),str(a.folder/'input.rgba32f'),str(main),str(ds),'0','0',mode],check=True,capture_output=True)
 raw=np.fromfile(main,np.uint8).reshape(4096,2048)[:,perm].reshape(4096,64,32)
 count=(raw!=0).sum((1,2));assert np.all(np.isin(count,[0,64,2048]))
 print(tag,'active',int((count!=0).sum()),flush=True)
 return count!=0,raw
input_index=np.zeros(4096,np.int32)
for bit in range(5):
 v=control(np.flatnonzero(np.arange(32)&(1<<bit)));v.view(np.uint8)[4096:8192]=0x38
 active,_=scan(f'input-{bit}',v,'ffn1-byte-scan');input_index|=active.astype(np.int32)<<bit
assert np.all(np.bincount(input_index,minlength=32)==128)
representatives=np.flatnonzero(input_index==0)
hidden_index=np.zeros(4096,np.int32)
for bit in [-1,0,1,2,3,4,5,6]:
 v=control([0]);chosen=np.ones(128,bool) if bit<0 else (np.arange(128)&(1<<bit))!=0
 v.view(np.uint8)[representatives[chosen]]=0x38
 active,raw=scan(f'hidden-{bit}',v,'ffn2-byte-scan')
 if bit<0:
  assert active.all();present=np.any(raw!=0,axis=1);assert np.all(present.sum(1)==1)
  output_index=np.argmax(present,axis=1)
 else:hidden_index|=active.astype(np.int32)<<bit
assert np.unique(output_index*128+hidden_index).size==4096
row_index=np.zeros(4096,np.int32)
for bit in range(7):
 v=control(np.arange(32));v.view(np.uint8)[4096+np.flatnonzero((output_index==0)&((hidden_index&(1<<bit))!=0))]=0x38
 active,_=scan(f'row-{bit}',v,'ffn1-byte-scan');row_index|=active.astype(np.int32)<<bit
assert np.unique(row_index*32+input_index).size==4096
np.testing.assert_array_equal(row_index[representatives],np.arange(128))
original=np.fromfile('/tmp/block0.weights',np.uint8)
w1=np.empty((128,32),np.float32);w2=np.empty((32,128),np.float32)
w1[row_index,input_index]=e4m3fn(original[:4096]);w2[output_index,hidden_index]=e4m3fn(original[4096:8192])
np.savez(a.folder/'layout.npz',w1_input=input_index,w1_hidden=row_index,w2_hidden=hidden_index,w2_output=output_index,W1=w1,W2=w2)
print(json.dumps({'w1_connections':4096,'w2_connections':4096,'hidden':128,'storage':'E4M3','method':'binary-coded exact connectivity, no fitted coefficients'}))
