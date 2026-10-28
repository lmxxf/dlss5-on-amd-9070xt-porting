from pathlib import Path
import re,json
P=Path(__file__).parent;A=Path('/home/lmxxf/work/dlss5-aco-isa-20261023');out={}
for name in ['fswin32','fswin64','fswin128','fswin256','attn','vitattn','decups','gemmvnores','gemmvproj']:
 ls=(A/(name+'.s')).read_text().splitlines();r={}
 for tag,pat in [('partial_wait',r'\bs_wait_loadcnt 0x[1-9a-f]'),('normalization_rsqrt',r'\bv_rsq_f32'),('packed_fp8',r'\bv_cvt_pk_fp8'),('paired_valu',r'\bv_dual_'),('address64',r'\bv_add_co_ci_u32')]:
  i=next((i for i,l in enumerate(ls) if re.search(pat,l)),None)
  if i is not None:r[tag]={'line':i+1,'text':'\n'.join(ls[max(0,i-3):i+5])}
 out[name]=r
(P/'aco-excerpts.json').write_text(json.dumps(out,indent=2))
