"""Block48 byte-region and input-merge candidates; no fitted corrections."""
from pathlib import Path
import json
import numpy as np
from native_split_reference import bits
from native_c64_reference import unpack,block,multiply
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-upsample48');raw=np.fromfile(root/'block48.weights',np.uint8)
assert raw.size==820784
# Transplant candidate regions into the already decoded ordinary C256 record.
ordinary=np.zeros(689232,np.uint8)
ordinary[:0x58000]=raw[:0x58000]
ordinary[0x58010:0x58210]=raw[0x78000:0x78200]
ordinary[0x58220:]=raw[0x78400:]
ordinary.tofile(root/'ordinary-candidate.weights')
params=unpack(root/'ordinary-candidate.weights')
matrix=np.empty((256,512),np.float32)
matrix[bits(131072,[3,6,7,8,9,10,11,12]),bits(131072,[1,0,4,5,2,13,14,15,16])]=e4m3fn(raw[0x58000:0x78000])
c=np.arange(256);order=(c//16)*16+(c%8)*2+(c%16//8)
scale=np.empty(256,np.float32);scale[order]=raw[0x78200:0x78400].view('<f2')
inverse=np.argsort(np.load('release/native-c256/view/mapping.npz')['cell_output_to_hwc'])
checks=[]
for case in ('main','skip'):
    data=np.fromfile(root/f'{case}-output.fp8',np.uint8)
    target=e4m3fn(data[:65536].reshape(-1,4096)[:,inverse]).reshape(4,4,4,4,256).transpose(0,2,1,3,4).reshape(16,16,256)
    low=multiply(np.full((1,512),.5 if case=='main' else 0,np.float32),matrix)
    merged=H(low+(.5 if case=='skip' else 0)*scale)
    for quantized in (False,True):
        x=np.broadcast_to(F(merged) if quantized else merged,(4,64,256)).copy()
        result=block(x,*params).reshape(2,2,8,8,256).transpose(0,2,1,3,4).reshape(16,16,256)
        err=np.abs(result-target)
        checks.append({'case':case,'quantize_merge':quantized,'different':int(np.count_nonzero(err)),
                       'max_error':float(err.max())})
print(json.dumps({'scope':'constant input candidates only','checks':checks},indent=2))
(root/'candidate-validation.json').write_text(json.dumps({'checks':checks},indent=2)+'\n')
