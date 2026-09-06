"""Test the physical-view interpretation without declaring it the live ABI."""
from pathlib import Path
import os,json,subprocess
import numpy as np
from native_split_reference import ffwd,attention_window
from native_c64_reference import multiply
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c512');folder=root/'physical-extent';folder.mkdir(parents=True,exist_ok=True)
source=Path('release/native-c256/block22-aux.fp8')
raw=np.fromfile(source,np.uint8);assert not np.any(raw[8192:])
c=np.arange(512);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
x=e4m3fn(raw[:8192]).reshape(32,4,4,16).transpose(1,2,0,3).reshape(4,4,512)[...,perm]
assert not np.any(x[2:]) and np.any(x[:2])
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_SPLIT_')}
for height in (2,4):
 output=folder/f'height-{height}.fp8'
 result=subprocess.run(['/tmp/native-split-global-oracle',str(source),str(output),*[str(root/f'block23-{i}.weights') for i in range(4)],'4',str(height),'0','native-inpview'],env=env,capture_output=True,text=True)
 print(json.dumps({'declared_height':height,'returncode':result.returncode,'stdout':result.stdout.strip()}),flush=True)
 if height==4:assert result.returncode==0
fw=np.load(root/'ffwd-check/matrices.npz');fp=np.load(root/'projection-check/matrices.npz');a=np.load(root/'full-check/attention-matrices.npz')
branch=ffwd(x,fw);feature=F(multiply(branch,fp['matrix'],H(x*fp['skip'])))
attended=attention_window(feature,[a[k] for k in ('Q','K','V')],a['bias'],a['scales'])
final=F(multiply(attended,a['P'],H(feature*a['skip'])))
inverse=np.argsort(np.load(root/'split-view/mapping.npz')['cell_output_to_hwc'])
for suffix,predicted in [('.branch',branch),('.ffn',feature),('.attn',attended),('',final)]:
 data=np.fromfile(str(folder/'height-4.fp8')+suffix,np.uint8);assert not np.any(data[8192:]) and not np.any((data[:8192]&127)==127)
 target=e4m3fn(data[:8192][inverse]).reshape(4,4,512)
 assert np.array_equal(predicted,target), 'physical-view component reference differs'
print(json.dumps({'physical_extent':[4,4,512],'valid_input_extent':[4,2,512],'four_stages_exact':True,'live_dispatch_extent':'not established'}))
