"""Controlled V/projection and attention-bias probes for original preblock."""
import argparse,subprocess,json
from pathlib import Path
import numpy as np
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('--bias',action='store_true');p.add_argument('--matrices',action='store_true');a=p.parse_args();a.folder.mkdir(parents=True,exist_ok=True)
l=np.load('release/preblock-ffn-byte-layout/layout.npz')
outidx=l['w2_output'][:1024];inidx=l['w2_hidden'][:1024]
assert np.unique(outidx*32+inidx).size==1024
w=np.zeros(10848,'<f2');w[4616:4648]=1;w.view('<f4')[10288//2]=1
s=np.arange(512);ch=(s//64)*4+(s//32%2)+2*(s//4%2);feature=(s//8%4)*4+s%4
w[4104+s[(ch==0)&(feature==5)]]=0.5
slot=int(np.flatnonzero((outidx==0)&(inidx==0))[0]);raw=w.view(np.uint8)
raw[9312+2048+slot]=0x38;raw[20592+slot]=0x38
w.tofile(a.folder/'uniform-control.weights');x=np.ones((8,8,4),'<f4');x[:,:,:3]=0.5;x.tofile(a.folder/'input.rgba32f')
subprocess.run(['/tmp/preblock-branch-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',str(a.folder/'uniform-control.weights'),str(a.folder/'input.rgba32f'),str(a.folder/'main.fp8'),str(a.folder/'ds.fp8'),'0','0'],check=True)
skip=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);perm=np.argmax(np.abs(skip),axis=0)
output=e4m3fn(np.fromfile(a.folder/'main.fp8',np.uint8)[perm]).reshape(8,8,32)
print(json.dumps({'active_channels':np.flatnonzero(np.any(output!=0,axis=(0,1))).tolist(),'channel0_values':np.unique(output[:,:,0]).tolist(),'other_absmax':float(np.abs(output[:,:,1:]).max())}))
assert np.all(output[:,:,0]==0.5) and np.all(output[:,:,1:]==0)
if a.matrices:
 recovered={}
 for name,mode in [('v','v-byte-scan'),('projection','projection-byte-scan')]:
  inputs_index=np.zeros(1024,np.int32);outputs_index=None
  for bit in [-1,0,1,2,3,4]:
   v=np.zeros(10848,'<f2');v[4616:4648]=1;v.view('<f4')[10288//2]=1
   selected=np.ones(512,bool) if bit<0 else (ch&(1<<bit))!=0
   v[4104+s[(feature==5)&selected]]=0.5
   diagonal=np.flatnonzero(outidx==inidx) if name=='v' else np.flatnonzero(recovered['v_input']==recovered['v_output'])
   v.view(np.uint8)[(20592 if name=='v' else 11360)+diagonal]=0x38
   path=a.folder/f'{name}-{bit}.weights';v.tofile(path)
   main=a.folder/f'{name}-{bit}-main.fp8';ds=a.folder/f'{name}-{bit}-ds.fp8'
   subprocess.run(['/tmp/preblock-branch-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',str(path),str(a.folder/'input.rgba32f'),str(main),str(ds),'0','0',mode],check=True,capture_output=True)
   y=np.fromfile(main,np.uint8).reshape(1024,2048)[:,perm].reshape(1024,64,32)
   active=np.any(y!=0,axis=(1,2))
   if bit<0:
    present=np.any(y!=0,axis=1);assert np.all(present.sum(1)==1);outputs_index=np.argmax(present,axis=1)
   else:inputs_index|=active.astype(np.int32)<<bit
  assert np.unique(outputs_index*32+inputs_index).size==1024
  recovered[name+'_input']=inputs_index;recovered[name+'_output']=outputs_index
  print(name,'all 1024 connections recovered as a bijection',flush=True)
 v=np.zeros(10848,'<f2');v[4616:4648]=1;v.view('<f4')[10288//2]=1;v[4104+s[feature==5]]=0.5
 path=a.folder/'skip-control.weights';v.tofile(path)
 main=a.folder/'skip-main.fp8';ds=a.folder/'skip-ds.fp8'
 subprocess.run(['/tmp/preblock-branch-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',str(path),str(a.folder/'input.rgba32f'),str(main),str(ds),'0','0','attention-skip-scan'],check=True,capture_output=True)
 y=np.fromfile(main,np.uint8).reshape(32,2048)[:,perm].reshape(32,64,32);present=np.any(y!=0,axis=1);assert np.all(present.sum(1)==1)
 recovered['skip_channel']=np.argmax(present,axis=1);assert np.unique(recovered['skip_channel']).size==32
 np.savez(a.folder/'matrix-layout.npz',**recovered)
if a.bias:
 v=w.copy();v[4104:4616]=0;v[4104+s[(ch==0)&(feature==8)]]=1
 v.tofile(a.folder/'bias-control.weights')
 query_index=None;key_index=np.zeros(4096,np.int32)
 for bit in range(6):
  mask=((np.arange(64).reshape(8,8)&(1<<bit))!=0).astype(np.float32)
  rgb=np.ones((8,8,4),'<f4');rgb[:,:,:3]=0.5;rgb[:,:,0]=(mask+1)*.5
  path=a.folder/f'bias-{bit}.rgba32f';rgb.tofile(path)
  main=a.folder/f'bias-{bit}-main.fp8';ds=a.folder/f'bias-{bit}-ds.fp8'
  subprocess.run(['/tmp/preblock-branch-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',str(a.folder/'bias-control.weights'),str(path),str(main),str(ds),'0','0','bias-scan'],check=True,capture_output=True)
  y=e4m3fn(np.fromfile(main,np.uint8).reshape(4096,2048)[:,perm]).reshape(4096,64,32)
  assert np.all(y[:,:,1:]==0)
  difference=y[:,:,0]-.5
  assert np.all((np.abs(difference)>.1).sum(1)==1)
  q=np.argmax(np.abs(difference),axis=1)
  if query_index is None:query_index=q
  else:np.testing.assert_array_equal(query_index,q)
  key_index|=(difference[np.arange(4096),q]>0).astype(np.int32)<<bit
  print('bias bit',bit,'query pixels',len(np.unique(q)),flush=True)
 assert np.unique(query_index*64+key_index).size==4096
 original=np.fromfile('/tmp/block0.weights','<f2').astype(np.float32)
 bias=np.empty((64,64),np.float32);bias[query_index,key_index]=original[6192:10288]
 np.savez(a.folder/'bias-layout.npz',query=query_index,key=key_index,bias=bias)
 print(json.dumps({'bias_connections':4096,'bijection':True,'method':'six spatial input bits and one FP16 bias coefficient at a time'}))
