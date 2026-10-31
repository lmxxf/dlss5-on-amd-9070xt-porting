from pathlib import Path
import shutil,re,sys
p=Path(__file__).parent
for c in [64,256]:
 r=p/f'one-plane-c{c}/source';shutil.copytree(p/'one-plane-isolated/source',r,dirs_exist_ok=True);f=r/'hip/wave_owned_mh.inc';s=f.read_text();a='constexpr bool w2_one_plane=C==128&&!Up';assert s.count(a)==1;s=s.replace(a,f'constexpr bool w2_one_plane=C=={c}&&!Up')
 for macro in ['W2_KERNEL','W2_KERNEL_W16','W2_KERNEL_HO','W2_KERNEL_HOB']:
  m=re.search(r'^#define '+macro+r'\(.*$',s,re.M);assert m;line=m[0];assert line.count('if constexpr(C==128)')==1;line=line.replace('if constexpr(C==128)',f'if constexpr(C=={c})');s=s[:m.start()]+line+s[m.end():]
 f.write_text(s)
