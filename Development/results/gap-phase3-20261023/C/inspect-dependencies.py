from pathlib import Path
import re,json,hashlib
out=Path(__file__).resolve().parent
root=Path('/home/lmxxf/work/dlss5-aco-isa-20261023')
result=[]
for file,name in [('c512-m32-mh.s','c512_qkv_attention_compact_w16'),('c64-wave2-fast.s','c128_wave2_bi_bo'),('c64-wave2-fast.s','c256_wave2_bi_bo_w16')]:
 all=(root/file).read_text().splitlines();a=next(i for i,l in enumerate(all)if '<'+name+'>:'in l);b=next((i for i in range(a+1,len(all))if re.match(r'^[0-9a-f]+ <',all[i])),len(all));ls=all[a:b]
 zeros=[]
 for i,l in enumerate(ls):
  if re.search(r's_wait_loadcnt(?:_dscnt)? 0x0\b',l):
   last=next((q for q in reversed(ls[:i])if re.search(r'\b(?:global|buffer)_load_',q)),None)
   zeros.append({'wait':l.strip(),'last_vmem_load':last.strip()if last else None,'next_instructions':[v.strip()for v in ls[i+1:i+5]]})
 result.append({'module_disassembly':str(root/file),'disassembly_sha256':hashlib.sha256((root/file).read_bytes()).hexdigest(),'kernel':name,'zero_waits':zeros,'note':'Lexical predecessor and first-use evidence, manually audited; not a full CFG hazard verifier or dynamic stall counter.'})
(out/'dependency-windows.json').write_text(json.dumps(result,indent=2)+'\n')
print([(r['kernel'],len(r['zero_waits']))for r in result])
