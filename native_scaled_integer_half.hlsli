// Exact RNE of signed int32 * 2^exponent to binary16; no intermediate float cast.
uint NativeRoundShift(uint m,int shift){
 if(shift<=0)return m<<uint(-shift);
 if(shift>=32)return shift==32&&m>0x80000000u?1u:0u;
 uint q=m>>shift,rem=m&((1u<<shift)-1u),half=1u<<(shift-1);
 return q+((rem>half||(rem==half&&(q&1u)))?1u:0u);
}
float NativeScaledIntegerHalf(int value,int exponent){
 uint sign=value<0?0x8000u:0u,m=value<0?0u-asuint(value):asuint(value);
 if(!m)return 0;
 int lead=firstbithigh(m),e=lead+exponent;uint bits;
 if(e < -14)bits=NativeRoundShift(m,-exponent-24);
 else{
  uint q=NativeRoundShift(m,lead-10);
  if(q==2048u){q=1024u;e++;}
  bits=e>15?0x7c00u:(uint(e+15)<<10)|(q-1024u);
 }
 return f16tof32(sign|bits);
}
