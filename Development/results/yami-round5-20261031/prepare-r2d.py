from pathlib import Path
import shutil
p=Path(__file__).parent
for arm in ['both','mh_only']:
 root=p/('r2d-'+arm)/'source';shutil.copytree('hip',root/'hip',dirs_exist_ok=True,ignore=shutil.ignore_patterns('modules','*.exe','*.hsaco'))
 (root/'Development/HIP').mkdir(parents=True,exist_ok=True);shutil.copyfile('Development/HIP/swin_persistent_types.h',root/'Development/HIP/swin_persistent_types.h')
 if arm=='mh_only':
  f=root/'hip/wave_owned_c32.inc';s=f.read_text();old='''  const float inv=norm_inverse(sums[0]);
  i2 prob[4];_Pragma("unroll 4") for(uint kt=0;kt<4;kt++){CW_Q8_DECL(a);for(uint e=0;e<8;e++)CW_Q8_SET(a,e,float(ex[kt][e])*inv);CW_Q8_END_SITE(a,4);prob[kt]=a;}''';new='''  i2 prob[4];_Pragma("unroll 4") for(uint kt=0;kt<4;kt++){CW_Q8_DECL(a);for(uint e=0;e<8;e++)CW_Q8_SET(a,e,float(ex[kt][e])*norm_inverse(sums[e]));CW_Q8_END_SITE(a,4);prob[kt]=a;}''';assert s.count(old)==1;f.write_text(s.replace(old,new))
