from pathlib import Path
import subprocess,sys,difflib
here=Path(__file__).resolve().parent;root=here.parents[3]
subprocess.run([sys.executable,str(here/'prepare.py')],check=True)
s=(here/'select.hip').read_text();a=s.index('WAVE void vit_select_prep');b=s.index('template<uint MAXT>',a)
s=s[:a]+(here/'packed_prep.hip').read_text()+'\n'+s[b:]
s=s.replace('const uint S=tokens+16,G=(tokens+31)/32,KT=tokens/2;','const uint G=(tokens+31)/32,KT=tokens/2,PT=(KT+15)&~15u;')
s=s.replace('const unsigned char*ks=(pooled?pool:in)+(pooled?key/2:tokens+key)*1024+rc()*1024+head*32;', 'const unsigned char*ks=pooled?pool+2*tokens*1024+(head*PT+key/2+rc())*32:pool+(head*tokens+key+rc())*32;')
s=s.replace('const unsigned char*vs=(pooled?pool:in)+(pooled?KT+key/2:2*tokens+key)*1024+head*32;', 'uint stride=pooled?PT:tokens;const unsigned char*vs=pooled?pool+(2*tokens+PT)*1024+head*32*PT+key/2:pool+tokens*1024+head*32*tokens+key;')
old='for(uint c=0;c<2;c++){i2 y{};for(uint e=0;e<8;e++){uint b=vs[(gr()*8+e)*1024+c*16+rc()];y[e/4]=int(uint(y[e/4])|(b<<(8*(e%4))));}acc[c]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(xb,y,acc[c]);}'
assert old in s
s=s.replace(old,'for(uint c=0;c<2;c++){i2 y{};__builtin_memcpy(&y,vs+(c*16+rc())*stride+gr()*8,8);acc[c]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(xb,y,acc[c]);}')
out=Path('/tmp/vit-select-src');(out/'kernel/deep_fast.hip').write_text((root/'hip/deep_fast.hip').read_text()+'\n'+s)
p=out/'Development/HIP/hip_reference_network.h';old=p.read_text();new=old.replace('auto pool=New(size_t(n)*256),route=', 'auto pool=New(size_t(2*n+2*((n/2+15)&~15u))*256),route=');assert old!=new;p.write_text(new)
(here/'packed-host.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/select-host.h',tofile='b/select-host.h')))
print('packed selective candidate generated')
