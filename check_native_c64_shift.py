"""Verify original block6/7 shifted outputs with native C64 arithmetic."""
from pathlib import Path
import json,subprocess,argparse,os
import numpy as np
from native_c64_reference import unpack,block
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser();parser.add_argument('--channels',type=int,choices=(64,128,256),default=64);args=parser.parse_args()
C=args.channels;heads=C//32;width,height={64:(32,16),128:(16,8),256:(8,4)}[C];count=width*height*C
cubin=f'/tmp/dlssnr-cubins/dlssnr-{heads.bit_length()-1:02d}.cubin'
root=Path(f'release/native-c{C}')
environment={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
inverse=np.argsort(np.load(root/'view/mapping.npz')['cell_output_to_hwc'])
def decode(path):
 raw=np.fromfile(path,np.uint8);assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
 return e4m3fn(raw[:count].reshape(-1,16*C)[:,inverse]).reshape(height//4,width//4,4,4,C).transpose(0,2,1,3,4).reshape(height,width,C)
sequence={64:[(6,3),(7,1)],128:[(10,3),(11,1),(12,2),(13,0)],256:[(16,3),(17,1),(18,2),(19,0),(20,3),(21,1)]}[C]
for index,shift in sequence:
 px=4 if shift&1 else 0;py=4 if shift&2 else 0;ww=(width+px+7)//8*8;hh=(height+py+7)//8*8
 subprocess.run(['/tmp/native-c32-global-oracle',cubin,str(root/f'block{index}.weights'),str(root/f'block{index-1}-output.fp8'),str(root/f'block{index}-output.fp8'),str(root/f'block{index}-aux.fp8'),f'cc_tinlayout_fused_swin_{heads}h_{C}_{heads}_fp8',str(width),str(height),str(ww//8),str(hh//8),str(heads),'7',str(shift)],check=True,capture_output=True,env=environment)
 x=decode(root/f'block{index-1}-output.fp8')
 padded=np.pad(x,((py,hh-height-py),(px,ww-width-px),(0,0)))
 if C==256 and height==4:
  # Original plain C256 H4 reuses its half-window rows; inpview does not.
  padded=np.pad(x[(np.arange(hh)-py)%height],((0,0),(px,ww-width-px),(0,0)))
 tiles=padded.reshape(hh//8,8,ww//8,8,C).transpose(0,2,1,3,4).reshape(-1,64,C)
 y=block(tiles,*unpack(root/f'block{index}.weights')).reshape(hh//8,ww//8,8,8,C).transpose(0,2,1,3,4).reshape(padded.shape)[py:py+height,px:px+width]
 target=decode(root/f'block{index}-output.fp8');error=np.abs(y-target)
 print(json.dumps({'block':index,'shift':shift,'exact_fraction':float(np.mean(y==target)),'mae':float(error.mean()),'max_error':float(error.max())}),flush=True)
 assert np.array_equal(y,target), 'shifted C64 differs from original'
