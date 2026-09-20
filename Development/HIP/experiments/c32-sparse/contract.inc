 // Calculate contraction separately, then add the existing residual. This changes FP32
 // addition order versus dense residual-in-accumulator; quality controls must account for it.
 typedef int sparse_i4 __attribute__((ext_vector_type(4)));
 f8 contracted[2]{};
 for(uint chunk=0;chunk<4;chunk++){
  sparse_i4 b;__builtin_memcpy(&b,scratch.hidden+(first+rc())*132+chunk*32+gr()*16,16);
  for(uint ci=0;ci<2;ci++){uint at=(ci*4+chunk)*32+gr()*16+rc();i2 a=matrix8(fw,41088+at*8);uint index;__builtin_memcpy(&index,reinterpret_cast<const unsigned char*>(fw)+43136+at*4,4);
   contracted[ci]=__builtin_amdgcn_swmmac_f32_16x16x32_fp8_fp8_w32(a,b,contracted[ci],index);
  }
 }

 for(uint ci=0;ci<2;ci++){
  f8 v=contracted[ci];uint lane=gr()*16+rc();
  for(uint bit=1;bit<=4;bit*=2){f8 next;
   for(uint e=0;e<8;e++){float other=value(__builtin_amdgcn_ds_bpermute((lane^bit)*4,bits(v[e^bit])));next[e]=((lane&bit)!=(e&bit))?other:v[e];}v=next;
  }
  uint source=(lane&7)|((lane&8)<<1)|((lane&16)>>1);
  for(uint e=0;e<8;e++)acc[ci][e]+=value(__builtin_amdgcn_ds_bpermute(source*4,bits(v[e])));
 }
