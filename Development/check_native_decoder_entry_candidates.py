"""Offline constant-input decoder candidates; no fitting or GPU execution."""
from pathlib import Path
import argparse
import json
import numpy as np
from native_split_reference import bits
from native_c32_reference import H, F
from native_c64_reference import multiply
from decode_tinlayout_global import e4m3fn

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('main_fixture', type=Path)
p.add_argument('skip_fixture', type=Path)
p.add_argument('--channels', type=Path)
a=p.parse_args()
for folder,case in [(a.main_fixture,'main'),(a.skip_fixture,'skip')]:
    report=json.loads((folder/'validation.json').read_text())
    assert report['status']=='smoke_pass' and report['case']==case
    assert report['width']==report['height']==8
raw=np.fromfile(a.main_fixture/'weights.bin',np.uint8)
assert raw.size==525312
assert (a.main_fixture/'weights.bin').read_bytes()==(a.skip_fixture/'weights.bin').read_bytes()
inverse=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
def target(folder):
    v=np.fromfile(folder/'result.output.fp8',np.uint8)
    assert not np.any(v[131072:]) and not np.any((v[:131072]&127)==127)
    return e4m3fn(v[:131072].reshape(-1,8192)[:,inverse]).reshape(-1,512)
def compare(name,predicted,actual):
    predicted=np.broadcast_to(predicted,actual.shape)
    return {'candidate':name,'different':int(np.count_nonzero(predicted!=actual)),
            'max_error':float(np.max(np.abs(predicted-actual))),
            'sorted_different':int(np.count_nonzero(np.sort(predicted.ravel())!=np.sort(actual.ravel())))}
tail=raw[524288:].view('<f2').astype(np.float32)
c=np.arange(512);order=(c//16)*16+(c%8)*2+(c%16//8)
skip=np.empty(512,np.float32);skip[order]=tail
results=[compare('split-projection-skip-order',F(H(.5*skip)),target(a.skip_fixture)),
         compare('linear-tail-order',F(H(.5*tail)),target(a.skip_fixture))]
matrix=np.empty((512,1024),np.float32)
matrix[bits(524288,[3,6,7,8,9,10,11,12,13]),
       bits(524288,[1,0,4,5,2,14,15,16,17,18])]=e4m3fn(raw[:524288])
x=np.full((1,1024),.5,np.float32)
parts=[multiply(x[:,i:i+256],matrix[:,i:i+256]) for i in range(0,1024,256)]
combined=parts[0]
for part in parts[1:]:combined=H(combined+part)
results.append(compare('split-matrix-four-partition',F(combined),target(a.main_fixture)))
if a.channels:
    report=json.loads((a.channels/'validation.json').read_text())
    assert report['status']=='smoke_pass' and report['case']=='channels'
    assert (a.channels/'weights.bin').read_bytes()==raw.tobytes()
    x=np.fromfile(a.channels/'main-channels.f32','<f4').reshape(1,1024)
    residual=np.fromfile(a.channels/'skip-channels.f32','<f4')
    parts=[multiply(x[:,i:i+256],matrix[:,i:i+256]) for i in range(0,1024,256)]
    combined=parts[0]
    for part in parts[1:]:combined=H(combined+part)
    results.append(compare('channel-varying-final-half-fma',F(H(combined+residual*skip)),target(a.channels)))
    results.append(compare('channel-varying-rounded-skip',F(H(combined+H(residual*skip))),target(a.channels)))
print(json.dumps({'scope':'spatially constant inputs; includes channel-varying case when supplied','checks':results},indent=2))
