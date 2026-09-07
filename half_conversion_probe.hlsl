RWStructuredBuffer<float> output:register(u0);
#include "native_half_corrected.hlsli"
float Hardware(float v){
#if PROBE_CORRECTED
 return NativeCorrectedHalf(v);
#elif PROBE_NATIVE_CAST
 return float(float16_t(v));
#else
 return f16tof32(f32tof16(v));
#endif
}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
[numthreads(32,1,1)]void main(uint3 id:SV_DispatchThreadID){
 for(uint j=0;j<32;j++){
  uint n=id.x*32+j;
#if PROBE_VALUES
  uint sample=n/4;
#else
  uint sample=n;
#endif
  uint h=(sample*251)%31742;
  float lo=f16tof32(h),hi=f16tof32(h+1);
  precise float midpoint=(lo+hi)*0.5;
  uint bits=asuint(midpoint);
  if(sample%4==0)bits--;else if(sample%4==2)bits++;
  if(sample&4)bits|=0x80000000u;
  float v=asfloat(bits);
#if PROBE_VALUES
  output[n]=n%4==0?v:n%4==1?H(v):n%4==2?Hardware(v):float(h);
#else
  output[n]=asuint(H(v))==asuint(Hardware(v))?0.0:1.0;
#endif
 }
}
