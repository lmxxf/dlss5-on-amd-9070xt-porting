"""C32 upsample candidate regions and merge rounding, using original outputs."""
from pathlib import Path
import json
import numpy as np
from native_c32_reference import unpack_bytes,block,H,F
from native_c64_reference import multiply
from native_split_reference import bits
from decode_tinlayout_global import e4m3fn
root=Path('release/native-upsample66');raw=np.fromfile(root/'weights.bin',np.uint8);assert raw.size==22784
ordinary=np.zeros(20672,np.uint8);ordinary[:0x2000]=raw[:0x2000];ordinary[0x2000:0x2060]=raw[0x2800:0x2860];ordinary[0x2060:]=raw[0x28a0:]
params=unpack_bytes(ordinary)
matrix=np.empty((32,64),np.float32)
matrix[bits(2048,[3,6,7,8,9]),bits(2048,[1,0,4,5,2,10])]=e4m3fn(raw[0x2000:0x2800])
maps={'ffn':np.array([0,1,4,5,8,9,12,13,2,3,6,7,10,11,14,15,16,17,20,21,24,25,28,29,18,19,22,23,26,27,30,31]),
      'attention':np.load('release/preblock-attention-layout/matrix-layout.npz')['skip_channel']}
c=np.arange(32);maps['multihead']=(c//16)*16+(c%8)*2+(c%16//8)
basis=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048)
mapping=np.argmax(np.abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
checks=[]
for case in ('main','skip'):
    output=np.fromfile(root/f'smoke/{case}.fp8',np.uint8)
    target=e4m3fn(output[:8192].reshape(-1,512)[:,mapping]).reshape(4,4,4,4,32).transpose(0,2,1,3,4).reshape(16,16,32)
    low=multiply(np.full((1,64),.5 if case=='main' else 0,np.float32),matrix)
    # Convert the multihead accumulator channel convention to C32's convention.
    reordered=low.copy();reordered[:,maps['ffn']]=low[:,maps['multihead']];low=reordered
    for name,order in maps.items():
        scale=np.empty(32,np.float32);scale[order]=raw[0x2860:0x28a0].view('<f2')
        merged=H(low+(.5 if case=='skip' else 0)*scale)
        for quantize in (False,True):
            tiles=np.broadcast_to(F(merged) if quantize else merged,(4,64,32)).copy()
            got=block(tiles,params).reshape(2,2,8,8,32).transpose(0,2,1,3,4).reshape(16,16,32)
            checks.append({'case':case,'scale_order':name,'quantize_merge':quantize,'different':int(np.count_nonzero(got!=target)),'max_error':float(np.max(np.abs(got-target)))})
report={'scope':'constant-input C32 candidates only','checks':checks}
(root/'candidate-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
