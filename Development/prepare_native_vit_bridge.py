"""Compose measured byte repacking with logical HWC/ViT layouts, without weights."""
from pathlib import Path
import json,hashlib
import numpy as np
from native_split_reference import bits
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb512');n=65536
repack=np.fromfile(root/'repack-output-to-input.i32','<u4');assert repack.size==n and np.unique(repack).size==n and repack.max()<n
inverse=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
canonical=np.arange(n,dtype=np.uint32).reshape(2,4,2,4,2,512).transpose(0,2,4,1,3,5).reshape(4,2,8192)
physical=np.empty_like(canonical);physical[:,:,inverse]=canonical
t=bits(n,[2,6,7,8,14,15]);c=bits(n,[0,1,3,4,5,9,10,11,12,13])
gather=np.empty((64,1024),np.uint32);gather[t,c]=physical.ravel()[repack]
assert np.unique(gather).size==n and gather.max()<n
head=np.fromfile(root/'block30-head.fp8',np.uint8);assert not np.any(head[n:])
hwc=np.empty(n,np.float32);hwc[physical.ravel()]=e4m3fn(head[:n])
source=np.fromfile(root/'vit-input.fp8',np.uint8);assert source.size==n
target=np.empty((64,1024),np.float32);target[t,c]=e4m3fn(source)
assert np.array_equal(hwc[gather],target),'composed mapping differs from actual original repack'
gather.astype('<u4').tofile(root/'hwc-to-vit.i32');np.argsort(gather.ravel()).astype('<u4').tofile(root/'vit-to-hwc.i32')
sha=hashlib.sha256((root/'vit-input.fp8').read_bytes()).hexdigest()
job=Path('release/native-vit')/f'chain31-38-input-{sha[:12]}'
report=json.loads((job/'validation.json').read_text());assert report['status']=='pass' and report['input_sha256']==sha and len(report['stages'])==56 and all(s['different']==0 for s in report['stages'])
summary={'entries':n,'HWC_extent':[8,8,1024],'tokens':64,'bijective':True,'actual_repack_exact':True,'original_vit_chain':str(job),'original_vit_stage_checks':56,'GPU_bridge_verified':False}
(root/'bridge.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
out=root/'amd-bridge';out.mkdir(exist_ok=True)
hwc.tofile(out/'input.f32');target.tofile(out/'oracle.f32');gather.astype('<u4').tofile(out/'indices.i32')
