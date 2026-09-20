"""Only change attention scheduling: compute each AV tile immediately, retaining rounding and K order."""
from pathlib import Path
import difflib
here=Path(__file__).resolve().parent;root=here.parents[3]
s=(root/'hip/deep_fast.hip').read_text();a=s.index('template<uint MAXT,bool ByteInput=false,bool ByteOut=false>\nDEV void vit_attention_fused_body');b=s.index('// Byte-stream twins:',a);f=s[a:b]
f=f.replace(' __attribute__((shared)) unsigned char p8[16*(MAXT+16)];\n','').replace('const uint S=tokens+16;','')
f=f.replace(' f8 sum{};',' f8 sum{},acc[2]{};')
p=f.index(' f8 acc[2]{};');q=f.index(' for(uint c=0;c<2;c++)for(uint e=0;e<8;e++){if constexpr(ByteOut)',p)
av=f[p:q];body=next(x for x in av.splitlines() if 'for(uint c=0;c<2;c++){' in x)
body=body.replace('uint key=k+gr()*8+e;','uint vkey=key+gr()*8+e;').replace('2*tokens+key','2*tokens+vkey').replace('(x,y,acc[c])','(xb,y,acc[c])')
f=f[:p]+f[q:];f=f.replace('  __builtin_memcpy(p8+rc()*S+key+gr()*8,&xb,8);',body)
new=s[:a]+f+s[b:];out=Path('/tmp/vit-stream-exact-src');out.mkdir(exist_ok=True);(out/'deep_fast.hip').write_text(new)
(here/'candidate.patch').write_text(''.join(difflib.unified_diff(s.splitlines(True),new.splitlines(True),fromfile='a/hip/deep_fast.hip',tofile='b/hip/deep_fast.hip')))
print(out)
