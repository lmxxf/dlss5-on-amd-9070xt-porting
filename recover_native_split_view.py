"""Recover split-Swin inpview-to-output coordinates with identity weights."""
from pathlib import Path
import json,subprocess
import numpy as np
from encode_tinlayout_global import quantize
root=Path('release/native-c512/split-view');root.mkdir(parents=True,exist_ok=True)
width,height,C=16,8,512;count=width*height*C
for i,size in enumerate((524288,263168,917568,263168)):
 w=np.zeros(size,np.uint8)
 if i in (1,3):w.view('<f2')[262144//2:]=1
 if i==2:w.view('<f4')[917504//4:]=1
 w.tofile(root/f'w{i}.bin')
def run(source):
 source.tofile(root/'input.fp8')
 subprocess.run(['/tmp/native-split-global-oracle',str(root/'input.fp8'),str(root/'output.fp8'),*[str(root/f'w{i}.bin') for i in range(4)],str(width),str(height),'0','native-inpview'],check=True,capture_output=True)
 result=np.fromfile(root/'output.fp8',np.uint8);assert not np.any(result[count:])
 return result[:count]
indices=np.arange(count,dtype=np.uint32);mapping=np.zeros(count,np.uint32)
for bit in range(16):
 result=run(np.where(indices&(1<<bit),0x38,0).astype(np.uint8))
 assert np.all((result==0)|(result==0x38))
 mapping|=(result==0x38).astype(np.uint32)<<bit
assert np.array_equal(np.sort(mapping),indices)
for seed in (511,521):
 source=quantize(np.random.default_rng(seed).normal(0,1,count).astype(np.float32))
 expected=source[mapping].copy();actual=run(source)
 expected[(expected&127)==0]=0;actual[(actual&127)==0]=0
 assert np.array_equal(expected,actual), 'split held-out coordinate mapping failed'
pixel=(mapping%(height*width*16))//16;y=pixel//width;x=pixel%width
within=mapping%16;c=mapping//(height*width*16)*16+(within&12)+((within&1)<<1)+((within&2)>>1)
local=((y%4)*4+x%4)*C+c;cell_map=local[:16*C]
assert np.array_equal(np.sort(cell_map),np.arange(16*C))
assert np.all(local.reshape(-1,16*C)==cell_map)
cells=np.arange(count)//(16*C)
assert np.all(x//4==cells%(width//4)) and np.all(y//4==cells//(width//4))
np.savez(root/'mapping.npz',output_to_input=mapping,cell_output_to_hwc=cell_map,width=width,height=height,channels=C)
print(json.dumps({'extent':[width,height,C],'unique_coordinates':count,'heldout_seeds':[511,521],'heldout_exact':True,'cell_layout':'4x4x512','scope':'identity coordinate contract, not original-weight split arithmetic'}))
