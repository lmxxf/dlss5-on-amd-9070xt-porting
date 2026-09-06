from pathlib import Path
import argparse
import json
import numpy as np
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser(description='Report independently decoded DS checkpoints; fail on any mismatch.')
parser.add_argument('--folder',type=Path,default=Path('release/native-rgb128'))
parser.add_argument('--size',type=int,choices=[128,256],default=128)
args=parser.parse_args()
root=args.folder
reports=[]
for layer,w,h,C in [(0,64,64,32),(4,32,32,64),(8,16,16,128),(14,8,8,256),(22,4,4,512)]:
 w*=args.size//128;h*=args.size//128
 raw=np.fromfile(root/f'block{layer}-ds.fp8',np.uint8);n=w*h*C
 assert not np.any(raw[n:]) and not np.any((raw[:n]&127)==127)
 expected=e4m3fn(raw[:n]).reshape(C//16,h,w,16).transpose(1,2,0,3).reshape(h,w,C)
 if layer:
  c=np.arange(C);perm=(c&~3)|((c&1)<<1)|((c&2)>>1);expected=expected[...,perm]
 actual=np.fromfile(root/f'amd/audit-ds{layer}.f32','<f4').reshape(expected.shape);assert np.isfinite(actual).all()
 error=np.abs(actual-expected)
 positions=np.argwhere(error!=0)
 report={'layer':layer,'values':n,'different':int(len(positions)),'exact_fraction':float(np.mean(actual==expected)),'mae':float(error.mean()),'max_error':float(error.max()),'first_mismatches':[{'y':int(y),'x':int(x),'channel':int(c),'expected':float(expected[y,x,c]),'actual':float(actual[y,x,c])} for y,x,c in positions[:16]]}
 reports.append(report)
 print(json.dumps(report),flush=True)
first=next((r['layer'] for r in reports if r['different']),None)
print(json.dumps({'first_divergent_DS_checkpoint':first,'all_exact':first is None,'scope':'numeric lab checkpoints, not game image acceptance'}))
raise SystemExit(0 if first is None else 1)
