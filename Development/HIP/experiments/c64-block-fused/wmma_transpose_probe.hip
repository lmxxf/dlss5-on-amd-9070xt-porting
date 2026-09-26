// c64-block-fused probe: is wmma(Y,X,C^T) == wmma(X,Y,C)^T bit for bit on gfx12 (fp8 and f16)?
// The transposed [feature][token] orientation (mochizuki fswin_t.comp) is operand swapping; bit-exactness with the current
// fast chain needs the hardware reduction to be symmetric in the A/B roles, including the f32 accumulator input.
#define KERNEL extern "C" __attribute__((global))
typedef unsigned int uint;
using i2=int __attribute__((ext_vector_type(2)));
using f8=float __attribute__((ext_vector_type(8)));
using h8=_Float16 __attribute__((ext_vector_type(8)));
// X,Y: [16][K] bytes (fp8) or halves; C: [16][16] f32 row-major (row m, col n). Out: D (orientation 0) and D' (1) row-major.
KERNEL void probe_fp8(const unsigned char*X,const unsigned char*Y,const float*C,float*out,uint K){
 uint l=__builtin_amdgcn_workitem_id_x()&31u,r=l&15u,g=l>>4;
 f8 d0,d1;for(uint i=0;i<8;i++){uint m=g*8+i;d0[i]=C[m*16+r];d1[i]=C[r*16+m];}
 for(uint k=0;k<K;k+=16){i2 x,y;__builtin_memcpy(&x,X+r*K+k+g*8,8);__builtin_memcpy(&y,Y+r*K+k+g*8,8);
  d0=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(x,y,d0);
  d1=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(y,x,d1);}
 for(uint i=0;i<8;i++){uint m=g*8+i;out[m*16+r]=d0[i];out[256+r*16+m]=d1[i];}}
KERNEL void probe_f16(const _Float16*X,const _Float16*Y,const float*C,float*out,uint K){
 uint l=__builtin_amdgcn_workitem_id_x()&31u,r=l&15u,g=l>>4;
 f8 d0,d1;for(uint i=0;i<8;i++){uint m=g*8+i;d0[i]=C[m*16+r];d1[i]=C[r*16+m];}
 for(uint k=0;k<K;k+=16){h8 x,y;__builtin_memcpy(&x,X+r*K+k+g*8,16);__builtin_memcpy(&y,Y+r*K+k+g*8,16);
  d0=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,y,d0);
  d1=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(y,x,d1);}
 for(uint i=0;i<8;i++){uint m=g*8+i;out[m*16+r]=d0[i];out[256+r*16+m]=d1[i];}}
