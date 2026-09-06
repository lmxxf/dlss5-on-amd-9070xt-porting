"""Original-CUBIN controlled preblock ablations, with CPU comparisons.

Disables later matrices in private copies; never changes deployed weights.
"""
from pathlib import Path
import os,subprocess,json,argparse
import numpy as np
from preblock_mix_reference import inputs
from preblock_noise_reference import fields
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser();parser.add_argument('--seed',type=lambda s:int(s,0),default=0);args=parser.parse_args()
root=Path('release/native-rgb128');out=root/'stage-audit'/f'seed{args.seed}';out.mkdir(parents=True,exist_ok=True)
original=np.fromfile('/tmp/block0.weights','<f2')
fw=np.fromfile(root/'amd/block0-ffn.f32','<f4')
rgb=np.fromfile(root/'input-hwc.rgba32f','<f4').reshape(128,128,4)
prefix=H(inputs(rgb[...,:3],seed=args.seed,live=True)@fw[:512].reshape(32,16).T)
expanded=H(F(prefix)@fw[512:4608].reshape(128,32).T)
gate=np.clip(expanded,-4,4)
poly=H(gate*H(np.abs(gate)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))
hidden=F(H(expanded*poly));ffn=H(prefix*fw[8704:])
w2=fw[4608:8704].reshape(32,128)
for k in range(0,128,32):ffn=H(ffn+hidden[...,k:k+32]@w2[:,k:k+32].T)
pm=np.argmax(np.abs(np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048)),axis=0)
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_')}
env.update(DLSS5_PREBLOCK_WIDTH='128',DLSS5_PREBLOCK_HEIGHT='128',DLSS5_PREBLOCK_SEED=str(args.seed),DLSS5_PREBLOCK_PARAMETER_FILE=str(Path('release/live-preblock-v2/preblock-live-0.bin').resolve()))
mix_matrix=fw[:512].reshape(32,16).copy();mix_matrix[:,[0,1,4]]=0
without_noise=H(inputs(rgb[...,:3],seed=args.seed,live=True)@mix_matrix.T)
native_features=inputs(rgb[...,:3],seed=args.seed,live=True)
noise=H(fields(128,128,args.seed,native_steps=True))
native_features[...,[0,1,4]]=noise[...,[1,2,0]]
native_prefix=H(native_features@fw[:512].reshape(32,16).T)
cases=[('mix',prefix),('ffn',ffn),('mix_no_noise',without_noise),('mix_native_steps',native_prefix)]
trace=root/f'noise-residual/trace-seed{args.seed}.f32'
if trace.exists():
 native_features[...,[0,1,4]]=H(np.fromfile(trace,'<f4').reshape(128,128,16)[...,[14,15,13]])
 cuda_prefix=H(native_features@fw[:512].reshape(32,16).T)
 cases.append(('mix_cuda_trace',cuda_prefix))
 ex=H(F(cuda_prefix)@fw[512:4608].reshape(128,32).T);gt=np.clip(ex,-4,4)
 hd=F(H(ex*H(gt*H(np.abs(gt)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))))
 ff=H(cuda_prefix*fw[8704:])
 for k in range(0,128,32):ff=H(ff+hd[...,k:k+32]@w2[:,k:k+32].T)
 cases.append(('ffn_cuda_trace',ff))
 # HFMA2 rounds once directly to half; float32 then half can double-round.
 inner=H(np.abs(gt).astype(np.float64)*(-.055908203125)+.447265625)
 exact_poly=H(gt.astype(np.float64)*inner.astype(np.float64)+.89453125)
 exact_hidden=F(H(ex*exact_poly));exact_ff=H(cuda_prefix*fw[8704:])
 for k in range(0,128,32):exact_ff=H(exact_ff+exact_hidden[...,k:k+32]@w2[:,k:k+32].T)
 cases.append(('ffn_cuda_half_fma',exact_ff))
 prefix64=H(native_features.astype(np.float64)@fw[:512].reshape(32,16).astype(np.float64).T)
 ex64=H(F(prefix64)@fw[512:4608].reshape(128,32).T);gt64=np.clip(ex64,-4,4)
 hd64=F(H(ex64*H(gt64*H(np.abs(gt64)*np.float32(-.055908203125)+np.float32(.447265625))+np.float32(.89453125))))
 ff64=H(prefix64*fw[8704:])
 for k in range(0,128,32):ff64=H(ff64+hd64[...,k:k+32]@w2[:,k:k+32].T)
 cases.append(('ffn_cuda_prefix64',ff64))
 print(json.dumps({'prefix_half_changed_by_float64':int(np.count_nonzero(prefix64!=cuda_prefix))}),flush=True)
 accum=H(cuda_prefix*fw[8704:]);cases.append(('ffn_skip_only',accum.copy()))
 for group in range(4):
  k=group*32;accum=H(accum+hd[...,k:k+32]@w2[:,k:k+32].T)
  cases.append((f'ffn_through_group{group}',accum.copy()))
for name,predicted in cases:
 w=original.copy();w[4656:6192]=0;w[10296:10808]=0;w[10808:10840]=1;w.view('<f4')[10288//2]=1
 if name.startswith('mix'):w[:4096]=0;w[4616:4648]=1
 if name=='mix_no_noise':
  slots=np.arange(512);features=(slots//8%4)*4+slots%4
  w[4104+slots[np.isin(features,[0,1,4])]]=0
 if name=='ffn_skip_only':w[:4096]=0
 if name.startswith('ffn_through_group'):
  group=int(name[-1]);layout=np.load('release/preblock-ffn-byte-layout/layout.npz')
  w.view(np.uint8)[4096+np.flatnonzero(layout['w2_hidden']>=32*(group+1))]=0
 path=out/f'{name}.weights';w.tofile(path)
 main=out/f'{name}-main.fp8';ds=out/f'{name}-ds.fp8'
 subprocess.run(['/tmp/preblock-branch-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',str(path),str(root/'input-hwc.rgba32f'),str(main),str(ds),'0','0'],env=env,check=True,capture_output=True)
 raw=np.fromfile(main,np.uint8);assert raw.size==128*128*32
 target=np.empty((128,128,32),np.float32)
 for ty in range(16):
  for tx in range(16):
   base=ty*16*2048+tx*1024
   record=np.concatenate([raw[base:base+1024],raw[base+16*1024:base+17*1024]])
   target[ty*8:ty*8+8,tx*8:tx*8+8]=e4m3fn(record[pm]).reshape(8,8,32)
 got=F(predicted);err=np.abs(got-target);where=np.argwhere(err!=0)
 print(json.dumps({'stage':name,'different':len(where),'values':got.size,'max_error':float(err.max()),'first_mismatches':[{'y':int(y),'x':int(x),'c':int(c),'cpu':float(got[y,x,c]),'original':float(target[y,x,c])} for y,x,c in where[:16]]}),flush=True)
