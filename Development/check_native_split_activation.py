"""Locate activation on the controlled three-coefficient split ffwd path."""
from pathlib import Path
import json,subprocess
import numpy as np
from native_c32_reference import H,F
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c512/activation-probe');root.mkdir(parents=True,exist_ok=True)
np.full(4*1024*1024,0x38,np.uint8).tofile(root/'input.fp8')
for i,size in enumerate((263168,917568,263168),1):
 w=np.zeros(size,np.uint8)
 if i in (1,3):w.view('<f2')[262144//2:]=1
 if i==2:w.view('<f4')[917504//4:]=1
 w.tofile(root/f'w{i}.bin')
def activate(value):
 gate=np.clip(value,-4,4)
 polynomial=H(gate*H(abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))
 return F(H(value*polynomial))
for a,b,c in [(1,1,1),(2,.5,1),(.5,2,1),(1,1,2),(-1,1,1),(1,-1,1),(-1,-1,1),(2,2,.5)]:
 w=np.zeros(524288,np.uint8);w[[0,0x40000,0x60000]]=quantize(np.array([a,b,c],np.float32));w.tofile(root/'w0.bin')
 output=root/'output.fp8'
 subprocess.run(['/tmp/native-split-global-oracle',str(root/'input.fp8'),str(output),*[str(root/f'w{i}.bin') for i in range(4)],'8','8','0','native-inpview'],check=True,capture_output=True)
 raw=np.fromfile(str(output)+'.branch',np.uint8);active=raw[(raw&127)!=0]
 assert active.size==64 and not np.any((active&127)==127)
 values=e4m3fn(active);assert np.all(values==values[0])
 predicted=F(H(activate(H(F(H(np.array([a],np.float32)))*b))*c)).item()
 print(json.dumps({'coefficients':[a,b,c],'original':float(values[0]),'activation_after_second':predicted}),flush=True)
 assert np.all(values==predicted), 'activation-position hypothesis differs from original'
