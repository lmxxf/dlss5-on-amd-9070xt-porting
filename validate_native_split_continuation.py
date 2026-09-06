"""Continue saved RGB oracle through plain split kernels, checking all stages.

Shift sequence is the port's existing sequence; this is not a fresh live trace.
"""
from pathlib import Path
import os,subprocess,json,argparse
import numpy as np
from native_split_weights import unpack
from native_split_reference import ffwd,attention_window
from native_c64_reference import multiply
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize
parser=argparse.ArgumentParser();parser.add_argument('--random-input',action='store_true');args=parser.parse_args()
root=Path('release/native-c512');folder=Path('release/native-rgb128')
inverse=np.argsort(np.load(root/'split-view/mapping.npz')['cell_output_to_hwc'])
source=folder/'block23-main.fp8'
width,height=4,4
if args.random_input:
 width,height=16,8;folder=root/'plain-continuation';folder.mkdir(exist_ok=True)
 x=F(np.random.default_rng(1703).normal(0,.5,(height,width,512)).astype(np.float32))
 cells=quantize(x).reshape(height//4,4,width//4,4,512).transpose(0,2,1,3,4).reshape(-1,8192)
 packed=np.empty_like(cells);packed[:,inverse]=cells;source=folder/'input.fp8';packed.tofile(source)
count=width*height*512
def decode(path):
 raw=np.fromfile(path,np.uint8)
 assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
 return e4m3fn(raw[:count].reshape(-1,8192)[:,inverse]).reshape(height//4,width//4,4,4,512).transpose(0,2,1,3,4).reshape(height,width,512)
x=decode(source)
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_SPLIT_')}
for block,shift in [(24,3),(25,1),(26,2),(27,0),(28,3),(29,1)]:
 for i in range(4):
  subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',f'block{block}.layer{i}.layer',str(root/f'block{block}-{i}.weights')],check=True,capture_output=True)
 fw,fp,qkv,bias,scales,projection=unpack(root,block)
 output=folder/f'block{block}-main.fp8'
 subprocess.run(['/tmp/native-split-global-oracle',str(source),str(output),*[str(root/f'block{block}-{i}.weights') for i in range(4)],str(width),str(height),str(shift),'native-plain'],env=env,check=True,capture_output=True)
 branch=ffwd(x,fw);feature=F(multiply(branch,fp['matrix'],H(x*fp['skip'])))
 attended=attention_window(feature,qkv,bias,scales,shift)
 result=F(multiply(attended,projection['matrix'],H(feature*projection['skip'])))
 ok=True
 for suffix,predicted in [('.branch',branch),('.ffn',feature),('.attn',attended),('',result)]:
  expected=decode(str(output)+suffix);error=np.abs(predicted-expected);different=int(np.count_nonzero(error));ok &= different==0
  print(json.dumps({'block':block,'shift':shift,'stage':suffix or 'final','different':different,'values':count,'max_error':float(error.max())}),flush=True)
  if different:
   print(json.dumps({'mismatch_by_x':np.count_nonzero(error,axis=(0,2)).tolist(),'mismatch_by_y':np.count_nonzero(error,axis=(1,2)).tolist(),'first_positions':np.argwhere(error)[:8].tolist(),'original_nonzero_by_x':np.count_nonzero(expected,axis=(0,2)).tolist()}),flush=True)
 assert ok,'native plain split differs; do not continue on mismatched input'
 x=decode(output);source=output
