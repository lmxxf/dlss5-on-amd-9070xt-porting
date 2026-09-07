// Experimental: current-device RTZ conversion plus explicit nearest-even repair.
// Do not enable on another conversion mode without probing it first.
float NativeCorrectedHalf(float v){
 uint bits=asuint(v),sign=bits&0x80000000u,mag=bits&0x7fffffffu;
 if(mag>=0x7f800000u)return v;
 float a=asfloat(mag);
 if(a>=65520.0)return asfloat(sign|0x7f800000u);
 if(a>=65504.0)return asfloat(sign|0x477fe000u);
 uint h=f32tof16(a)&0x7fffu;
 float lo=f16tof32(h),hi=f16tof32(h+1);
 precise float midpoint=(lo+hi)*0.5;
 if(a>midpoint||(a==midpoint&&(h&1)))h++;
 return asfloat(sign|asuint(f16tof32(h)));
}
