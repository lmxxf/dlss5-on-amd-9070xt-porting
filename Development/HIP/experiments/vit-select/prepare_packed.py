from pathlib import Path
import subprocess,sys,difflib
here=Path(__file__).resolve().parent;root=here.parents[3]
subprocess.run([sys.executable,str(here/'prepare.py')],check=True)
s=(here/'select.hip').read_text();a=s.index('WAVE void vit_select_prep');b=s.index('template<uint MAXT>',a)
prep=(here/'packed_prep.hip').read_text()
if '--fragments' in sys.argv:
 prep=prep.replace('sk[32*36],sv[32*36],spv[16*36]','sk[32*36],sv[32*36],spk[16*36],spv[16*36]')
 prep=prep.replace('pool[(h*tokens+first+j)*32+c]=k;','')
 prep=prep.replace('for(uint j=0;j<16&&first+2*j+1<tokens;j++){','for(uint j=0;j<16;j++){if(first+2*j+1>=tokens){spk[j*36+c]=0;spv[j*36+c]=0;continue;}')
 prep=prep.replace('pool[2*tokens*1024+(h*PT+first/2+j)*32+c]=','spk[j*36+c]=')
 start=prep.index(' for(uint r=0;r<32;r++){')
 prep=prep[:start]+(here/'fragment_stores.hip').read_text()+'\n}\n'
s=s[:a]+prep+'\n'+s[b:]
s=s.replace('const uint S=tokens+16,G=(tokens+31)/32,KT=tokens/2;','const uint G=(tokens+31)/32,KT=tokens/2,PT=(KT+15)&~15u;')
s=s.replace('const unsigned char*ks=(pooled?pool:in)+(pooled?key/2:tokens+key)*1024+rc()*1024+head*32;', 'const unsigned char*ks=pooled?pool+2*tokens*1024+(head*PT+key/2+rc())*32:pool+(head*tokens+key+rc())*32;')
s=s.replace('const unsigned char*vs=(pooled?pool:in)+(pooled?KT+key/2:2*tokens+key)*1024+head*32;', 'uint stride=pooled?PT:tokens;const unsigned char*vs=pooled?pool+(2*tokens+PT)*1024+head*32*PT+key/2:pool+tokens*1024+head*32*tokens+key;')
old='for(uint c=0;c<2;c++){i2 y{};for(uint e=0;e<8;e++){uint b=vs[(gr()*8+e)*1024+c*16+rc()];y[e/4]=int(uint(y[e/4])|(b<<(8*(e%4))));}acc[c]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(xb,y,acc[c]);}'
assert old in s
s=s.replace(old,'for(uint c=0;c<2;c++){i2 y{};__builtin_memcpy(&y,vs+(c*16+rc())*stride+gr()*8,8);acc[c]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(xb,y,acc[c]);}')
if '--fragments' in sys.argv:
 old='const unsigned char*ks=pooled?pool+2*tokens*1024+(head*PT+key/2+rc())*32:pool+(head*tokens+key+rc())*32;'
 new='uint nt=pooled?PT/16:tokens/16,ki=pooled?key/32:key/16;const unsigned char*ks=pool+(pooled?2*tokens*1024:0)+(head*nt+ki)*512+__builtin_amdgcn_workitem_id_x()*8;'
 assert old in s;s=s.replace(old,new).replace('ks+k+gr()*8','ks+(k/16)*256')
 old='uint stride=pooled?PT:tokens;const unsigned char*vs=pooled?pool+(2*tokens+PT)*1024+head*32*PT+key/2:pool+tokens*1024+head*32*tokens+key;'
 new='const unsigned char*vs=pool+(pooled?(2*tokens+PT)*1024:tokens*1024)+(head*nt+ki)*512+__builtin_amdgcn_workitem_id_x()*8;'
 assert old in s;s=s.replace(old,new).replace('vs+(c*16+rc())*stride+gr()*8','vs+c*256')
out=Path('/tmp/vit-select-src');(out/'kernel/deep_fast.hip').write_text((root/'hip/deep_fast.hip').read_text()+'\n'+s)
p=out/'Development/HIP/hip_reference_network.h';old=p.read_text();new=old.replace('auto pool=New(size_t(n)*256),route=', 'auto pool=New(size_t(2*n+2*((n/2+15)&~15u))*256),route=');assert old!=new;p.write_text(new)
(here/'packed-host.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/select-host.h',tofile='b/select-host.h')))
print('packed selective candidate generated')
