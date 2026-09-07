// ViT global attention reading a packed f16 copy of the normalized Q/K/V
// (values are on the F8 lattice, so the f16 copy is exact). Same arithmetic as
// native_wave_vit_attention.hlsl: identical exp bit mapping, the same
// denominator tree per 64-key chunk (now two lanes per query, combined with a
// wave read), F() after the reduction, K32 products with H() per step.
#include <dx/linalg.h>
ByteAddressBuffer qkv:register(t0);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint tokens;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
uint key_index(uint c){return ((c&16)>>4)|((c&1)<<1)|((c&2)<<1)|(c&8)|((c&4)<<2)|(c&32);}
groupshared half ex[640*16];
groupshared float inverse[16];
using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint t:SV_GroupIndex){
 uint query=gid.x*16,head=gid.y;
 A q=A::Load(qkv,(query*1024+head*32)*2,2048,dx::linalg::MatrixLayout::RowMajor,16);
 for(uint key=0;key<tokens;key+=16){
  B k=B::Load(qkv,((tokens+key)*1024+head*32)*2,2048,dx::linalg::MatrixLayout::ColMajor,16);
  C s=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(q,k);
  for(uint i=0;i<s.Length();i++){
   uint2 c=s.GetCoordinate(i);
   float affine=clamp(H(H(s.Get(i))*f16tof32(0x2dbb)+1.708984375),1.439453125,1.9775390625);
   uint b=f32tof16(affine);
   ex[(key+c.y)*16+c.x]=half(f16tof32(((b<<4)+0x4000)&65535));
  }
 }
 GroupMemoryBarrierWithGroupSync();
 {
  // Two lanes per query: lane>>4 selects the parity half of every 64-key chunk.
  uint qi=t&15,parity=t>>4;float denominator=0;
  for(uint chunk=0;chunk<tokens;chunk+=64){
   float accumulated=0;
   [unroll]for(uint group=0;group<4;group++){
    uint base=parity+(group&1)*2+(group>>1)*8;
    float a=H(float(ex[(chunk+key_index(base))*16+qi])+float(ex[(chunk+key_index(base+16))*16+qi]));
    a=H(a+H(float(ex[(chunk+key_index(base+4))*16+qi])+float(ex[(chunk+key_index(base+20))*16+qi])));
    a=H(a+H(float(ex[(chunk+key_index(base+32))*16+qi])+float(ex[(chunk+key_index(base+48))*16+qi])));
    a=H(a+H(float(ex[(chunk+key_index(base+36))*16+qi])+float(ex[(chunk+key_index(base+52))*16+qi])));
    accumulated=group?H(accumulated+a):a;
   }
   float other=WaveReadLaneAt(accumulated,t^16);
   float sum0=parity?other:accumulated,sum1=parity?accumulated:other;
   denominator=H(denominator+H(sum0+sum1));
  }
  if(t<16)inverse[t]=H(1.0/denominator);
 }
 GroupMemoryBarrierWithGroupSync();
 for(uint i=t;i<tokens*16;i+=32)ex[i]=half(F(float(ex[i])));
 GroupMemoryBarrierWithGroupSync();
 C acc[2];acc[0]=C::Splat(0.0f);acc[1]=C::Splat(0.0f);
 for(uint key=0;key<tokens;key+=32){
  A a=A::Load(ex,key*16,16,dx::linalg::MatrixLayout::ColMajor);
  [unroll]for(uint half_index=0;half_index<2;half_index++){
   B b=B::Load(qkv,((2*tokens+key)*1024+head*32+half_index*16)*2,2048,dx::linalg::MatrixLayout::RowMajor,16);
   C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   for(uint i=0;i<acc[half_index].Length();i++)acc[half_index].Set(i,H(acc[half_index].Get(i)+p.Get(i)));
  }
 }
 [unroll]for(uint half_index=0;half_index<2;half_index++)for(uint i=0;i<acc[half_index].Length();i++){
  uint2 c=acc[half_index].GetCoordinate(i);
  output[(query+c.x)*1024+head*32+half_index*16+c.y]=F(H(acc[half_index].Get(i)*inverse[c.x]));
 }
}
// f32 -> f16 copy of the normalized Q/K/V (F8-lattice values: exact).
StructuredBuffer<float> source:register(t0);
RWByteAddressBuffer packed:register(u0);
[numthreads(64,1,1)]void pack(uint3 id:SV_DispatchThreadID){
 uint pair=id.x;if(pair>=tokens*3072/2)return;
 packed.Store(pair*4,f32tof16(source[pair*2])|(f32tof16(source[pair*2+1])<<16));
}
