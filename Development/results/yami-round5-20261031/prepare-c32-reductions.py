from pathlib import Path
import shutil,json,hashlib
p=Path(__file__).parent;src=(p/'guarded-dot2-probe.hip').read_text();a=src.index('DEV float guarded_dot2');b=src.index('extern "C"',a);old=src[a:b];g=old.index(' if(__builtin_amdgcn_ballot');suffix=old[g:];kernel=src[b:];prefix=src[:a]
compute={
 'dotpar':''' float z0=0.f,z1=0.f,z2=0.f,z3=0.f;const uint ones=0x3c003c00u;
 u4 w0=__builtin_bit_cast(u4,ex[0]),w1=__builtin_bit_cast(u4,ex[1]),w2=__builtin_bit_cast(u4,ex[2]),w3=__builtin_bit_cast(u4,ex[3]);
 _Pragma("unroll")for(uint j=0;j<4;j++){
  asm("v_dot2_f32_f16 %0, %1, %2, %0":"+v"(z0):"v"(w0[j]),"s"(ones));
  asm("v_dot2_f32_f16 %0, %1, %2, %0":"+v"(z1):"v"(w1[j]),"s"(ones));
  asm("v_dot2_f32_f16 %0, %1, %2, %0":"+v"(z2):"v"(w2[j]),"s"(ones));
  asm("v_dot2_f32_f16 %0, %1, %2, %0":"+v"(z3):"v"(w3[j]),"s"(ones));
 }
 float local=(z0+z1)+(z2+z3);uint lane=__builtin_amdgcn_workitem_id_x()&31u;
 float other=__builtin_bit_cast(float,__builtin_amdgcn_ds_bpermute(int((lane^16u)*4u),__builtin_bit_cast(int,local)));float den=local+other;
''',
 'wmma2':''' h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;f8 sa{},sb{};
 sa=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[0],sa);
 sb=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[1],sb);
 sa=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[2],sa);
 sb=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[3],sb);
 float den=sa[0]+sb[0];
''',
 'wmma4':''' h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;float part[4];
 _Pragma("unroll")for(uint k=0;k<4;k++){f8 t=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[k],f8{});part[k]=t[0];}
 float den=(part[0]+part[1])+(part[2]+part[3]);
'''}
helpers={name:'DEV float guarded_'+name+'(const h8*ex,uint&tier){\n'+code+suffix for name,code in compute.items()}
helpers={'dotseq':old.replace('guarded_dot2','guarded_dotseq'),**helpers}
kernel=kernel.replace('mode=group%4','mode=group%8')
a='  if(mode==3){e=k==0?18u:1u;m=k==0?7u:m;}'
b=a+'''
  uint key=k*16+(lane>>4)*8+j;
  if(mode==4){e=13;m=31;if(key==0){e=1;m=1;}}
  if(mode==5){e=18;m=7;if(key==0){e=6;m=1;}}
  if(mode>=6){if(key<3){e=18;m=7;}else if(key<8){e=14;m=0;}else if(key==8){e=12;m=31;}else{uint t=key-9;e=1;m=t<10?28u:t==10?(mode==6?7u:9u):0u;}}'''
assert kernel.count(a)==1;kernel=kernel.replace(a,b)
(p/'guarded-reductions-probe.hip').write_text(prefix+'\n'.join(helpers.values())+'\n'+'\n'.join(kernel.replace('guarded_dot2_probe','guarded_'+name+'_probe').replace('guarded_dot2(ex,tier)','guarded_'+name+'(ex,tier)')for name in helpers))
cpp=(p/'guarded-dot2-probe.cpp').read_text().replace('if(argc!=2)','if(argc!=3)').replace('a.hipModuleGetFunction(&f,m,"guarded_dot2_probe")','a.hipModuleGetFunction(&f,m,argv[2])').replace('printf("GUARDED_DOT2','printf("KERNEL %s\\n",argv[2]);printf("GUARDED_DOT2');cpp=cpp.replace('#include "hip_api.h"','#include "hip_api.h"\n#include <cmath>\n#include <cstring>').replace('pairbad=0;','pairbad=0,oracle=0,invalid=0;').replace('(i/32)%4','(i/32)%8')
cpp=cpp.replace('pairbad+=z[0]!=v[8*(i^16u)];','''pairbad+=z[0]!=v[8*(i^16u)];float rv,fv;std::memcpy(&rv,z,4);std::memcpy(&fv,z+1,4);invalid+=!std::isfinite(rv)||!std::isfinite(fv);if(z[4]>=4){double want=z[4]==4?63.0*0.4921875+33.0/524288.0:z[4]==5?63.0*9.75+33.0/16384.0:z[4]==6?32.0-1.0/524288.0:32.0+1.0/524288.0;float wf=float(want);unsigned wb;std::memcpy(&wb,&wf,4);oracle+=wb!=z[0];}''')
cpp=cpp.replace('a.hipFree(d);','printf("ORACLE oracle_diff=%zu invalid=%zu\\n",oracle,invalid);a.hipFree(d);').replace('return bad||missing','return oracle||invalid||bad||missing')
(p/'guarded-reductions-probe.cpp').write_text(cpp)
block='''  f8 sums{};h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;
  _Pragma("unroll 4") for(uint kt=0;kt<4;kt++)sums=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[kt],sums);
  const float inv=norm_inverse(sums[0]);'''
for name,helper in helpers.items():
 if name=='dotseq':continue
 r=p/('c32-'+name)/'source';shutil.copytree(p/'r2d-both/source',r,dirs_exist_ok=True);f=r/'hip/wave_owned_c32.inc';s=f.read_text();h=helper.replace('guarded_'+name,'cw_exact_exp_sum64').replace('u4','cw_exact_u4');s='// Only positive Swin exponent F16 lattice; exact guarded reassociation, otherwise originalWMMA.\nusing cw_exact_u4=uint __attribute__((ext_vector_type(4)));\n'+h+s
 assert s.count(block)==1;s=s.replace(block,'''  uint cw_den_tier;float cw_den64=cw_exact_exp_sum64(ex,cw_den_tier);
  if(cw_den_tier==2u){f8 sums{};h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;
   _Pragma("unroll 4") for(uint kt=0;kt<4;kt++)sums=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[kt],sums);cw_den64=sums[0];}
  const float inv=norm_inverse(cw_den64);''');f.write_text(s)
r=p/'c32-wmma-fused/source';shutil.copytree(p/'r2d-both/source',r,dirs_exist_ok=True);f=r/'hip/wave_owned_c32.inc';s=f.read_text();assert s.count(block)==1;s=s.replace(block,'  const float inv=norm_inverse(sums[0]);');a='  h8 ex[4];';assert s.count(a)==1;s=s.replace(a,a+'f8 sums{};h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;');a='   ex[kt]=__builtin_bit_cast(h8,pk);';assert s.count(a)==1;s=s.replace(a,a+'\n   sums=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[kt],sums);');f.write_text(s)
r=r'D:\DLSSNR-Lab\yami-round5-20261031';cc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe';file='guarded-reductions-probe.hip';m={'name':'guarded-reductions-compile','files':[dict(path=cc,sha256='553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'),dict(path=r+'\\'+file,sha256=hashlib.sha256((p/file).read_bytes()).hexdigest())],'jobs':[dict(label='guarded-reductions-compile',exe=cc,cpu_compile=True,env={'RTC_EXTRA_OPTS':''},arguments='"{OUT}\\probe.hsaco" "'+r+'\\'+file+'" comgr gfx1201')]};(p/'guarded-reductions-compile.json').write_text(json.dumps(m,indent=2)+'\n')
