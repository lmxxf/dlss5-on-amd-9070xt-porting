"""Symbolic bias-coordinate propagation through C64 local half sums only."""
import re,json,subprocess
from pathlib import Path
import numpy as np
mapping=np.load('release/native-c64/attention-layout/bias-layout.npz');regs={};empty=[(None,None)]*32
sass=subprocess.check_output(['/usr/local/cuda/bin/cuobjdump','--dump-sass','--function','cc_tinlayout_fused_swin_2h_64_2_fp8','/tmp/dlssnr-cubins/dlssnr-01.cubin'],text=True)
def read(s):
 m=re.match(r'R(\d+)',s)
 return regs.get(int(m[1]),empty) if m else empty
for line in sass.splitlines():
 m=re.search(r'/\*([0-9a-f]+)\*/\s+(\S+)\s+(.*?)\s*;',line)
 if not m or not 0x7510<=int(m[1],16)<=0x8600:continue
 op,args=m[2],m[3];a=[x.strip() for x in args.split(',')]
 if not re.fullmatch(r'R\d+',a[0]):continue
 dest=int(a[0][1:])
 if op.startswith('LDG.E.128') and 'R14.64+' in args:
  offset=int(re.search(r'R14.64\+(0x[0-9a-f]+)',args)[1],16)
  for j in range(4):
   regs[dest+j]=[tuple([int(mapping['query'][(offset-0xa0a0)//2+lane*8+j*2+k]),int(mapping['key'][(offset-0xa0a0)//2+lane*8+j*2+k])] for k in (0,1)) for lane in range(32)]
  continue
 if op.startswith('QMMA'):
  source=a[3];v=read(source);v2=read('R'+str(int(source[1:])+1)) if source!='RZ' else empty;regs[dest],regs[dest+1]=v,v2;continue
 value=empty
 if op in ('HFMA2','HMNMX2') or (op=='LEA' and '0x7ff88000' in args):value=read(a[1])
 elif op=='HADD2':value=[tuple(['+',u,v] if u is not None and v is not None else None for u,v in zip(x,y)) for x,y in zip(read(a[1]),read(a[2]))]
 regs[dest]=value
out={str(r):regs.get(r,empty) for r in (8,11,56,57)}
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18')
(root/'c64-partial-trees.json').write_text(json.dumps({'scope':'local sums, lane base assumption not yet fully traced; no numeric acceptance','registers':out},indent=2)+'\n')
print({r:sum(v is not None for lane in x for v in lane) for r,x in out.items()})
