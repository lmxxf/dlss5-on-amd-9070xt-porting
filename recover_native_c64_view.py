"""Recover every C64 inpview output byte's input coordinate with binary codes."""
from pathlib import Path
import json
import subprocess
import numpy as np
from encode_tinlayout_global import quantize

folder=Path('release/native-c64/view');folder.mkdir(parents=True,exist_ok=True)
weights=np.zeros(61760,np.uint8)
weights.view('<f2')[0x7010//2:0x7090//2]=1
weights.view('<f2')[0xf0b0//2:0xf130//2]=1
weights.view('<f4')[0xe0a0//4:0xe0a0//4+2]=1
weights.tofile(folder/'identity.weights')
count=32*16*64
def run(source):
 source.tofile(folder/'input.fp8')
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(folder/'identity.weights'),str(folder/'input.fp8'),str(folder/'output.fp8'),str(folder/'aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_inpview_fp8','32','16','4','2','2','7','0'],check=True,capture_output=True)
 result=np.fromfile(folder/'output.fp8',np.uint8)
 assert not np.any(result[count:]), 'unexpected output outside extent'
 return result[:count]
indices=np.arange(count,dtype=np.uint32);mapping=np.zeros(count,np.uint32)
for bit in range(15):
 result=run(np.where(indices&(1<<bit),0x38,0).astype(np.uint8))
 assert np.all((result==0)|(result==0x38)), 'identity probe changed values'
 mapping|=(result==0x38).astype(np.uint32)<<bit
assert np.array_equal(np.sort(mapping),indices), 'mapping is not bijective'
for seed in (13,29):
 source=quantize(np.random.default_rng(seed).normal(0,3,count).astype(np.float32))
 result=run(source)
 expected=source[mapping].copy()
 expected[(expected&127)==0]=0;result[(result&127)==0]=0
 assert np.array_equal(result,expected), 'held-out coordinate validation failed'
# Decode the independently established block4 DS banks, then check whether
# the recovered C64 output is a repeating 4x4 cell layout at every position.
pixel=(mapping%(16*32*16))//16;y=pixel//32;x=pixel%32
within=mapping%16
channel=mapping//(16*32*16)*16+(within&12)+((within&1)<<1)+((within&2)>>1)
local=((y%4)*4+x%4)*64+channel
cell_map=local[:1024]
assert np.array_equal(np.sort(cell_map),np.arange(1024))
assert np.all(local.reshape(-1,1024)==cell_map)
cells=np.arange(count)//1024
assert np.all(x//4==cells%8) and np.all(y//4==cells//8)
np.savez(folder/'mapping.npz',output_to_input=mapping,cell_output_to_hwc=cell_map,width=32,height=16,channels=64)
print(json.dumps({'extent':[32,16,64],'coded_probes':15,'unique_coordinates':int(np.unique(mapping).size),'held_out_seeds':[13,29],'held_out_exact':True,'scope':'C64 inpview identity layout, not real block5 arithmetic'},indent=2))
