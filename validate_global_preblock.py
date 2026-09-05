"""Prepare nonsquare global test and compare independently decoded original views."""
import argparse,json
from pathlib import Path
import numpy as np
from decode_tinlayout_global import e4m3fn
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('--compare',action='store_true');a=p.parse_args();a.folder.mkdir(parents=True,exist_ok=True)
width,height=128,64
if not a.compare:
 tiles=np.fromfile('release/preblock-branch-audit/input.rgba32f','<f4').reshape(256,8,8,4)[:128]
 tiles.tofile(a.folder/'input-tiles.rgba32f')
 tiles.reshape(height//8,width//8,8,8,4).transpose(0,2,1,3,4).copy().tofile(a.folder/'input-hwc.rgba32f')
else:
 raw=np.fromfile(a.folder/'main.fp8',np.uint8)
 assert raw.size==height*width*32 and not np.any((raw&127)==127)
 sm=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);perm=np.argmax(np.abs(sm),axis=0)
 target=np.empty((height,width,32),np.float32);cols=width//8
 for ty in range(height//8):
  for tx in range(cols):
   base=ty*cols*2048+tx*1024;record=np.concatenate([raw[base:base+1024],raw[base+cols*1024:base+cols*1024+1024]])
   target[ty*8:ty*8+8,tx*8:tx*8+8]=e4m3fn(record[perm]).reshape(8,8,32)
 ds_raw=np.fromfile(a.folder/'ds.fp8',np.uint8)
 assert ds_raw.size==height*width*8 and not np.any((ds_raw&127)==127)
 ds=e4m3fn(ds_raw).reshape(2,height//2,width//2,16).transpose(1,2,0,3).reshape(height//2,width//2,32)
 main=np.fromfile(a.folder/'amd-main.f32','<f4').reshape(target.shape);down=np.fromfile(a.folder/'amd-down.f32','<f4').reshape(ds.shape)
 def report(x,y):
  assert np.isfinite(x).all() and np.isfinite(y).all() and np.any(y)
  return {'correlation':float(np.corrcoef(x.ravel(),y.ravel())[0,1]),'exact_fraction':float(np.mean(x==y)),'mae':float(np.abs(x-y).mean()),'max_error':float(np.abs(x-y).max())}
 print(json.dumps({'width':width,'height':height,'main':report(main,target),'downsample':report(down,ds),'view_decode':'post skip bank gather for main; two channel banks for DS, separately checked by numerical comparison'},indent=2))
