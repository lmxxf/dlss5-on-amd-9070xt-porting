from pathlib import Path
import shutil,sys
here=Path(__file__).resolve().parent;root=here.parents[3];out=Path('/tmp/c32-sparse-src')
shutil.copytree('/tmp/vit-adaptive-src',out,dirs_exist_ok=True)
p=out/'Development/HIP/hip_reference_network.h';s=p.read_text();s=s.replace('AppendC32ResidualDiagonals(v);','AppendC32ResidualDiagonals(v);AppendC32Sparse(v);');p.write_text(s)
p=out/'Development/HIP/packed_weights.h';s=p.read_text();at=s.index('// MH attention residual')
s=s[:at]+(here/'weights.inc').read_text()+'\n'+s[at:];p.write_text(s)
p=out/'kernel/c32_fused_ffn_attention.hip';s=(root/'hip/c32_fused_ffn_attention.hip').read_text();a=s.index(' C32_LOOP for(uint chunk=0;chunk<4;chunk++)for(uint ci=0;ci<2;ci++)');b=s.index('\n#endif',a)
s=s[:a]+(here/('contract-lds.inc' if '--lds' in sys.argv else 'contract.inc')).read_text()+s[b:];p.write_text(s)
print(out)
