"""Verify original block6/7 shifted outputs with native C64 arithmetic."""
from pathlib import Path
import json,subprocess
import numpy as np
from native_c64_reference import unpack,block
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c64')
inverse=np.argsort(np.load(root/'view/mapping.npz')['cell_output_to_hwc'])
def decode(path):
 raw=np.fromfile(path,np.uint8);assert not np.any(raw[32768:]) and not np.any((raw[:32768]&127)==127)
 return e4m3fn(raw[:32768].reshape(32,1024)[:,inverse]).reshape(4,8,4,4,64).transpose(0,2,1,3,4).reshape(16,32,64)
for index,shift in [(6,3),(7,1)]:
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(root/f'block{index}.weights'),str(root/f'block{index-1}-output.fp8'),str(root/f'block{index}-output.fp8'),str(root/f'block{index}-aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_fp8','32','16','5',str(3 if shift&2 else 2),'2','7',str(shift)],check=True,capture_output=True)
 x=decode(root/f'block{index-1}-output.fp8');px=4;py=4 if shift&2 else 0
 padded=np.pad(x,((py,py),(px,px),(0,0)));hh,ww=padded.shape[:2]
 tiles=padded.reshape(hh//8,8,ww//8,8,64).transpose(0,2,1,3,4).reshape(-1,64,64)
 y=block(tiles,*unpack(root/f'block{index}.weights')).reshape(hh//8,ww//8,8,8,64).transpose(0,2,1,3,4).reshape(padded.shape)[py:py+16,px:px+32]
 target=decode(root/f'block{index}-output.fp8');error=np.abs(y-target)
 print(json.dumps({'block':index,'shift':shift,'exact_fraction':float(np.mean(y==target)),'mae':float(error.mean()),'max_error':float(error.max())}),flush=True)
 assert np.array_equal(y,target), 'shifted C64 differs from original'
