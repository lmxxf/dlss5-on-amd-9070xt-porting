from pathlib import Path
import shutil,re
p=Path(__file__).parent;r=p/'one-plane-isolated/source';shutil.copytree(p/'r2d-both/source',r,dirs_exist_ok=True)
new=(p/'one-plane/source/hip/wave_owned_mh.inc').read_text();a=new.index('template<uint C,bool ByteIn,bool ByteOut,bool Up=false');b=new.index('#define W2_KERNEL',a);body=new[a:b].replace('DEV void swin_wave2_body(', 'DEV void swin_wave2_one_plane_body(')
f=r/'hip/wave_owned_mh.inc';s=f.read_text();m=re.search(r'^#define W2_KERNEL\(.*$',s,re.M);assert m;s=s[:m.end()]+'\n'+body+s[m.end():]
for macro in ['W2_KERNEL','W2_KERNEL_W16','W2_KERNEL_HO','W2_KERNEL_HOB']:
 pattern=r'^#define '+macro+r'\(.*$';m=re.search(pattern,s,re.M);assert m,macro;line=m[0];pre,call=line.split('{',1);assert call.endswith('}');call=call[:-1];line=pre+'{if constexpr(C==128)'+call.replace('swin_wave2_body<','swin_wave2_one_plane_body<')+'else '+call+'}';s=s[:m.start()]+line+s[m.end():]
f.write_text(s)
# The recipe extracts the ORIGINAL attention-only body, ending at the first W2_KERNEL macro.
orig=Path('hip/wave_owned_mh.inc').read_text();anchor=' // One wave owns all keys';assert s[s.index(anchor):s.index('#define W2_KERNEL')]==orig[orig.index(anchor):orig.index('#define W2_KERNEL')]
