"""Locate V coefficient regions with unit weights, not fitted matrices."""
from pathlib import Path
import subprocess,os,json,argparse
import numpy as np
parser=argparse.ArgumentParser();parser.add_argument('--prefix-scales',action='store_true');parser.add_argument('--valid-only',action='store_true');args=parser.parse_args()
root=Path('release/native-vit');env=os.environ.copy();env['DLSS5_VIT_QKV_UNIT_SCAN']='1'
if args.prefix_scales:env['DLSS5_VIT_QKV_PREFIX_SCALE']='1'
if args.valid_only:env['DLSS5_VIT_QKV_SCAN_VALID_ONLY']='1'
stem='qkv'+('-prefix' if args.prefix_scales else '')+('-valid' if args.valid_only else '')
subprocess.run(['/tmp/native-vit-qkv-oracle',str(root/'contract.fp8'),str(root/'block31-qkv.weights'),str(root/stem), '4','4','16'],check=True,env=env)
raw=np.fromfile(root/f'{stem}-unit-scan.fp8',np.uint8).reshape(23,32768)
unit_linear=True
for probe,a in enumerate(raw):
 positions=np.flatnonzero(a);unit_linear &= bool(np.all(a[positions]==0x38))
 offset=0 if not probe else 1<<(probe-1)
 print(json.dumps({'weight_offset':offset,'record_offset':offset+(128 if args.prefix_scales else 0),'third_output_nonzero':len(positions),'nonzero_codes':np.unique(a[positions]).tolist(),'first_output_offsets':positions[:8].tolist()}),flush=True)
assert np.any(raw),'unit scan never reached the third output'
assert unit_linear,'third output does not support the assumed unit V projection contract'
