"""Candidate matrix/address tests for encoder final head; no fitted correction."""
from pathlib import Path
import json,argparse,subprocess
import numpy as np
from native_split_reference import bits
from native_c64_reference import multiply
from native_c32_reference import F
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize
parser=argparse.ArgumentParser();parser.add_argument('--random-input',action='store_true');args=parser.parse_args()
root=Path('release/native-rgb256')
inverse=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
width,height=4,4;input_path=root/'block30-pool.fp8'
if args.random_input:
 width,height=16,8;root=Path('release/native-c512/head-check');root.mkdir(exist_ok=True)
 values=F(np.random.default_rng(1801).normal(0,.25,(height,width,512)).astype(np.float32))
 cells=quantize(values).reshape(height//4,4,width//4,4,512).transpose(0,2,1,3,4).reshape(-1,8192)
 encoded=np.empty_like(cells);encoded[:,inverse]=cells;input_path=root/'input.fp8';encoded.tofile(input_path)
count=width*height*512
source=np.fromfile(input_path,np.uint8);assert not np.any(source[count:])
x=e4m3fn(source[:count].reshape(-1,8192)[:,inverse]).reshape(height//4,width//4,4,4,512).transpose(0,2,1,3,4).reshape(height,width,512)
raw=np.fromfile('release/native-c512/block30-4.weights',np.uint8);assert raw.size==524304
subprocess.run(['/tmp/native-split-head-oracle',str(input_path),str(root/'block30-head.fp8'),'release/native-c512/block30-4.weights',str(width),str(height),str(4*((width+7)//8)),str((height+7)//8),'1','8'],check=True)
target_raw=np.fromfile(root/'block30-head.fp8',np.uint8);assert not np.any(target_raw[2*count:]) and not np.any((target_raw[:2*count]&127)==127)
target=e4m3fn(target_raw[:2*count]);weights=e4m3fn(raw[:524288])
maps=[('expanded-output-bits',[3,6,7,8,9,10,11,12,13,14],[1,0,4,5,2,15,16,17,18]),('appended-output-bank',[3,6,7,8,9,10,11,12,13,18],[1,0,4,5,2,14,15,16,17])]
for name,ob,ib in maps:
 matrix=np.empty((1024,512),np.float32);oi,ii=bits(524288,ob),bits(524288,ib)
 assert np.unique(oi*512+ii).size==524288
 matrix[oi,ii]=weights;predicted=F(multiply(x,matrix))
 # Output-layout test: two concatenated C512 cell banks, independently decoded.
 decoded=target.reshape(-1,2,8192)[:,:,inverse].reshape(-1,2,4,4,512).transpose(0,2,3,1,4).reshape(height//4,width//4,4,4,1024).transpose(0,2,1,3,4).reshape(height,width,1024)
 print(json.dumps({'candidate':name,'sorted_different':int(np.count_nonzero(np.sort(predicted.ravel())!=np.sort(target))),'cell_banks_different':int(np.count_nonzero(predicted!=decoded)),'max_error':float(np.max(np.abs(predicted-decoded)))}),flush=True)
 if name=='expanded-output-bits':
  assert np.array_equal(predicted,decoded),'head matrix or output view differs'
  matrix.astype('<f4').tofile(root/'head-matrix.f32')
