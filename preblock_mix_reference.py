"""Controlled original input mix (single color texture, CTA0 legacy test params)."""
import argparse,json
import numpy as np
from preblock_noise_reference import fields
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize

def unpack_mix(weight):
 s=np.arange(512);channel=(s//64)*4+(s//32%2)+(s//4%2)*2
 feature=(s//8%4)*4+s%4
 matrix=np.zeros((32,16),np.float32);matrix[channel,feature]=weight[4104:4616]
 return matrix

def inputs(rgb,seed=0x3f800000):
 rgb=rgb.astype(np.float16);rgb=((rgb-np.float16(.5))*np.float16(2)).astype(np.float32)
 g=fields(rgb.shape[1],rgb.shape[0],seed).astype(np.float16).astype(np.float32)
 x=np.zeros((*rgb.shape[:2],16),np.float32)
 x[:,:,0]=g[:,:,1];x[:,:,1]=g[:,:,2];x[:,:,4]=g[:,:,0]
 for target,source in [(2,1),(3,2),(8,0),(9,1),(12,2),(13,0)]:x[:,:,target]=rgb[:,:,source]
 x[:,:,5]=1;x[:,:,7]=1
 return x

if __name__=='__main__':
 from pathlib import Path
 p=argparse.ArgumentParser();p.add_argument('input');p.add_argument('weights');p.add_argument('oracle');p.add_argument('skip_matrix');p.add_argument('--export-lab',type=Path);a=p.parse_args()
 rgb=np.fromfile(a.input,'<f4').reshape(-1,8,8,4)[:,:,:,:3]
 w=np.fromfile(a.weights,'<f2').astype(np.float32)
 raw=np.fromfile(a.oracle,np.uint8).reshape(-1,2048)
 skip=np.fromfile(a.skip_matrix,'<f4').reshape(2048,2048)
 y=e4m3fn(raw[:,np.argmax(np.abs(skip),axis=0)]).reshape(-1,8,8,32)
 predicted=np.stack([inputs(tile)@unpack_mix(w).T for tile in rgb])
 q=e4m3fn(quantize(predicted.astype(np.float16).astype(np.float32)))
 print(json.dumps({'correlation':float(np.corrcoef(q.ravel(),y.ravel())[0,1]),'exact_fraction':float(np.mean(q==y)),'mae':float(np.abs(q-y).mean()),'max_error':float(np.abs(q-y).max())},indent=2))
 assert np.array_equal(q,y), 'Input mix differs from original controlled kernel'
 if a.export_lab:
  a.export_lab.mkdir(parents=True,exist_ok=True)
  unpack_mix(w).astype('<f4').tofile(a.export_lab/'weights.f32')
  y.astype('<f4').tofile(a.export_lab/'oracle.f32')
