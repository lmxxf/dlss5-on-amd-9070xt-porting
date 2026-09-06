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
def select(dst,left,right,bit,invert=False):
 a,b=regs[left],regs[right];regs[dst]=[a[l] if bool(l&bit)!=invert else b[l] for l in range(32)]
select(9,11,8,1);select(8,8,11,1);select(10,57,56,1);select(57,56,57,1)
select(11,9,10,2,True);select(56,8,57,2,True);select(9,10,9,2,True);select(8,57,8,2,True)
for reg,xor in [(11,0),(56,1),(9,2),(8,3)]:
 old=regs[reg];regs[reg]=[old[((l%8)*4+l//8)^xor] for l in range(32)]
select(10,11,56,8,True);select(57,56,11,8,True);select(59,9,8,8,True);select(58,8,9,8,True)
select(60,10,59,16,True);select(61,57,58,16,True);select(10,59,10,16,True);select(57,58,57,16,True)
def add(a,b):return ['+',a,b] if a is not None and b is not None else None
trees=[]
for lane in range(32):
 pair=regs[60][lane]
 for reg in (61,10,57):pair=tuple(add(a,b) for a,b in zip(pair,regs[reg][lane]))
 trees.append(add(*pair))
def leaves(t):
 if t is None:raise ValueError('unknown provenance')
 return leaves(t[1])+leaves(t[2]) if t[0]=='+' else [t]
coverage=[]
for t in trees:
 leaf=leaves(t);queries=sorted(set(x[0] for x in leaf));keys=sorted(x[1] for x in leaf)
 coverage.append({'queries':queries,'all64keys':keys==list(range(64))})
(root/'c64-full-trees.json').write_text(json.dumps({'scope':'candidate symbolic full reduction, validate coordinate base separately','coverage':coverage,'trees':trees},indent=2)+'\n')
print(coverage[:4]);assert all(len(c['queries'])==1 and c['all64keys'] for c in coverage)
from native_c32_reference import H
data=np.load(root/'query-reference.npz');exp=data['exp'][0,18]
def evaluate(t):return H(evaluate(t[1])+evaluate(t[2])) if t[0]=='+' else exp[t[1]]
den=float(evaluate(trees[0]));print({'traced_denominator_query18':den,'previous':float(data['den'][0,18,0])})
