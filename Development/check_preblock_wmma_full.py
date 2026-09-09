"""Whole-frame HMMA input-mix comparison and unfitted subtotal candidates."""
from pathlib import Path
import json,subprocess,itertools
import numpy as np
from preblock_mix_reference import inputs
from native_c32_reference import H,F
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb512');out=root/'wmma-full';out.mkdir(exist_ok=True)
rgb=np.fromfile(root/'input-hwc.rgba32f','<f4').reshape(512,512,4);noise=np.memmap(root/'noise-trace.f32',dtype='<f4',mode='r',shape=(512,512,16))
features=inputs(rgb[...,:3],seed=0,live=True);features[...,[0,1,4]]=H(noise[...,[14,15,13]]);features=features.reshape(-1,16)
weight=np.fromfile(root/'amd/block0-ffn.f32','<f4')[:512].reshape(32,16)
features.astype('<f2').tofile(out/'input.f16');weight.astype('<f2').tofile(out/'weights.f16')
subprocess.run(['/tmp/probe-preblock-wmma',str(out/'input.f16'),str(out/'weights.f16'),str(out/'output.f16'),str(len(features))],check=True)
wmma=np.fromfile(out/'output.f16','<f2').astype(np.float32).reshape(-1,32)
packed=np.fromfile(root/'stage-audit/mix.fp8',np.uint8);assert packed.size==8388608 and not np.any((packed&127)==127)
pm=np.argmax(np.abs(np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048)),axis=0)
target=e4m3fn(packed.reshape(64,2,64,1024).transpose(0,2,1,3).reshape(-1,2048)[:,pm]).reshape(64,64,8,8,32).transpose(0,2,1,3,4).reshape(-1,32)
different=int(np.count_nonzero(F(wmma)!=target));print(json.dumps({'WMMA_vs_original_mix_FP8':different,'values':target.size}),flush=True);assert different==0
permutation_reports=[]
for name,permutation in [('reverse',np.arange(15,-1,-1)),('shuffle',np.random.default_rng(2203).permutation(16))]:
 features[:,permutation].astype('<f2').tofile(out/'permuted-input.f16');weight[:,permutation].astype('<f2').tofile(out/'permuted-weights.f16')
 subprocess.run(['/tmp/probe-preblock-wmma',str(out/'permuted-input.f16'),str(out/'permuted-weights.f16'),str(out/'permuted-output.f16'),str(len(features))],check=True)
 permuted=np.fromfile(out/'permuted-output.f16','<f2').astype(np.float32).reshape(wmma.shape)
 item={'K_permutation':name,'half_differences':int(np.count_nonzero(permuted!=wmma))};permutation_reports.append(item);print(json.dumps(item),flush=True)
groups={}
for count in [1,2,3]:
 for selected in itertools.combinations(range(4),count):
  ids=np.arange(16);labels=sum(((ids>>b)&1)<<i for i,b in enumerate(selected));groups[str(selected)]=[np.flatnonzero(labels==i) for i in range(1<<count)]
totals={name:0 for name in ['exact','final_float32',*groups]}
alignment={f'{mode}-{precision}':0 for mode in ['trunc','nearest'] for precision in range(24,33)}
for start in range(0,len(features),4096):
 product=features[start:start+4096,None,:].astype(np.float64)*weight[None,:,:].astype(np.float64);expected=wmma[start:start+4096]
 total=product.sum(-1);totals['exact']+=int(np.count_nonzero(H(total)!=expected));totals['final_float32']+=int(np.count_nonzero(H(total.astype(np.float32))!=expected))
 for name,parts in groups.items():
  total=sum(product[...,part].sum(-1).astype(np.float32).astype(np.float64) for part in parts)
  totals[name]+=int(np.count_nonzero(H(total)!=expected))
 exponent=np.frexp(np.max(np.abs(product),axis=-1))[1]
 for precision in range(24,33):
  quantum=np.exp2(exponent-precision)
  scaled=product/quantum[...,None]
  for mode,rounding in [('trunc',np.trunc),('nearest',np.rint)]:
   total=rounding(scaled).sum(-1)*quantum
   alignment[f'{mode}-{precision}']+=int(np.count_nonzero(H(total)!=expected))
print(json.dumps({'half_differences_vs_WMMA':totals}),flush=True)
print(json.dumps({'aligned_product_candidates':alignment}),flush=True)
(out/'validation.json').write_text(json.dumps({'wmma_mix_exact':True,'permutations':permutation_reports,'half_differences_vs_WMMA':totals,'aligned_product_candidates':alignment},indent=2)+'\n')
