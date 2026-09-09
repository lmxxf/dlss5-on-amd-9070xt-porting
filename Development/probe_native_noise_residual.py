"""Expose small half-noise errors by cancellation, using original CUBIN.

The input-mix coefficients are diagnostic controls, never replacement weights.
Report compatible half values, not an unjustified inverse of FP8 rounding.
"""
from pathlib import Path
import os,subprocess,json,argparse
import numpy as np
from preblock_noise_reference import fields
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser();parser.add_argument('--size',type=int,choices=[128,512],default=128);parser.add_argument('--gain',type=int,choices=[64,4096],default=64);args=parser.parse_args();size=args.size;gain=args.gain
root=Path(f'release/native-rgb{size}');out=root/'noise-residual';out.mkdir(exist_ok=True)
noise=H(fields(128,128,0,native_steps=True)) if size==128 else H(np.fromfile(root/'noise-trace.f32','<f4').reshape(512,512,16)[...,13:16])
points=[(1,64),(22,72),(67,66),(116,7)] if size==128 else [(382,384)]
w=np.zeros(10848,np.float16);w[4616:4648]=1;w[10808:10840]=1;w.view('<f4')[10288//2]=1
s=np.arange(512);channel=(s//64)*4+(s//32%2)+(s//4%2)*2;feature=(s//8%4)*4+s%4
for point,(y,x) in enumerate(points):
 for g,f in enumerate([4,0,1]):
  c=point*3+g
  w[4104+s[(channel==c)&(feature==f)]]=gain
  w[4104+s[(channel==c)&(feature==5)]]=-noise[y,x,g]*gain
path=out/'probe.weights';w.tofile(path)
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_')}
env.update(DLSS5_PREBLOCK_WIDTH=str(size),DLSS5_PREBLOCK_HEIGHT=str(size),DLSS5_PREBLOCK_SEED='0',DLSS5_PREBLOCK_PARAMETER_FILE=str(Path('release/live-preblock-v2/preblock-live-0.bin').resolve()))
subprocess.run(['/tmp/preblock-branch-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',str(path),str(root/'input-hwc.rgba32f'),str(out/'main.fp8'),str(out/'ds.fp8'),'0','0'],env=env,check=True,capture_output=True)
raw=np.fromfile(out/'main.fp8',np.uint8)
pm=np.argmax(np.abs(np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048)),axis=0).reshape(8,8,32)
for point,(y,x) in enumerate(points):
 cols=size//8;base=(y//8)*cols*2048+(x//8)*1024
 record=np.concatenate([raw[base:base+1024],raw[base+cols*1024:base+(cols+1)*1024]])
 for g in range(3):
  c=point*3+g;delta=float(e4m3fn(record[[pm[y%8,x%8,c]]])[0])
  value=np.float16(noise[y,x,g]);bits=int(value.view(np.uint16))
  candidates=np.arange(max(0,bits-8),min(65536,bits+9),dtype=np.uint16).view(np.float16).astype(np.float32)
  compatible=candidates[F(H(candidates*gain-float(value)*gain))==delta]
  print(json.dumps({'y':y,'x':x,'gaussian':g,'gain':gain,'candidate':float(value),'original_scaled_residual':delta,'compatible_nearby_half':compatible.tolist()}),flush=True)
