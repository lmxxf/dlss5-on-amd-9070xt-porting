"""Compare real C256 half-window input with explicitly zero-padded 8x8 input."""
from pathlib import Path
import json,os,subprocess
import numpy as np
root=Path('release/native-c256');source=Path('release/native-c128/block14-aux.fp8')
original=np.fromfile(source,np.uint8);assert not np.any(original[8192:])
original=original[:8192].reshape(16,4,8,16)
np.pad(original,((0,0),(0,4),(0,0),(0,0))).tofile(root/'padded-input.fp8')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
for height,input_path,output in [(4,source,root/'block15-output.fp8'),(8,root/'padded-input.fp8',root/'padded-output.fp8')]:
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-03.cubin',str(root/'block15.weights'),str(input_path),str(output),str(root/'boundary-aux.fp8'),'cc_tinlayout_fused_swin_8h_256_8_inpview_fp8','8',str(height),'1','1','8','7','0'],env=env,check=True,capture_output=True)
 raw=np.fromfile(output,np.uint8);assert not np.any(raw[height*8*256:]) and not np.any((raw[:height*8*256]&127)==127)
small=np.fromfile(root/'block15-output.fp8',np.uint8)[:8192]
# Recovered output layout is row-major 4x4 cells: the first two cells
# are precisely the valid top half of an 8x8 window.
padded=np.fromfile(root/'padded-output.fp8',np.uint8)[:8192]
print(json.dumps({'real_input_extent':[8,4,256],'padded_extent':[8,8,256],'cropped_values':8192,'exact_fraction':float(np.mean(small==padded))},indent=2))
assert np.array_equal(small,padded), 'C256 boundary is not equivalent to zero padding'
from encode_tinlayout_global import quantize
for seed,scale in [(211,.25),(227,3.)]:
 values=quantize(np.random.default_rng(seed).normal(0,scale,(16,4,8,16)).astype(np.float32))
 values.tofile(root/'boundary-random-input.fp8')
 np.pad(values,((0,0),(0,4),(0,0),(0,0))).tofile(root/'boundary-random-padded.fp8')
 outputs=[]
 for height,name in [(4,'input'),(8,'padded')]:
  output=root/f'boundary-random-{height}-output.fp8'
  subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-03.cubin',str(root/'block15.weights'),str(root/f'boundary-random-{name}.fp8'),str(output),str(root/'boundary-aux.fp8'),'cc_tinlayout_fused_swin_8h_256_8_inpview_fp8','8',str(height),'1','1','8','7','0'],env=env,check=True,capture_output=True)
  raw=np.fromfile(output,np.uint8);assert not np.any(raw[height*8*256:]) and not np.any((raw[:height*8*256]&127)==127)
  outputs.append(raw[:8192])
 assert np.array_equal(*outputs), 'random C256 boundary equivalence failed'
 print(json.dumps({'seed':seed,'scale':scale,'cropped_values':8192,'exact_fraction':1.0}))
