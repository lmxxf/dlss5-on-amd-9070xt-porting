"""Compose actual32x20 original repack with logical GPU tensor layouts."""
from pathlib import Path
import json
import numpy as np
from native_split_reference import bits
root=Path('release/native-vit/repack640');n=640*1024
forward=np.fromfile(root/'forward.i32','<u4');backward=np.fromfile(root/'inverse.i32','<u4')
assert np.array_equal(forward[backward],np.arange(n))
inv=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
canonical=np.arange(n,dtype=np.uint32).reshape(5,4,8,4,2,512).transpose(0,2,4,1,3,5).reshape(-1,2,8192)
physical=np.empty_like(canonical);physical[:,:,inv]=canonical
t=bits(n,[2,6,7,8,14,15,16,17,18,19]);c=bits(n,[0,1,3,4,5,9,10,11,12,13])
logical=np.empty((640,1024),np.uint32);logical[t,c]=physical.ravel()[forward]
gather=logical.ravel();assert np.array_equal(np.sort(gather),np.arange(n))
undo=np.argsort(gather).astype('<u4')
# Independently compose the captured inverse path instead of assuming transpose.
vit_physical=t*1024+c
inverse_logical=np.empty(n,np.uint32);inverse_logical[physical.ravel()]=vit_physical[backward]
assert np.array_equal(undo,inverse_logical)
gather.tofile(root/'hwc-to-vit.i32');undo.tofile(root/'vit-to-hwc.i32')
report={'entries':n,'HWC_shape':[20,32,1024],'forward_inverse_composition_exact':True,'identity_entries':int(np.count_nonzero(gather==np.arange(n))),'scope':'composed original mappings; GPU bridge execution pending'}
(root/'bridge.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
for name,mapping in [('forward',gather),('inverse',undo)]:
 out=root/f'gpu-{name}';out.mkdir(exist_ok=True)
 values=np.arange(n,dtype=np.float32)
 values.tofile(out/'input.f32');values[mapping].tofile(out/'oracle.f32');mapping.astype('<u4').tofile(out/'indices.i32')
