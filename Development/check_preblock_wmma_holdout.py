"""Fresh half-valued inputs and weights test the measured accumulation rule."""
from pathlib import Path
import subprocess,json
import numpy as np
from native_preblock_mix_reference import tensor_mix
from native_c32_reference import H
root=Path('release/native-rgb512/wmma-holdout');root.mkdir(exist_ok=True)
rng=np.random.default_rng(2213)
x=H(rng.normal(0,1,(65536,16))*np.exp2(rng.integers(-12,5,(65536,16))))
w=H(rng.normal(0,1,(32,16))*np.exp2(rng.integers(-8,3,(32,16))))
x.astype('<f2').tofile(root/'input.f16');w.astype('<f2').tofile(root/'weights.f16')
subprocess.run(['/tmp/probe-preblock-wmma',str(root/'input.f16'),str(root/'weights.f16'),str(root/'output.f16'),str(len(x))],check=True)
target=np.fromfile(root/'output.f16','<f2').astype(np.float32).reshape(-1,32);got=tensor_mix(x,w)
error=np.abs(got-target);print(json.dumps({'values':target.size,'different':int(np.count_nonzero(got!=target)),'max_error':float(error.max())}))
assert np.array_equal(got,target),'fresh HMMA arithmetic holdout differs'
