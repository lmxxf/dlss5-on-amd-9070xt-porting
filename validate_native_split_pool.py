"""Validate block30 projection/pool with original coefficients, non-square input."""
from pathlib import Path
import os,subprocess,json,argparse
import numpy as np
from native_split_weights import unpack
from native_split_reference import ffwd,attention_window
from native_c64_reference import multiply
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser();parser.add_argument('--rgb-small',action='store_true');args=parser.parse_args()
root=Path('release/native-c512');folder=root/('pool-rgb-small' if args.rgb_small else 'pool-real');folder.mkdir(exist_ok=True)
width,height=(4,4) if args.rgb_small else (16,8)
inverse=np.argsort(np.load(root/'split-view/mapping.npz')['cell_output_to_hwc'])
def decode(path,width,height):
 physical_w,physical_h=max(4,width),max(4,height)
 n=physical_w*physical_h*512;raw=np.fromfile(path,np.uint8)
 assert not np.any(raw[n:]) and not np.any((raw[:n]&127)==127)
 result=e4m3fn(raw[:n].reshape(-1,8192)[:,inverse]).reshape(physical_h//4,physical_w//4,4,4,512).transpose(0,2,1,3,4).reshape(physical_h,physical_w,512)
 assert not np.any(result[height:]) and not np.any(result[:height,width:]),'nonzero assumed padding'
 return result[:height,:width]
for i in range(4):
 subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',f'block30.layer{i}.layer',str(root/f'block30-{i}.weights')],check=True,capture_output=True)
source=Path('release/native-rgb128/block29-main.fp8') if args.rgb_small else root/'plain-continuation/block29-main.fp8';x=decode(source,width,height)
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_SPLIT_')}
out=folder/'body.fp8'
subprocess.run(['/tmp/native-split-global-oracle',str(source),str(out),*[str(root/f'block30-{i}.weights') for i in range(4)],str(width),str(height),'2','native-plain'],env=env,check=True,capture_output=True)
fw,fp,qkv,bias,scales,projection=unpack(root,30)
branch=ffwd(x,fw);feature=F(multiply(branch,fp['matrix'],H(x*fp['skip'])))
attended=attention_window(feature,qkv,bias,scales,2)
raw=multiply(attended,projection['matrix'],H(feature*projection['skip']))
def compare(stage,got,want):
 err=np.abs(got-want);print(json.dumps({'stage':stage,'values':got.size,'different':int(np.count_nonzero(err)),'max_error':float(err.max())}),flush=True)
 assert np.array_equal(got,want),stage+' differs'
for suffix,value in [('.branch',branch),('.ffn',feature),('.attn',attended),('',F(raw))]:compare('body'+suffix,value,decode(str(out)+suffix,width,height))
subprocess.run(['/tmp/native-split-pool-oracle',str(out)+'.attn',str(out)+'.ffn',str(root/'block30-3.weights'),str(folder/'main.fp8'),str(folder/'pool.fp8'),str(width),str(height)],check=True)
compare('pool-kernel-main',F(raw),decode(folder/'main.fp8',width,height))
top=H(raw[::2,::2]+raw[::2,1::2]);bottom=H(raw[1::2,::2]+raw[1::2,1::2]);pooled=F(H(H(top+bottom)*.25))
compare('pool-kernel-down',pooled,decode(folder/'pool.fp8',width//2,height//2))
