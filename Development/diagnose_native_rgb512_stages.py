"""Ablate original preblock to locate the RGB512 window mismatch."""
from pathlib import Path
import os,subprocess,json
import numpy as np
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb512');out=root/'stage-audit';out.mkdir(exist_ok=True)
tile=np.load(root/'tile-diagnostic.npz');prefix=tile['prefix'];fw=np.fromfile(root/'amd/block0-ffn.f32','<f4')
expanded=H(F(prefix)@fw[512:4608].reshape(128,32).T);gate=np.clip(expanded,-4,4)
hidden=F(H(expanded*H(gate*H(abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))))
ffn=H(prefix*fw[8704:]);w2=fw[4608:8704].reshape(32,128)
for k in range(0,128,32):ffn=H(ffn+hidden[...,k:k+32]@w2[:,k:k+32].T)
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_')};env.update(DLSS5_PREBLOCK_WIDTH='512',DLSS5_PREBLOCK_HEIGHT='512',DLSS5_PREBLOCK_SEED='0',DLSS5_PREBLOCK_PARAMETER_FILE=str(Path('release/live-preblock-v2/preblock-live-0.bin').resolve()))
original=np.fromfile('/tmp/block0.weights','<f2');pm=np.argmax(np.abs(np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048)),axis=0)
for name,predicted in [('mix',prefix),('ffn',ffn)]:
 w=original.copy();w[4656:6192]=0;w[10296:10808]=0;w[10808:10840]=1;w.view('<f4')[10288//2]=1
 if name=='mix':w[:4096]=0;w[4616:4648]=1
 weight=out/f'{name}.weights';w.tofile(weight);main=out/f'{name}.fp8'
 subprocess.run(['/tmp/preblock-branch-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',str(weight),str(root/'input-hwc.rgba32f'),str(main),str(out/f'{name}-ds.fp8'),'0','0'],check=True,env=env,capture_output=True)
 raw=np.memmap(main,dtype=np.uint8,mode='r');base=47*64*2048+48*1024
 record=np.concatenate([raw[base:base+1024],raw[base+64*1024:base+65*1024]])
 target=e4m3fn(record[pm]).reshape(8,8,32);where=np.argwhere(F(predicted)!=target)
 print(json.dumps({'stage':name,'different':len(where),'first_positions':where[:16].tolist()}),flush=True)
np.savez(out/'cpu-ffn.npz',prefix=prefix,expanded=expanded,hidden=hidden,ffn=ffn)
