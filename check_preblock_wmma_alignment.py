"""Test exponent-aligned tensor accumulation against full hardware half output."""
from pathlib import Path
import json
import numpy as np
from native_c32_reference import H
root=Path('release/native-rgb512/wmma-full')
features=np.fromfile(root/'input.f16','<f2').astype(np.float64).reshape(-1,16)
weight=np.fromfile(root/'weights.f16','<f2').astype(np.float64).reshape(32,16)
target=np.fromfile(root/'output.f16','<f2').astype(np.float32).reshape(-1,32)
totals={f'{mode}-{precision}':0 for mode in ['trunc','floor','nearest'] for precision in range(24,31)}
we=np.frexp(np.abs(weight))[1]
for start in range(0,len(features),4096):
 x=features[start:start+4096];product=x[:,None,:]*weight[None,:,:]
 exponent=np.max(np.where(product!=0,np.frexp(np.abs(x))[1][:,None,:]+we[None,:,:],-1000),axis=-1)
 for precision in range(24,31):
  quantum=np.exp2(exponent-precision);scaled=product/quantum[...,None]
  for mode,rounding in [('trunc',np.trunc),('floor',np.floor),('nearest',np.rint)]:
   value=H(rounding(scaled).sum(-1)*quantum)
   totals[f'{mode}-{precision}']+=int(np.count_nonzero(value!=target[start:start+4096]))
print(json.dumps({'operand_exponent_sum_candidates':totals}),flush=True)
(root/'alignment.json').write_text(json.dumps(totals,indent=2)+'\n')
