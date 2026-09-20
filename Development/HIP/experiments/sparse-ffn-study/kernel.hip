#pragma clang fp contract(off)
#pragma clang fp reassociate(off)
typedef unsigned uint;typedef unsigned char u8;
typedef int i2 __attribute__((ext_vector_type(2)));typedef int i8 __attribute__((ext_vector_type(8)));typedef float f8 __attribute__((ext_vector_type(8)));
#define DEV __attribute__((device)) __attribute__((always_inline))
#define KERNEL extern "C" __attribute__((global))
#define WAVE KERNEL __attribute__((amdgpu_flat_work_group_size(32,32)))
DEV uint bits(float x){union{float f;uint u;}v;v.f=x;return v.u;}
DEV float val(uint x){union{float f;uint u;}v;v.u=x;return v.f;}
DEV float ab(float x){return val(bits(x)&0x7fffffffu);}
DEV u8 activation(float v){float g=v<-4?-4:v>4?4:v;float poly=__builtin_fmaf(g,ab(g)*(-.055908203125f)+.447265625f,.89453125f);float z=v*poly;z=z<-448?-448:z>448?448:z;if((bits(z)&0x7fffffffu)==0)z=0;return u8(__builtin_amdgcn_cvt_pk_fp8_f32(z,0.f,0,false));}

typedef int i4 __attribute__((ext_vector_type(4)));
// A is the sparse weight matrix. C is transposed when written to token-major output.
WAVE void sparse_expand(const u8*x,const u8*w,const uint*indices,u8*out,float*pre,uint M){
 uint lane=__builtin_amdgcn_workitem_id_x(),rc=lane%16,gr=lane/16,block=__builtin_amdgcn_workgroup_id_x();
 uint first=block/256*16,col=block%256*16;f8 c{};
 for(uint k=0;k<1024;k+=32){
  i2 a;i4 b;__builtin_memcpy(&a,w+(col+rc)*512+k/2+gr*8,8);
  __builtin_memcpy(&b,x+(first+rc)*1024+k+gr*16,16);
  uint index=indices[(col+rc)*64+k/16+gr];
  c=__builtin_amdgcn_swmmac_f32_16x16x32_fp8_fp8_w32(a,b,c,index);
 }
 for(uint e=0;e<8;e++){uint at=(first+rc)*4096+col+gr*8+e;float v=c[e];if(pre)pre[at]=v;out[at]=activation(v);}
}

WAVE void sparse_expand_frag(const u8*x,const u8*w,const uint*indices,u8*out,float*pre,uint M){
 uint lane=__builtin_amdgcn_workitem_id_x(),rc=lane%16,gr=lane/16,block=__builtin_amdgcn_workgroup_id_x();
 uint first=block/64*16,col=block%64*64;f8 c[4]{};
 for(uint k=0;k<1024;k+=32){i4 b;__builtin_memcpy(&b,x+(first+rc)*1024+k+gr*16,16);
  #pragma unroll
  for(uint j=0;j<4;j++){uint at=((col/16+j)*32+k/32)*32+lane;i2 a;__builtin_memcpy(&a,w+at*8,8);uint index=indices[at];c[j]=__builtin_amdgcn_swmmac_f32_16x16x32_fp8_fp8_w32(a,b,c[j],index);}
 }
 for(uint j=0;j<4;j++){uint at=(first+rc)*4096+col+j*16+gr*8;uint lo=0,hi=0;for(uint e=0;e<4;e++){lo|=uint(activation(c[j][e]))<<(e*8);hi|=uint(activation(c[j][e+4]))<<(e*8);}i2 packed{int(lo),int(hi)};__builtin_memcpy(out+at,&packed,8);if(pre)__builtin_memcpy(pre+at,&c[j],32);}
}
