"""Recover C64/C128 inpview output coordinates using binary-coded inputs."""
from pathlib import Path
import json,argparse,os
import subprocess
import numpy as np
from encode_tinlayout_global import quantize

parser=argparse.ArgumentParser();parser.add_argument('--channels',type=int,choices=(64,128),default=64);args=parser.parse_args()
channels=args.channels;heads=channels//32
width,height,size,ffn_skip,attention_skip,scale=(32,16,61760,0x7010,0xf0b0,0xe0a0) if channels==64 else (16,8,197184,0x18010,0x30130,0x2c120)
cubin='/tmp/dlssnr-cubins/dlssnr-01.cubin' if channels==64 else '/tmp/dlssnr-cubins/dlssnr-02.cubin'
folder=Path(f'release/native-c{channels}/view');folder.mkdir(parents=True,exist_ok=True)
weights=np.zeros(size,np.uint8)
weights.view('<f2')[ffn_skip//2:ffn_skip//2+channels]=1
weights.view('<f2')[attention_skip//2:attention_skip//2+channels]=1
weights.view('<f4')[scale//4:scale//4+heads]=1
weights.tofile(folder/'identity.weights')
count=width*height*channels
environment={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_NATIVE_SCAN_')}
def run(source):
 source.tofile(folder/'input.fp8')
 subprocess.run(['/tmp/native-c32-global-oracle',cubin,str(folder/'identity.weights'),str(folder/'input.fp8'),str(folder/'output.fp8'),str(folder/'aux.fp8'),f'cc_tinlayout_fused_swin_{heads}h_{channels}_{heads}_inpview_fp8',str(width),str(height),str(width//8),str(height//8),str(heads),'7','0'],check=True,capture_output=True,env=environment)
 result=np.fromfile(folder/'output.fp8',np.uint8)
 assert not np.any(result[count:]), 'unexpected output outside extent'
 return result[:count]
indices=np.arange(count,dtype=np.uint32);mapping=np.zeros(count,np.uint32)
bits=count.bit_length()-1;assert count==1<<bits
for bit in range(bits):
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
pixel=(mapping%(height*width*16))//16;y=pixel//width;x=pixel%width
within=mapping%16
channel=mapping//(height*width*16)*16+(within&12)+((within&1)<<1)+((within&2)>>1)
local=((y%4)*4+x%4)*channels+channel
cell_size=16*channels;cell_map=local[:cell_size]
assert np.array_equal(np.sort(cell_map),np.arange(cell_size))
assert np.all(local.reshape(-1,cell_size)==cell_map)
cells=np.arange(count)//cell_size
assert np.all(x//4==cells%(width//4)) and np.all(y//4==cells//(width//4))
np.savez(folder/'mapping.npz',output_to_input=mapping,cell_output_to_hwc=cell_map,width=width,height=height,channels=channels)
print(json.dumps({'extent':[width,height,channels],'coded_probes':bits,'unique_coordinates':int(np.unique(mapping).size),'held_out_seeds':[13,29],'held_out_exact':True,'scope':f'C{channels} inpview identity layout, not real-block arithmetic'},indent=2))
