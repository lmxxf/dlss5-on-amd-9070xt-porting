"""Locate V coefficient regions with unit weights, not fitted matrices."""
from pathlib import Path
import subprocess,os,json
import numpy as np
root=Path('release/native-vit');env=os.environ.copy();env['DLSS5_VIT_QKV_UNIT_SCAN']='1'
subprocess.run(['/tmp/native-vit-qkv-oracle',str(root/'contract.fp8'),str(root/'block31-qkv.weights'),str(root/'qkv'), '4','4','16'],check=True,env=env)
raw=np.fromfile(root/'qkv-unit-scan.fp8',np.uint8).reshape(23,32768)
unit_linear=True
for probe,a in enumerate(raw):
 positions=np.flatnonzero(a);unit_linear &= bool(np.all(a[positions]==0x38))
 print(json.dumps({'weight_offset':0 if not probe else 1<<(probe-1),'third_output_nonzero':len(positions),'nonzero_codes':np.unique(a[positions]).tolist(),'first_output_offsets':positions[:8].tolist()}),flush=True)
assert unit_linear,'third output does not support the assumed unit V projection contract'
