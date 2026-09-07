// Candidate E4M3FN round-to-nearest-even. Validated domain must be finite half.
float NativeFastFp8(float v) {
 uint bits=asuint(v),a=bits&0x7fffffffu,sign=bits&0x80000000u;
 if(a==0)return 0.0; // Legacy comparison v<0 treats negative zero as positive.
 if(a<0x3c800000u)return (v<0?-1.0:1.0)*round(abs(v)*512.0)/512.0;
 if(a>=0x43e00000u)return asfloat(sign|0x43e00000u); //448
 uint rounded=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;
 return asfloat(sign|min(rounded,0x43e00000u));
}
