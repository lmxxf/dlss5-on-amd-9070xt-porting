#include <dx/linalg.h>
// Input is normalized Q/K/V on the existing F8 lattice, losslessly held in half.
// Keep unquantized half exponent values for the original denominator tree;
// quantize them only after that reduction, before the K32 value products.
// 16 queries per wave; shared storage is bounded to 21568 bytes at 640 tokens.
StructuredBuffer<float> input:register(t0);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint tokens;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float F(float v){float a=abs(v),sg=v<0?-1:1;if(a<.015625)return sg*round(a*512)/512;float e=floor(log2(a)),m=round((a/exp2(e)-1)*8);if(m==8){m=0;e++;}return sg*min(exp2(e)*(1+m/8),448);}
uint key_index(uint c){return ((c&16)>>4)|((c&1)<<1)|((c&2)<<1)|(c&8)|((c&4)<<2)|(c&32);}

groupshared half ex[640*16],tile[512];
groupshared float inverse[16];
[WaveSize(32)]
[numthreads(32,1,1)]void main(uint3 gid:SV_GroupID,uint t:SV_GroupIndex){
 uint query=gid.x*16,head=gid.y;
 using A=dx::linalg::Matrix<dx::linalg::ComponentType::F16,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
 using B=dx::linalg::Matrix<dx::linalg::ComponentType::F16,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
 using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
 for(uint i=t;i<512;i+=32)tile[i]=half(input[(query+i/32)*1024+head*32+i%32]);
 GroupMemoryBarrierWithGroupSync();
 A q=A::Load(tile,0,32,dx::linalg::MatrixLayout::RowMajor);
 GroupMemoryBarrierWithGroupSync();
 for(uint key=0;key<tokens;key+=16){
  for(uint i=t;i<512;i+=32)tile[i]=half(input[(tokens+key+i/32)*1024+head*32+i%32]);
  GroupMemoryBarrierWithGroupSync();
  B k=B::Load(tile,0,32,dx::linalg::MatrixLayout::ColMajor);
  C s=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(q,k);
  for(uint i=0;i<s.Length();i++){
   uint2 c=s.GetCoordinate(i);
   float affine=clamp(H(H(s.Get(i))*f16tof32(0x2dbb)+1.708984375),1.439453125,1.9775390625);
   uint b=f32tof16(affine);
   ex[(key+c.y)*16+c.x]=half(f16tof32(((b<<4)+0x4000)&65535));
  }
  GroupMemoryBarrierWithGroupSync();
 }
 if(t<16){
  float denominator=0;
  for(uint chunk=0;chunk<tokens;chunk+=64){
   float sums[2];
   [unroll]for(uint parity=0;parity<2;parity++){
    float accumulated=0;
    [unroll]for(uint group=0;group<4;group++){
     uint base=parity+(group&1)*2+(group>>1)*8;
     float a=H(float(ex[(chunk+key_index(base))*16+t])+float(ex[(chunk+key_index(base+16))*16+t]));
     a=H(a+H(float(ex[(chunk+key_index(base+4))*16+t])+float(ex[(chunk+key_index(base+20))*16+t])));
     a=H(a+H(float(ex[(chunk+key_index(base+32))*16+t])+float(ex[(chunk+key_index(base+48))*16+t])));
     a=H(a+H(float(ex[(chunk+key_index(base+36))*16+t])+float(ex[(chunk+key_index(base+52))*16+t])));
     accumulated=group?H(accumulated+a):a;
    }
    sums[parity]=accumulated;
   }
   denominator=H(denominator+H(sums[0]+sums[1]));
  }
  inverse[t]=H(1.0/denominator);
 }
 GroupMemoryBarrierWithGroupSync();
 for(uint i=t;i<tokens*16;i+=32)ex[i]=half(F(float(ex[i])));
 GroupMemoryBarrierWithGroupSync();
 for(uint channel=0;channel<32;channel+=16){
  C acc=C::Splat(0.0f);
  for(uint key=0;key<tokens;key+=32){
   for(uint i=t;i<512;i+=32)tile[i]=half(input[(2*tokens+key+i/16)*1024+head*32+channel+i%16]);
   GroupMemoryBarrierWithGroupSync();
   A a=A::Load(ex,key*16,16,dx::linalg::MatrixLayout::ColMajor);
   B b=B::Load(tile,0,16,dx::linalg::MatrixLayout::RowMajor);
   C p=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
   for(uint i=0;i<acc.Length();i++)acc.Set(i,H(acc.Get(i)+p.Get(i)));
   GroupMemoryBarrierWithGroupSync();
  }
  for(uint i=0;i<acc.Length();i++){
   uint2 c=acc.GetCoordinate(i);
   output[(query+c.x)*1024+head*32+channel+c.y]=F(H(acc.Get(i)*inverse[c.x]));
  }
 }
}
