"""Compare original preblock main/DS branches. No fitted correction matrices."""
import argparse,json
from pathlib import Path
import numpy as np
from decode_tinlayout_global import e4m3fn,local_maps
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('--analyze',action='store_true');p.add_argument('--skip-matrix',type=Path);a=p.parse_args()
if not a.analyze:
 a.folder.mkdir(parents=True,exist_ok=True)
 rng=np.random.default_rng(29700)
 x=rng.random((256,8,8,4),dtype=np.float32);x[...,3]=1
 x[:32,:,:,:3]=rng.random((32,1,1,3),dtype=np.float32)
 x.astype('<f4').tofile(a.folder/'input.rgba32f')
 print('256 tiles, including 32 constant-color controls')
else:
 raw=np.fromfile(a.folder/'main.fp8',np.uint8).reshape(-1,2048)
 dsraw=np.fromfile(a.folder/'ds.fp8',np.uint8).reshape(-1,512)
 report={'tiles':len(raw),'candidates':[]}
 for name in ['input','output']:
  perm=np.fromfile('tinlayout-2h64-'+name+'-permutation.i32','<i4').reshape(64,64)
  maps=local_maps(perm,32)
  main=np.empty((len(raw),8,8,32),np.float32)
  for y in range(2):
   for x in range(2):main[:,y*4:y*4+4,x*4:x*4+4]=e4m3fn(raw[:,(y*2+x)*512:(y*2+x+1)*512][:,maps[y,x]])
  avg=main.reshape(-1,4,2,4,2,32).mean(axis=(2,4))
  for y in range(2):
   for x in range(2):
    ds=e4m3fn(dsraw[:,maps[y,x]])
    err=avg-ds
    report['candidates'].append({'main_map':name,'ds_phase':[y,x], 'correlation':float(np.corrcoef(avg.ravel(),ds.ravel())[0,1]),'mae':float(np.abs(err).mean()),'max_error':float(np.abs(err).max())})
 if a.skip_matrix:
  from encode_tinlayout_global import quantize
  weight=np.fromfile(a.skip_matrix,'<f4').reshape(2048,2048)
  assert np.all((np.abs(weight)>1e-5).sum(0)==1)
  ix=np.argmax(np.abs(weight),axis=0).reshape(8,8,32)
  assert np.unique(ix).size==2048
  main=e4m3fn(raw[:,ix]);yy,xx,cc=np.indices((4,4,32))
  ds=e4m3fn(dsraw[:,(cc//16)*256+(yy*4+xx)*16+cc%16])
  avg=main.reshape(-1,4,2,4,2,32).mean((2,4));q=e4m3fn(quantize(avg))
  report['oracle_derived_layout']={'correlation':float(np.corrcoef(avg.ravel(),ds.ravel())[0,1]),'mae':float(np.abs(avg-ds).mean()),'quantized_exact_fraction':float(np.mean(q==ds)),'quantized_mae':float(np.abs(q-ds).mean()),'quantized_max_error':float(np.abs(q-ds).max())}
 print(json.dumps(report,indent=2))
