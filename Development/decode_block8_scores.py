"""Decode bias-traced QMMA score registers before first softmax affine."""
from pathlib import Path
import re,json,subprocess
import numpy as np
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18');dump=json.loads((root/'qk-registers-5080.json').read_text());bias=np.load('release/native-c64/attention-layout/bias-layout.npz')
sass=subprocess.check_output(['/usr/local/cuda/bin/cuobjdump','--dump-sass','--function','cc_tinlayout_fused_swin_2h_64_2_fp8','/tmp/dlssnr-cubins/dlssnr-01.cubin'],text=True);regs={};scores=set()
for line in sass.splitlines():
 m=re.search(r'/\*([0-9a-f]+)\*/\s+(\S+)\s+(.*?)\s*;',line)
 if not m or not 0x4070<=int(m[1],16)<0x5080:continue
 op,args=m[2],m[3];a=[x.strip() for x in args.split(',')]
 if not re.fullmatch(r'R\d+',a[0]):continue
 dst=int(a[0][1:])
 if op.startswith('LDG.E.128') and 'R14.64+' in args:
  off=int(re.search(r'R14.64\+(0x[0-9a-f]+)',args)[1],16)
  for j in range(4):
   slots=(off-0xa0a0)//2+np.arange(32)[:,None]*8+j*2+np.arange(2)[None,:]
   regs[dst+j]=np.stack([bias['query'][slots],bias['key'][slots]],-1);scores.discard(dst+j)
 elif op.startswith('QMMA'):
  src=int(a[3][1:]) if a[3]!='RZ' else -1;copies=[regs.get(src),regs.get(src+1)]
  for j,v in enumerate(copies):
   if v is not None:regs[dst+j]=v.copy();scores.add(dst+j)
   else:regs.pop(dst+j,None);scores.discard(dst+j)
 else:regs.pop(dst,None);scores.discard(dst)
reference=np.load(root/'query-reference.npz')['scores'][0];mismatches=[];count=0;covered=set()
for reg in sorted(scores):
 for row in dump['rows']:
  lane=row['lane'];v=row['registers'][str(reg)];half=np.array([v&65535,v>>16],np.uint16).view(np.float16).astype(np.float32)
  for k in (0,1):
   q,key=map(int,regs[reg][lane,k]);count+=1;covered.add((q,key))
   if half[k]!=reference[q,key]:mismatches.append({'q':q,'key':key,'original':float(half[k]),'reference':float(reference[q,key])})
r={'scope':'direct pre-affine QMMA scores; only registers already computed atPC5080','values':count,'unique_coordinates':len(covered),'different':len(mismatches),'mismatches':mismatches}
(root/'score-register-comparison.json').write_text(json.dumps(r,indent=2)+'\n');print({k:v for k,v in r.items() if k!='mismatches'});print(mismatches[:16])
