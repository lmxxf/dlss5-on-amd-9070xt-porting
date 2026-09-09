"""Offline post70 body/head candidates; no fitted coefficients or GPU calls."""
from pathlib import Path
import json
import numpy as np
from native_c32_reference import unpack_bytes,block,H,F
from native_split_reference import bits
root=Path('release/native-post70');raw=np.fromfile(root/'smoke/weights.bin',np.uint8)
ordinary=np.zeros(20672,np.uint8);ordinary[:0x2050]=raw[:0x2050];ordinary[0x2060:]=raw[0x20d0:0x5130]
body=unpack_bytes(ordinary)
order=np.array([0,1,4,5,8,9,12,13,2,3,6,7,10,11,14,15,16,17,20,21,24,25,28,29,18,19,22,23,26,27,30,31])
main_scale=np.empty(32,np.float32);main_scale[order]=raw[0x2050:0x2090].view('<f2')
skip_scale=np.empty(32,np.float32);skip_scale[order]=raw[0x2090:0x20d0].view('<f2')
head=np.empty((16,32),np.float32);head[bits(512,[2,5,6,7]),bits(512,[0,1,3,4,8])]=raw[0x5130:].view('<f2')
c=np.arange(32);other=(c//16)*16+(c%8)*2+(c%16//8)
head_variants={'direct':head}
v=np.empty_like(head);v[:,order]=head[:,other];head_variants['C32-from-multihead']=v
v=np.empty_like(head);v[:,other]=head[:,order];head_variants['multihead-from-C32']=v
checks=[]
for case in ('main','skip'):
    target=np.fromfile(root/f'smoke-{case}/output.f32','<f4').reshape(16,16,4)[:,:,:3]
    initial=H(.5*(main_scale if case=='main' else skip_scale))
    for shift in (0,4):
        for quantized in (False,True):
            x=np.broadcast_to(F(initial) if quantized else initial,(16,16,32))
            x=np.pad(x,((shift,shift),(shift,shift),(0,0)));n=x.shape[0]
            tiles=x.reshape(n//8,8,n//8,8,32).transpose(0,2,1,3,4).reshape(-1,64,32)
            for raw_body in (False,True):
                features=block(tiles,body,raw_output=raw_body).reshape(n//8,n//8,8,8,32).transpose(0,2,1,3,4).reshape(n,n,32)[shift:shift+16,shift:shift+16]
                for name,weights in head_variants.items():
                    y=H(H(features[:,:,:16]@weights[[0,2,4],:16].T)+features[:,:,16:]@weights[[0,2,4],16:].T)
                    got=np.clip(np.float32(.25)+np.float32(.25)*y,0,1)
                    err=np.abs(got-target)
                    checks.append({'case':case,'shift':shift,'quantize_merge':quantized,'raw_body':raw_body,'head_layout':name,
                                   'different':int(np.count_nonzero(err)),'max_error':float(err.max())})
report={'scope':'post70 constant-input hypotheses only','best':sorted(checks,key=lambda x:x['max_error'])[:8],'checks':checks}
(root/'candidates.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'best':report['best']},indent=2))
