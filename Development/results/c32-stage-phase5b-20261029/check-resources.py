from pathlib import Path
import importlib.util,json,sys,hashlib
p=Path(__file__).parent;folder=Path(sys.argv[1]);tag=sys.argv[2];sp=importlib.util.spec_from_file_location('m','Development/tools/llvm-fork/compare-kernels.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);out={}
for f in sorted(folder.glob('c32-wave1-fast*.hsaco')):
 _,ks,sy=m.elf(f);_,bs,bsy=m.elf(p.parent/'c32-stage-phase5-20261029/payload/HIP'/f.parent.name/f.name);rows={}
 for k,d in ks.items():
  r={x:d.get(x,0)for x in ['.vgpr_count','.sgpr_count','.group_segment_fixed_size','.private_segment_fixed_size','.vgpr_spill_count','.sgpr_spill_count']};r['baseline_vgpr']=bs.get(k,{}).get('.vgpr_count');r['code_changed']=k not in bsy or sy[k][2]!=bsy[k][2];r['production_w16']=k.startswith('c32_wave1_')and k.endswith('_w16');r['code_sha256']=hashlib.sha256(sy[k][2]).hexdigest();assert not r['.private_segment_fixed_size']and not r['.vgpr_spill_count']and not r['.sgpr_spill_count'],(f,k,r)
  if r['production_w16']:assert r['.vgpr_count']<=160,(f,k,r)
  elif r['.vgpr_count']>160:assert k in bs and r['.vgpr_count']<=bs[k]['.vgpr_count'],(f,k,r)
  rows[k]=r
 out[f.name]=rows
(p/(tag+'-resource-gates.json')).write_text(json.dumps({'modules':out,'scope':'All exports private=0 and spill=0. All production W16 exports <=160 VGPR. Existing legacy >160 exports may remain only without any resource regression; explicitly listed, never called by measured default routes.'},indent=2));print(tag,{n:{'active_max':max(d['.vgpr_count']for d in r.values()if d['production_w16']),'legacy_above160':[k for k,d in r.items()if d['.vgpr_count']>160]}for n,r in out.items()})
