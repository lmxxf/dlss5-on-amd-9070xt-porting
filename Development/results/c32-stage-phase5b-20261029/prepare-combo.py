from pathlib import Path
import shutil,difflib,sys
p=Path(__file__).parent;fused=len(sys.argv)>1 and sys.argv[1]=='fused';d=p/'work/combo-source';assert not d.exists();shutil.copytree(p/'work/3-source'if fused else Path('hip'),d,ignore=shutil.ignore_patterns('*.hsaco','*.exe','*.dll'));shared=d.parent/'Development/HIP';shared.mkdir(parents=True,exist_ok=True);shutil.copyfile('Development/HIP/swin_persistent_types.h',shared/'swin_persistent_types.h');f=d/'wave_owned_c32.inc';old=f.read_text();s='#ifndef CW_OUTPROJ_HOIST\n#define CW_OUTPROJ_HOIST 0\n#endif\n'+old
key=' _Pragma("clang loop unroll(disable)") for(uint qt=0;qt<4;qt++){';assert s.count(key)==1
s=s.replace(key,''' cw_i2_pair proj_hoisted[2];float skip_hoisted[2][8];
 if constexpr(CW_OUTPROJ_HOIST && W16>0){
  const uint pl=__builtin_amdgcn_workitem_id_x(),pr=pl&15u,pg=pl>>4;
  _Pragma("unroll") for(uint ci=0;ci<2;ci++){
   proj_hoisted[ci]=cw_matrix16(w,3072+ci*512+(pg*16+pr)*16);
   __builtin_memcpy(skip_hoisted[ci],w+8193+ci*16+pg*8,32);
  }
 }
'''+key);s=s.replace('cw_i2_pair bp=cw_matrix16(w,3072+ci*512+(g*16+r)*16);','cw_i2_pair bp;if constexpr(CW_OUTPROJ_HOIST)bp=proj_hoisted[ci];else bp=cw_matrix16(w,3072+ci*512+(g*16+r)*16);').replace('prod=res*w[8193+ci*16+g*8+e]','prod=res*((CW_OUTPROJ_HOIST&&W16>0)?skip_hoisted[ci][e]:w[8193+ci*16+g*8+e])');f.write_text(s)
f=d/'build-modules.ps1';s=f.read_text()
for name in ['c32-wave1-fast','c32-wave1-fast-norm900']:
 key="@{ name = '"+name+"'; defines = @(";assert key in s;s=s.replace(key,key+"'CW_OUTPROJ_HOIST 1',"+("'CW_NORM_HOIST 1',"if name=='c32-wave1-fast'else''))
f.write_text(s);(p/'combo-kernel.patch').write_text(''.join(difflib.unified_diff(Path('hip/wave_owned_c32.inc').read_text().splitlines(True),(d/'wave_owned_c32.inc').read_text().splitlines(True),fromfile='a/hip/wave_owned_c32.inc',tofile='b/hip/wave_owned_c32.inc')));(p/'combo-recipe.patch').write_text(''.join(difflib.unified_diff(Path('hip/build-modules.ps1').read_text().splitlines(True),s.splitlines(True),fromfile='a/hip/build-modules.ps1',tofile='b/hip/build-modules.ps1')))
