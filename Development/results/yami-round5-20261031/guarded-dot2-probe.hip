using uint=unsigned;using ushort=unsigned short;
using h8=_Float16 __attribute__((ext_vector_type(8)));
using f8=float __attribute__((ext_vector_type(8)));
using u4=uint __attribute__((ext_vector_type(4)));
#define DEV __attribute__((device,always_inline)) inline
DEV uint hsh(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
DEV float guarded_dot2(const h8*ex,uint&tier){
 float local=0.f;const uint ones=0x3c003c00u;
 _Pragma("unroll")for(uint k=0;k<4;k++){u4 w=__builtin_bit_cast(u4,ex[k]);_Pragma("unroll")for(uint j=0;j<4;j++)asm("v_dot2_f32_f16 %0, %1, %2, %0":"+v"(local):"v"(w[j]),"s"(ones));}
 uint lane=__builtin_amdgcn_workitem_id_x()&31u;float other=__builtin_bit_cast(float,__builtin_amdgcn_ds_bpermute(int((lane^16u)*4u),__builtin_bit_cast(int,local)));float den=local+other;
 if(__builtin_amdgcn_ballot_w32(!(den<32.f))==0u){tier=0;return den;}
 uint mn=0xffffffffu;
 _Pragma("unroll")for(uint k=0;k<4;k++){u4 w=__builtin_bit_cast(u4,ex[k]);_Pragma("unroll")for(uint j=0;j<4;j++)asm("v_pk_min_u16 %0, %0, %1":"+v"(mn):"v"(w[j]));}
 uint a=mn&65535u,b=mn>>16,e=(a<b?a:b)>>10;float bound=__builtin_bit_cast(float,(e+4u+127u)<<23);
 bool safe=e>=1u&&e<=18u&&den<bound;
 tier=__builtin_amdgcn_ballot_w32(!safe)==0u?1u:2u;return den;
}
extern "C" __attribute__((global,amdgpu_flat_work_group_size(32,32)))void guarded_dot2_probe(uint*out,uint n){
 uint group=__builtin_amdgcn_workgroup_id_x(),lane=__builtin_amdgcn_workitem_id_x(),i=group*32+lane;if(i>=n)return;uint mode=group%4;h8 ex[4];
 _Pragma("unroll")for(uint k=0;k<4;k++){u4 w{};_Pragma("unroll")for(uint j=0;j<8;j++){
  uint x=hsh(i*131u+k*17u+j*301u+0x1234567u),e=mode==0?1+x%13:mode==1?6+x%13:1+x%18,m=(x>>16)&31u;
  if(e==18)m&=7u;
  if(mode==3){e=k==0?18u:1u;m=k==0?7u:m;}
  uint hb=(e<<10)|(m<<5);w[j/2]|=hb<<(16*(j%2));
 }ex[k]=__builtin_bit_cast(h8,w);}
 h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;f8 ref{};
 _Pragma("unroll")for(uint k=0;k<4;k++)ref=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[k],ref);
 float old=ref[0];uint tier;float fast=guarded_dot2(ex,tier);float picked=tier<2?fast:old;uint repeats=0;
 _Pragma("unroll")for(uint e=0;e<8;e++){float t=ref[e];repeats+=__builtin_bit_cast(uint,t)!=__builtin_bit_cast(uint,old);}
 out[8*i]=__builtin_bit_cast(uint,old);out[8*i+1]=__builtin_bit_cast(uint,fast);out[8*i+2]=__builtin_bit_cast(uint,picked);out[8*i+3]=tier;out[8*i+4]=mode;out[8*i+5]=repeats;out[8*i+6]=i;out[8*i+7]=i^0xa53c19e7u;
}
