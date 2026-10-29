from pathlib import Path
import shutil,difflib
p=Path(__file__).parent;d=p/'work/4-source';assert not d.exists();shutil.copytree('hip',d,ignore=shutil.ignore_patterns('*.hsaco','*.exe','*.dll'));f=d/'wave_owned_c32.inc';old=f.read_text();s='#ifndef CW_POST_REGISTER_TAIL\n#define CW_POST_REGISTER_TAIL 0\n#endif\n'+old
key=' _Pragma("clang loop unroll(disable)") for(uint qt=0;qt<4;qt++){';assert s.count(key)==1;s=s.replace(key,key+'\n  h8 post_current[2];',1)
key='if constexpr(Finish||Post)__builtin_memcpy(residual+((qt*2+ci)*32+(g*16+r))*8,&rounded,16);';assert s.count(key)==1;s=s.replace(key,'if constexpr(CW_POST_REGISTER_TAIL&&Post&&!PostTap){if(!logit)post_current[ci]=rounded;else __builtin_memcpy(residual+((qt*2+ci)*32+(g*16+r))*8,&rounded,16);}else '+key)
# Formal recipes use CW_RTZ_PAIR; leave all other instantiations unchanged.
key='  __builtin_amdgcn_sched_barrier(0);\n }\n\n#if CW_POST_HEAD_VEC';assert s.count(key)==1
code='''  if constexpr(CW_POST_REGISTER_TAIL&&CW_RTZ_PAIR&&Post&&!PostTap){if(!logit){
   {float sums[3]{};
    _Pragma("unroll") for(uint ci=0;ci<2;ci++){
     h8 local=post_current[ci],peer;
     typedef _Float16 ph2 __attribute__((ext_vector_type(2)));
     _Pragma("unroll") for(uint pair=0;pair<4;pair++){ph2 v{local[pair*2],local[pair*2+1]};uint u=__builtin_amdgcn_ds_bpermute((lane^16u)*4u,__builtin_bit_cast(uint,v));ph2 v2=__builtin_bit_cast(ph2,u);peer[pair*2]=v2[0];peer[pair*2+1]=v2[1];}
     _Pragma("unroll") for(uint half=0;half<2;half++)_Pragma("unroll") for(uint e=0;e<8;e++){float f=float(half?(g?local[e]:peer[e]):(g?peer[e]:local[e]));uint c=ci*16+half*8+e;sums[0]+=f*headw[c];sums[1]+=f*headw[32+c];sums[2]+=f*headw[64+c];}
    }
    uint t=qt*16+r,ww=width+2*sx,tx=(win%(ww/8))*8,ty=(win/(ww/8))*8;int x=int(tx+t%8)-int(sx),y=int(ty+t/8)-int(sy);
    if(g==0&&x>=0&&y>=0&&x<int(width)&&y<int(height)){uint pix=uint(y)*width+uint(x);for(uint c=0;c<3;c++){float acc=Hrtz(sums[c]),centered=color[pix*4+c]*.125f-.0625f,v=(acc*rgbscale+centered)*8.f+.5f;rgb[pix*(PostRGBA?4:3)+c]=__builtin_amdgcn_fmed3f(v,0.f,1.f);}if constexpr(PostRGBA)rgb[pix*4+3]=1.f;}
   }
  }}
'''
s=s.replace(key,code+key).replace('\n#if CW_POST_HEAD_VEC || CW_POST_FULL_TILE','\n if constexpr(CW_POST_REGISTER_TAIL&&CW_RTZ_PAIR&&Post&&!PostTap){if(!logit)return;}\n#if CW_POST_HEAD_VEC || CW_POST_FULL_TILE',1);f.write_text(s);(p/'4-kernel.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),s.splitlines(True),fromfile='a/hip/wave_owned_c32.inc',tofile='b/hip/wave_owned_c32.inc')))
f=d/'build-modules.ps1';s=f.read_text()
for name in ['c32-wave1-fast','c32-wave1-fast-norm900']:
 key="@{ name = '"+name+"'; defines = @(";assert key in s;s=s.replace(key,key+"'CW_POST_REGISTER_TAIL 1',")
f.write_text(s)
