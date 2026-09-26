// Independent C64/C128/C256 block. Each wave owns all 64 tokens of one 32-channel head.
// Shared matrices use native 16x16 A fragments: [token tile][channel tile][lane][8 bytes].
// Two 64*C-byte planes alternate input -> contract -> feature -> AV; no global intermediate.
DEV uint w2_lane(){return __builtin_amdgcn_workitem_id_x()&31u;}
DEV uint w2_rc(){return w2_lane()&15u;}
DEV uint w2_gr(){return w2_lane()>>4;}
DEV uint w2_pixel(uint win,uint tok,uint workw){return ((win/(workw/8))*8+tok/8)*workw+(win%(workw/8))*8+tok%8;}
DEV void w2_sync(){WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);}
template<uint C> DEV i2 w2_load(const unsigned char*p,uint qt,uint ct,uint lane=w2_lane()){i2 a;__builtin_memcpy(&a,p+((qt*(C/16)+ct)*32+lane)*8,8);return a;}
template<uint C> DEV void w2_store(unsigned char*p,uint qt,uint ct,i2 a){__builtin_memcpy(p+((qt*(C/16)+ct)*32+w2_lane())*8,&a,8);}
#ifndef W2_FRAGMENT_WEIGHTS
#define W2_FRAGMENT_WEIGHTS 0
#endif
#ifndef W2_DEFER_Q
#define W2_DEFER_Q 0
#endif
#ifndef W2_LAUNDER_QKV
#define W2_LAUNDER_QKV 0
#endif
#ifndef W2_SCHED_FENCE
#define W2_SCHED_FENCE 0
#endif
#ifndef W2_ROLL_QUERY
#define W2_ROLL_QUERY 0
#endif
#ifndef W2_HIDDEN_TILES
#define W2_HIDDEN_TILES 1
#endif
using w2_i16=int __attribute__((ext_vector_type(16)));
DEV void w2_schedule(){
#if W2_SCHED_FENCE
 __builtin_amdgcn_sched_barrier(0);
#endif
}
DEV i2 w2_weight(const float*p,uint row,uint stride,uint k,uint group=w2_gr()){i2 a;
#if W2_FRAGMENT_WEIGHTS
 uint off=((row/16)*(stride/32)+k/32)*512+(((k%32)/16*2+group)*16+row%16)*8;
#else
 uint off=row*stride+k+group*8;
#endif
 __builtin_memcpy(&a,reinterpret_cast<const unsigned char*>(p)+off,8);return a;}

DEV void w2_bits(i2&a,uint e,uint v){a[e/4]=int(uint(a[e/4])|((v&255u)<<(8*(e%4))));}
// W2_PACK8 (2026-09-27, results/c64-block-fused-20260927): the same E4M3 bytes as q8_fused_round, two values per
// v_cvt_pk_fp8_f32 written straight into the fragment word. +0.f after the clamp maps -0 to +0 (the old select on |x|==0)
// and leaves every other input unchanged, NaN included; it follows the clamp so it cannot contract into a preceding mul.
#ifndef W2_PACK8
#define W2_PACK8 0
#endif
#if W2_PACK8
DEV float w2_z(float x){return __builtin_fminf(__builtin_fmaxf(x,-448.f),448.f)+0.f;}
DEV i2 w2_pack8(const float*t){int lo=__builtin_amdgcn_cvt_pk_fp8_f32(w2_z(t[0]),w2_z(t[1]),0,false);lo=__builtin_amdgcn_cvt_pk_fp8_f32(w2_z(t[2]),w2_z(t[3]),lo,true);
 int hi=__builtin_amdgcn_cvt_pk_fp8_f32(w2_z(t[4]),w2_z(t[5]),0,false);hi=__builtin_amdgcn_cvt_pk_fp8_f32(w2_z(t[6]),w2_z(t[7]),hi,true);i2 a;a[0]=lo;a[1]=hi;return a;}
#define W2_Q8_DECL(A) i2 A;float A##_t[8]
#define W2_Q8_SET(A,E,X) A##_t[E]=(X)
#define W2_Q8_END(A) A=w2_pack8(A##_t)
#else
#define W2_Q8_DECL(A) i2 A{}
#define W2_Q8_SET(A,E,X) w2_bits(A,E,q8_fused_round(X))
#define W2_Q8_END(A) (void)0
#endif
DEV float w2_shuffle(float v,uint src){return __builtin_bit_cast(float,__builtin_amdgcn_ds_bpermute(src*4,__builtin_bit_cast(int,v)));}
DEV float w2_serial_norm(f8 lo,f8 hi){
 // Same 32 scalar products/adds in channel order as production. Both halves execute;
 // each shuffle carries the correct prefix to the half owning the next eight channels.
 float s=0.f;
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=lo[e]*lo[e];
 s=w2_shuffle(s,w2_rc());
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=lo[e]*lo[e];
 s=w2_shuffle(s,w2_rc()+16);
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=hi[e]*hi[e];
 s=w2_shuffle(s,w2_rc());
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=hi[e]*hi[e];
 return w2_shuffle(s,w2_rc()+16);
}

template<uint C,bool ByteIn,bool ByteOut>
DEV void swin_wave2_body(const float*in,const float*fw,const float*aw,void*out_any,
 uint width,uint height,uint workw,uint workh,uint sx,uint sy,uint post){
 const uint win=__builtin_amdgcn_workgroup_id_x(),head=__builtin_amdgcn_workitem_id_x()/32;
 const uint rc=w2_rc(),gr=w2_gr();
 if(win>=(workw/8)*(workh/8))return;
 __attribute__((shared)) unsigned char plane0[64*C],plane1[64*C];
 auto input=[&](uint tok,uint col)->float{
  uint p=w2_pixel(win,tok,workw);int x=int(p%workw)-int(sx),y=int(p/workw)-int(sy);
  bool valid=x>=0&&y>=0&&x<int(width)&&y<int(height);uint idx=valid?(uint(y)*width+uint(x))*C+col:col;
  float v;if constexpr(ByteIn)v=__builtin_amdgcn_cvt_f32_fp8(int(reinterpret_cast<const unsigned char*>(in)[idx]),0);else v=in[idx];
  return valid?v:0.f;
 };
 // Stage original input in A fragments. Preserve the byte cast used by pack4.
 for(uint qt=0;qt<4;qt++)for(uint ci=0;ci<2;ci++){
  i2 a{};for(uint e=0;e<8;e++)w2_bits(a,e,q8(input(qt*16+rc,head*32+ci*16+gr*8+e)));
  w2_store<C>(plane0,qt,head*2+ci,a);
 }
 w2_sync();
 // Each head contracts only its own 128 hidden channels. Expanded fragments never enter LDS.
 _Pragma("clang loop unroll(disable)") for(uint qt=0;qt<4;qt++){
  f8 contract[2]{};
  _Pragma("clang loop unroll(disable)") for(uint ht=0;ht<8;ht+=W2_HIDDEN_TILES){
   f8 expanded[W2_HIDDEN_TILES]{};
   for(uint kt=0;kt<C/16;kt++){
    i2 a=w2_load<C>(plane0,qt,kt);
    _Pragma("unroll") for(uint tile=0;tile<W2_HIDDEN_TILES;tile++){
     i2 b=w2_weight(fw,head*128+(ht+tile)*16+rc,C,kt*16);
     expanded[tile]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,expanded[tile]);
    }
   }
   _Pragma("unroll") for(uint tile=0;tile<W2_HIDDEN_TILES;tile++){
    W2_Q8_DECL(hidden);
    for(uint e=0;e<8;e++){float a=expanded[tile][e],g=clampf(a,-4.f,4.f),q=absf(g)*(-.055908203125f)+.447265625f,poly=g*q+.89453125f;W2_Q8_SET(hidden,e,a*poly);}W2_Q8_END(hidden);
    for(uint ci=0;ci<2;ci++){
     i2 b=w2_weight(fw+4*C*C,head*32+ci*16+rc,4*C,head*128+(ht+tile)*16);
     contract[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,hidden,contract[ci]);
    }
   }
  }
  for(uint ci=0;ci<2;ci++){W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,Hrtz(contract[ci][e]));W2_Q8_END(a);w2_store<C>(plane1,qt,head*2+ci,a);}
 }
 w2_sync();
 // Cross-head FFN mixing plus the original residual. plane0 is now free for feature bytes.
 _Pragma("clang loop unroll(disable)") for(uint qt=0;qt<4;qt++){
  f8 result[2]{};
  for(uint ci=0;ci<2;ci++)for(uint e=0;e<8;e++){uint c=head*32+ci*16+gr*8+e;result[ci][e]=Hrtz(input(qt*16+rc,c)*fw[9*C*C+c]);}
  for(uint kt=0;kt<C/16;kt++)for(uint ci=0;ci<2;ci++){
   i2 a=w2_load<C>(plane1,qt,kt),b=w2_weight(fw+8*C*C,head*32+ci*16+rc,C,kt*16);
   result[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,result[ci]);
  }
  for(uint ci=0;ci<2;ci++){W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,result[ci][e]);W2_Q8_END(a);w2_store<C>(plane0,qt,head*2+ci,a);}
 }
 w2_sync();
 // Q/K use A-fragment orientation. V is computed in the opposite orientation,
 // directly becoming the B fragments consumed by P*V; no V transpose/staging needed.
 i2 qkv[3][4][2];
#if W2_ROLL_QUERY && !W2_DEFER_Q
 w2_i16 query_words{};
#endif
 _Pragma("unroll") for(uint part=W2_DEFER_Q?1:0;part<3;part++){
  _Pragma("unroll 4") for(uint qt=0;qt<4;qt++){
   uint qlane=w2_lane();
#if W2_LAUNDER_QKV
   // Make each tile's coordinates a fresh compiler value, preventing whole-window
   // A/B fragments from being hoisted and kept live across all unrolled tiles.
   __asm__ volatile("" : "+v"(qlane));qlane&=31u;
#endif
   uint qrc=qlane&15u,qgr=qlane>>4;
   f8 q[2]{};
   for(uint kt=0;kt<C/16;kt++)for(uint ci=0;ci<2;ci++){
    i2 a=w2_load<C>(plane0,qt,kt,qlane),b=w2_weight(aw,part*C+head*32+ci*16+qrc,C,kt*16,qgr);
    if(part<2)q[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,q[ci]);
    else q[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,q[ci]);
   }
   float inv=1.f;
   if(part<2){float ss=w2_serial_norm(q[0],q[1]);inv=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?aw[4*C*C+(C/32)*4096+head]:1.f);}
   for(uint ci=0;ci<2;ci++){W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,q[ci][e]*inv);W2_Q8_END(a);
#if W2_ROLL_QUERY && !W2_DEFER_Q
    if(part==0){query_words[qt*4+ci*2]=a[0];query_words[qt*4+ci*2+1]=a[1];}else
#endif
    qkv[part][qt][ci]=a;
   }
   w2_schedule();
  }
 }
 // One wave owns all keys and queries of a head. Existing WMMA half row-sum order is retained.
#if W2_ROLL_QUERY
 _Pragma("clang loop unroll(disable)")
#else
 _Pragma("unroll 4")
#endif
 for(uint qt=0;qt<4;qt++){
#if W2_ROLL_QUERY && !W2_DEFER_Q
  i2 query_frag[2];_Pragma("unroll 2") for(uint ci=0;ci<2;ci++)query_frag[ci]=i2{query_words[qt*4+ci*2],query_words[qt*4+ci*2+1]};
#endif
#if W2_DEFER_Q
  uint qlane=w2_lane();
#if W2_LAUNDER_QKV
  __asm__ volatile("" : "+v"(qlane));qlane&=31u;
#endif
  uint qrc=qlane&15u,qgr=qlane>>4;
  f8 query[2]{};
  for(uint kt=0;kt<C/16;kt++)for(uint ci=0;ci<2;ci++){
   i2 a=w2_load<C>(plane0,qt,kt,qlane),b=w2_weight(aw,head*32+ci*16+qrc,C,kt*16,qgr);
   query[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,query[ci]);
  }
  float qs=w2_serial_norm(query[0],query[1]);
  float qinv=__builtin_amdgcn_rsqf(maxf(qs,6.198883056640625e-5f))*aw[4*C*C+(C/32)*4096+head];
  i2 query_frag[2];for(uint ci=0;ci<2;ci++){W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,query[ci][e]*qinv);W2_Q8_END(a);query_frag[ci]=a;}
#endif
  h8 exponent[4];
  _Pragma("unroll 4") for(uint key=0;key<4;key++){
   f8 scores{};
   _Pragma("unroll 2") for(uint ci=0;ci<2;ci++)scores=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(qkv[1][key][ci],
#if W2_DEFER_Q || W2_ROLL_QUERY
    query_frag[ci],
#else
    qkv[0][qt][ci],
#endif
    scores);
   h8 ex{};
   for(uint e=0;e<8;e++){
    uint row=qt*16+rc,k=key*16+gr*8+e;
    float score=scores[e]+aw[4*C*C+head*4096+row*64+k],affine=clampf(score*.044921875f+1.30078125f,1.03125f,1.5693359375f);
    uint ah=(bits(affine)>>13)-0x1c000u;unsigned short hb=static_cast<unsigned short>(((ah<<5)+0x8000u)&65535u);ex[e]=__builtin_bit_cast(_Float16,hb);
   }
   exponent[key]=ex;
  }
  h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;
  f8 sums[2]{};
  _Pragma("unroll 2") for(uint side=0;side<2;side++){
   _Pragma("unroll 2") for(uint half=0;half<2;half++)sums[side]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,exponent[side+2*half],sums[side]);
  }
  i2 prob[4];
  _Pragma("unroll 4") for(uint key=0;key<4;key++){W2_Q8_DECL(a);for(uint e=0;e<8;e++){float inv=1.f/(sums[0][e]+sums[1][e]);W2_Q8_SET(a,e,float(exponent[key][e])*inv);}W2_Q8_END(a);prob[key]=a;}
  for(uint ci=0;ci<2;ci++){
   f8 av{};
   _Pragma("unroll 4") for(uint key=0;key<4;key++)av=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(qkv[2][key][ci],prob[key],av);
   W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,av[e]);W2_Q8_END(a);w2_store<C>(plane1,qt,head*2+ci,a);
  }
  w2_schedule();
 }
 w2_sync();
 // All heads' AV ready; feature still lives in plane0. Preserve diagonal residual and K order.
 const unsigned char*diag=reinterpret_cast<const unsigned char*>(aw)+4*(4*C*C+(C/32)*4096+C/32+C);
 _Pragma("clang loop unroll(disable)") for(uint qt=0;qt<4;qt++){
  f8 result[2]{};
  for(uint ci=0;ci<2;ci++){
   uint ct=head*2+ci;i2 a=w2_load<C>(plane0,qt,ct);
   for(uint part=0;part<3;part++){i2 b;__builtin_memcpy(&b,diag+(part*(C/16)+ct)*512+(gr*16+rc)*8,8);result[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,result[ci]);}
  }
  for(uint kt=0;kt<C/16;kt++)for(uint ci=0;ci<2;ci++){
   i2 a=w2_load<C>(plane1,qt,kt),b=w2_weight(aw+3*C*C,head*32+ci*16+rc,C,kt*16);
   result[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,result[ci]);
  }
  uint p=w2_pixel(win,qt*16+rc,workw),x=p%workw,y=p/workw;
  if(x>=sx&&y>=sy&&x<sx+width&&y<sy+height){
   uint dest=((y-sy)*width+x-sx)*C+head*32+gr*8;
   for(uint ci=0;ci<2;ci++){
    if constexpr(ByteOut){W2_Q8_DECL(a);for(uint e=0;e<8;e++){float v=result[ci][e];W2_Q8_SET(a,e,post==0?Hrtz(v):v);}W2_Q8_END(a);__builtin_memcpy(reinterpret_cast<unsigned char*>(out_any)+dest+ci*16,&a,8);}
    else for(uint e=0;e<8;e++){float v=result[ci][e];static_cast<float*>(out_any)[dest+ci*16+e]=post==3?Hrtz(v):post==0?F(Hrtz(v)):F(v);}
   }
  }
 }
}
#define W2_KERNEL(NAME,C,BI,BO) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post){swin_wave2_body<C,BI,BO>(in,fw,aw,out,w,h,ww,hh,sx,sy,post);}
W2_KERNEL(c64_wave2,64,false,false)
W2_KERNEL(c64_wave2_bi,64,true,false)
W2_KERNEL(c64_wave2_bo,64,false,true)
W2_KERNEL(c64_wave2_bi_bo,64,true,true)
W2_KERNEL(c128_wave2,128,false,false)
W2_KERNEL(c128_wave2_bi,128,true,false)
W2_KERNEL(c128_wave2_bo,128,false,true)
W2_KERNEL(c128_wave2_bi_bo,128,true,true)
W2_KERNEL(c256_wave2,256,false,false)
W2_KERNEL(c256_wave2_bi,256,true,false)
W2_KERNEL(c256_wave2_bo,256,false,true)
W2_KERNEL(c256_wave2_bi_bo,256,true,true)
