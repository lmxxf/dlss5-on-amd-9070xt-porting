 typedef int sparse_i4 __attribute__((ext_vector_type(4)));
 sparse_i4 input4;__builtin_memcpy(&input4,packed+(first+rc())*36+gr()*16,16);
 C32_LOOP for(uint col=0;col<128;col+=16){
  uint at=(col/16)*32+gr()*16+rc();i2 a=matrix8(fw,41088+at*8);uint index;__builtin_memcpy(&index,reinterpret_cast<const unsigned char*>(fw)+43136+at*4,4);
  f8 sum{};sum=__builtin_amdgcn_swmmac_f32_16x16x32_fp8_fp8_w32(a,input4,sum,index);
  uint lo=0,hi=0;for(uint e=0;e<8;e++){float v=sum[e],g=clampf(v,-4.f,4.f),q=absf(g)*(-.055908203125f)+.447265625f,p=g*q+.89453125f;uint z=fp8(v*p)&255u;if(e<4)lo|=z<<(8*e);else hi|=z<<(8*(e-4));}
  i2 word{int(lo),int(hi)};__builtin_memcpy(scratch.hidden+(first+rc())*132+col+gr()*8,&word,8);
 }
