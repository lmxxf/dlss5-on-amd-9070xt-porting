// FAST PATH: wave-matrix C32 downsample projection (pooled 32 -> 64), replacing the scalar native_c32_ds.hlsl.
// The pooled input is F()-quantized (E4M3-exact) f32; the tile is loaded as two 16x16 accumulators and cast to E4M3
// in LDS for the A operand. Weights are an E4M3 copy of the f32 [64][32] table (checked exact on the host).
// Output = F(H(sum)) as f32 [pixel][64], identical values to the scalar kernel up to f32 summation order.
#include <dx/linalg.h>
ByteAddressBuffer pooled:register(t0);
ByteAddressBuffer weights:register(t1);
RWByteAddressBuffer output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;uint work_width;uint crop_x;uint crop_y;}
float H(float v){return f16tof32(f32tof16(v));}
float F(float v){uint bits=asuint(v),a=bits&0x7fffffffu;if(a>=0x7f800000u)return v;float sg=v<0?-1:1;if(a<0x3c800000u)return sg*round(abs(v)*512)/512;if(a>=0x43e00000u)return sg*448;uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return sg*min(asfloat(r),448);}
groupshared uint a8[128];
using A8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B8=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID){
 // 16 consecutive output pixels of one row (width % 16 == 0, checked on the host) are contiguous in the pooled source.
 uint p=gid.x*16;if(p>=width*height)return;
 uint base=((p/width+crop_y)*work_width+p%width+crop_x)*32;
 C in0=C::Load(pooled,base*4,128,dx::linalg::MatrixLayout::RowMajor,16),in1=C::Load(pooled,base*4+64,128,dx::linalg::MatrixLayout::RowMajor,16);
 in0.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(a8,0,8,dx::linalg::MatrixLayout::RowMajor);
 in1.Cast<dx::linalg::ComponentType::F8_E4M3FN>().Store(a8,4,8,dx::linalg::MatrixLayout::RowMajor);
 GroupMemoryBarrierWithGroupSync();
 A8 a=A8::Load(a8,0,8,dx::linalg::MatrixLayout::RowMajor);
 [unroll]for(uint n=0;n<4;n++){
  B8 b=B8::Load(weights,n*16*32,32,dx::linalg::MatrixLayout::ColMajor,16);
  C z=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
  for(uint i=0;i<z.Length();i++)z.Set(i,F(H(z.Get(i))));
  z.Store(output,(p*64+n*16)*4,256,dx::linalg::MatrixLayout::RowMajor,16);
 }
}
