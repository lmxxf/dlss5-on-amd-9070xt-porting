#pragma clang fp contract(off)
#pragma clang fp reassociate(off)
using uint=unsigned int;using i2=int __attribute__((ext_vector_type(2)));using f8=float __attribute__((ext_vector_type(8)));
#define DEV __attribute__((device)) __attribute__((always_inline))
#define KERNEL extern "C" __attribute__((global))


DEV uint q8(float x){return uint(__builtin_amdgcn_cvt_pk_fp8_f32(x,x,0,false))&255u;}

DEV float c5c_shuffle(float v,uint src){return __builtin_bit_cast(float,__builtin_amdgcn_ds_bpermute(src*4,__builtin_bit_cast(int,v)));}

DEV float c5c_norm_broadcast(float v,bool upper){
 uint b=__builtin_bit_cast(uint,v);
 uint swapped=__builtin_amdgcn_permlanex16(b,b,0x76543210u,0xfedcba98u,false,false);
 const bool high=(__builtin_amdgcn_workitem_id_x()&16u)!=0;
 return high==upper?v:__builtin_bit_cast(float,swapped);
}

DEV float norm_old(f8 lo,f8 hi){
 // Channel order is 0..7,8..15,16..23,24..31, as in the production raw-LDS loop.
 // Both half-waves execute, but only the correct prefix is carried to the next half.
 uint row=__builtin_amdgcn_workitem_id_x()&15u;float s=0.f;
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=lo[e]*lo[e];
 s=c5c_shuffle(s,row);
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=lo[e]*lo[e];
 s=c5c_shuffle(s,row+16);
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=hi[e]*hi[e];
 s=c5c_shuffle(s,row);
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=hi[e]*hi[e];
 return c5c_shuffle(s,row+16);
}

#define C512_NORM_PERMLANE 1

DEV float norm_new(f8 lo,f8 hi){
 // Channel order is 0..7,8..15,16..23,24..31, as in the production raw-LDS loop.
 // Both half-waves execute, but only the correct prefix is carried to the next half.
 uint row=__builtin_amdgcn_workitem_id_x()&15u;float s=0.f;
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=lo[e]*lo[e];
 
#if C512_NORM_PERMLANE
 s=c5c_norm_broadcast(s,false);
#else
 s=c5c_shuffle(s,row);
#endif

 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=lo[e]*lo[e];
 
#if C512_NORM_PERMLANE
 s=c5c_norm_broadcast(s,true);
#else
 s=c5c_shuffle(s,row+16);
#endif

 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=hi[e]*hi[e];
 
#if C512_NORM_PERMLANE
 s=c5c_norm_broadcast(s,false);
#else
 s=c5c_shuffle(s,row);
#endif

 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=hi[e]*hi[e];
 
#if C512_NORM_PERMLANE
 return c5c_norm_broadcast(s,true);
#else
 return c5c_shuffle(s,row+16);
#endif

}

DEV uint c5c_fp8(float x){return uint(__builtin_amdgcn_cvt_pk_fp8_f32(__builtin_amdgcn_fmed3f(x,-448.f,448.f),0.f,0,false))&255u;}

DEV uint c5c_q8mz(float a,float b){
#pragma clang fp contract(off)
 return q8(__builtin_amdgcn_fmed3f(a*b+0.f,-448.f,448.f));}

DEV void c5c_bits(i2&v,uint e,uint b){v[e/4]=int(uint(v[e/4])|((b&255u)<<(8*(e%4))));}

DEV i2 c5c_pack8_pairs(f8 v){i2 a{};
 a[0]=__builtin_amdgcn_cvt_pk_fp8_f32(v[0],v[1],0,false);
 a[0]=__builtin_amdgcn_cvt_pk_fp8_f32(v[2],v[3],a[0],true);
 a[1]=__builtin_amdgcn_cvt_pk_fp8_f32(v[4],v[5],0,false);
 a[1]=__builtin_amdgcn_cvt_pk_fp8_f32(v[6],v[7],a[1],true);return a;}

DEV float c5c_pair_qkv_input(float a,float b){
#pragma clang fp contract(off)
 return __builtin_amdgcn_fmed3f(a*b+0.f,-448.f,448.f);}

DEV float c5c_pair_plain_input(float x){return __builtin_amdgcn_fmed3f(x,-448.f,448.f);}


KERNEL __attribute__((amdgpu_flat_work_group_size(32,32))) void norm_lane_probe(const float*input,const uint*tags,uint*out,uint lanes){
 uint gid=__builtin_amdgcn_workgroup_id_x()*32+__builtin_amdgcn_workitem_id_x();if(gid>=lanes)return;
 f8 lo{},hi{};for(uint e=0;e<8;e++){lo[e]=input[gid*16+e];hi[e]=input[gid*16+8+e];}
 out[gid*6]=__builtin_bit_cast(uint,norm_old(lo,hi));out[gid*6+1]=__builtin_bit_cast(uint,norm_new(lo,hi));
 const uint rc=__builtin_amdgcn_workitem_id_x()&15u;float tag=__builtin_bit_cast(float,tags[gid]);
 out[gid*6+2]=__builtin_bit_cast(uint,c5c_shuffle(tag,rc));out[gid*6+3]=__builtin_bit_cast(uint,c5c_norm_broadcast(tag,false));
 out[gid*6+4]=__builtin_bit_cast(uint,c5c_shuffle(tag,rc+16));out[gid*6+5]=__builtin_bit_cast(uint,c5c_norm_broadcast(tag,true));
}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void scalar_pair_probe(const float*input,const float*scales,uint*out,uint lanes){
 uint gid=__builtin_amdgcn_workgroup_id_x()*128+__builtin_amdgcn_workitem_id_x();if(gid>=lanes)return;
 f8 q{},p{},v{};i2 qo{},po{},vo{};
 for(uint e=0;e<8;e++){float a=input[gid*8+e],b=scales[gid*8+e];
  c5c_bits(qo,e,c5c_q8mz(a,b));q[e]=c5c_pair_qkv_input(a,b);
  float probability=a*b;c5c_bits(po,e,c5c_fp8(probability));p[e]=c5c_pair_plain_input(probability);
  c5c_bits(vo,e,c5c_fp8(a));v[e]=c5c_pair_plain_input(a);
 }
 const i2 qn=c5c_pack8_pairs(q),pn=c5c_pack8_pairs(p),vn=c5c_pack8_pairs(v);
 for(uint k=0;k<2;k++){out[gid*12+k]=uint(qo[k]);out[gid*12+2+k]=uint(qn[k]);out[gid*12+4+k]=uint(po[k]);out[gid*12+6+k]=uint(pn[k]);out[gid*12+8+k]=uint(vo[k]);out[gid*12+10+k]=uint(vn[k]);}
}

