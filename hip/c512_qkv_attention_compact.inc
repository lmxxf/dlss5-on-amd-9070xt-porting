// Paired FP8 conversion preserves the path-specific float operations before packing.
#ifndef C512_PAIR_PACK
#define C512_PAIR_PACK 0
#endif
// C512 compact unshifted input; attention windows retain original shifted geometry. Appended to multihead_fast_padded.hip.
// The host supplies the same tiled FFN feature and fragment-packed QKV weights
// as mh_qkv_normalize_frag_c512_m32; AV remains raster FP8 for the old projection.
#ifndef C512_COMPACT_QKV_ATTN
#define C512_COMPACT_QKV_ATTN 0
#endif
#ifndef C512_COMPACT_WAVES
#define C512_COMPACT_WAVES 2
#endif
#ifndef C512_COMPACT_DEBUG
#define C512_COMPACT_DEBUG 0
#endif
// C512_COMPACT_VT (2026-09-30, results/c512-compact-vt-20260930): V (part 2) is written to LDS transposed, [channel][token],
// so each P*V B fragment (8 consecutive keys of one channel) is one 8-byte LDS read instead of 8 ds_load_u8 + byte inserts.
// Same bytes, same WMMA operands -> bit-identical. 0 = original [token][channel] layout.
#ifndef C512_COMPACT_VT
#define C512_COMPACT_VT 0
#endif
// C512_COMPACT_NOF (c512-av-f 2026-09-30): drop the F() in front of the two byte quantizations.
//  QKV: q8(F(y)) == q8(med3(y+0,-448,448)) for all 2^32 y (F only turns -0 into +0; the +0 add does the same).
//  AV : c5c_fp8(c5c_F(av)) == c5c_fp8(av) except av = -0, which the P.V WMMA chain from +0 never returns.
//  CPU + GPU exhaustive proof: Development/results/c512-av-f-20260930. The mul and +0 are kept apart (contract off).
#ifndef C512_COMPACT_NOF
#define C512_COMPACT_NOF 0
#endif
// C512_COMPACT_RCP (same experiment): softmax 1/sum as rcp + two Newton steps instead of the IEEE divide sequence.
//  The 64 exponent values are in [2^-14, 9.75], so sum is in [1/256, 624]; c32 norm_inverse was checked bit-for-bit
//  against 1.f/x for every float in that interval on gfx1201 (2026-09-19) and again here (probe_r).
#ifndef C512_COMPACT_RCP
#define C512_COMPACT_RCP 0
#endif
// C512_COMPACT_FUSEQKV (2026-10-01, results/mochizuki-gap-20261001): Q, K and V projections in one k loop
// (6 accumulators per token tile, as mochizuki's attn.comp NR_APARTS=1/NR_AN=6) instead of three passes that
// each re-read the X fragments; the per-token bounds test and address are computed once, outside the loop.
// Every accumulator sees the same WMMA sequence (gs, h order) -> bit-identical. 0 = three-pass original.
// C512_COMPACT_QKV_PIPE (2026-10-01, results/mochizuki-gap-20261001): with FUSEQKV, next K step's fragments are
// prefetched one step ahead (register double buffer). Bit-identical. 0 = off.
// C512_COMPACT_ABLATE: DIAGNOSTIC ONLY, wrong output. bit0 = no serial Q/K norm, bit1 = stop after the QKV stage.
#ifndef C512_COMPACT_ABLATE
#define C512_COMPACT_ABLATE 0
#endif
#define C5C_PRAGMA(x) _Pragma(#x)
#define C5C_UNROLL(n) C5C_PRAGMA(unroll n)
#ifndef C512_COMPACT_QKV_UNROLL
#define C512_COMPACT_QKV_UNROLL 0 /* 2026-10-01: static unroll factor of the fused QKV gs loop (0 = compiler default) */
#endif
#ifndef C512_COMPACT_QKV_PIPE
#define C512_COMPACT_QKV_PIPE 0
#endif
#ifndef C512_COMPACT_FUSEQKV
#define C512_COMPACT_FUSEQKV 0
#endif
#ifndef C512_COMPACT_QKV_DEEP
#define C512_COMPACT_QKV_DEEP 0 /* 2026-10-01: explicit D-deep fragment ring in the fused QKV loop (0 = off) */
#endif
#ifndef C512_COMPACT_QKV_SCHED
#define C512_COMPACT_QKV_SCHED 0 /* with QKV_DEEP: sched_barrier around each step's WMMA group (keeps loads issued early) */
#endif
#if C512_COMPACT_QKV_ATTN
#if C512_COMPACT_WAVES != 2 && C512_COMPACT_WAVES != 4
#error C512_COMPACT_WAVES must be 2 or 4
#endif
DEV uint c5c_pixel(uint win,uint tok,uint width){return ((win/(width/8))*8+tok/8)*width+(win%(width/8))*8+tok%8;}
DEV float c5c_shuffle(float v,uint src){return __builtin_bit_cast(float,__builtin_amdgcn_ds_bpermute(src*4,__builtin_bit_cast(int,v)));}
DEV float c5c_serial_norm(f8 lo,f8 hi){
 // Channel order is 0..7,8..15,16..23,24..31, as in the production raw-LDS loop.
 // Both half-waves execute, but only the correct prefix is carried to the next half.
 uint row=__builtin_amdgcn_workitem_id_x()&15u;float s=0.f;
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=lo[e]*lo[e];

 s=c5c_shuffle(s,row);

 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=lo[e]*lo[e];

 s=c5c_shuffle(s,row+16);

 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=hi[e]*hi[e];

 s=c5c_shuffle(s,row);

 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=hi[e]*hi[e];

 return c5c_shuffle(s,row+16);

}
// Keep C512 attention's own clamp/F/byte semantics (not the C256 bounded reciprocal).
DEV float c5c_clamp(float a,float lo,float hi){float b=a>lo?a:lo;return b<hi?b:hi;}
DEV uint c5c_fp8(float x){return uint(__builtin_amdgcn_cvt_pk_fp8_f32(__builtin_amdgcn_fmed3f(x,-448.f,448.f),0.f,0,false))&255u;}
DEV float c5c_F(float x){float c=__builtin_amdgcn_fmed3f(x+0.f,-448.f,448.f);return __builtin_amdgcn_cvt_f32_fp8(__builtin_amdgcn_cvt_pk_fp8_f32(c,0.f,0,false),0);}
DEV float c5c_inv(float x){
#if C512_COMPACT_RCP
 float r=__builtin_amdgcn_rcpf(x);r=__builtin_fmaf(r,__builtin_fmaf(-x,r,1.f),r);return __builtin_fmaf(r,__builtin_fmaf(-x,r,1.f),r);
#else
 return 1.f/x;
#endif
}
DEV uint c5c_q8mz(float a,float b){
#pragma clang fp contract(off)
 return q8(__builtin_amdgcn_fmed3f(a*b+0.f,-448.f,448.f));}
#if C512_PAIR_PACK
// Both arguments arrive after their original path-specific float operations.
DEV i2 c5c_pack8_pairs(f8 v){i2 a{};
 a[0]=__builtin_amdgcn_cvt_pk_fp8_f32(v[0],v[1],0,false);
 a[0]=__builtin_amdgcn_cvt_pk_fp8_f32(v[2],v[3],a[0],true);
 a[1]=__builtin_amdgcn_cvt_pk_fp8_f32(v[4],v[5],0,false);
 a[1]=__builtin_amdgcn_cvt_pk_fp8_f32(v[6],v[7],a[1],true);return a;}
DEV float c5c_pair_qkv_input(float a,float b){
#pragma clang fp contract(off)
 return __builtin_amdgcn_fmed3f(a*b+0.f,-448.f,448.f);}
DEV float c5c_pair_plain_input(float x){return __builtin_amdgcn_fmed3f(x,-448.f,448.f);}
#endif
DEV void c5c_bits(i2&v,uint e,uint b){v[e/4]=int(uint(v[e/4])|((b&255u)<<(8*(e%4))));}
DEV i2 c5c_load(const unsigned char*p,uint row,uint col){i2 a;__builtin_memcpy(&a,p+row*32+col,8);return a;}

template<bool DebugNorm,bool W16=false>
DEV void c5c_body(const unsigned char*in8,const float*w,unsigned char*out,uint validwidth,uint validheight,uint width,uint height,uint sx,uint sy){
 constexpr uint C=512,NW=C512_COMPACT_WAVES,QT=4/NW;
 uint tid=__builtin_amdgcn_workitem_id_x(),lane=tid&31u,row=lane&15u,half=lane>>4,wave=tid/32;
 uint wh=__builtin_amdgcn_workgroup_id_x(),windows=(width/8)*(height/8),win=wh/16,head=wh%16;
 if(width%8||height%8||wh>=windows*16)return;
 __attribute__((shared)) unsigned char packed[3*64*32];
 const unsigned char*wb=reinterpret_cast<const unsigned char*>(w);
 // Each wave owns QT independent 16-token tiles, sharing B-weight fragments.
 // Swapping WMMA operands transposes the result: lane row is now the token,
 // allowing serial channel normalization without a float scratch tile.
#if C512_COMPACT_FUSEQKV
 f8 accq[QT][6]{};
 {
  const unsigned char*ap[QT];bool av[QT];
  _Pragma("unroll") for(uint t=0;t<QT;t++){
   uint tok=(wave*QT+t)*16+row;
   int x=int((win%(width/8))*8+tok%8)-int(sx),y=int((win/(width/8))*8+tok/8)-int(sy);
   av[t]=x>=0&&y>=0&&x<int(validwidth)&&y<int(validheight);
   uint p=av[t]?uint(y)*validwidth+uint(x):0u;
   ap[t]=in8+(p/16)*16*512+(p%16)*32+half*8;
  }
#if C512_COMPACT_QKV_PIPE
  // Software pipeline: step s+1's A and B fragments are loaded before step s's 12 WMMAs, so a wave no longer
  // waits one full memory latency per K step. Same fragments, same WMMA order per accumulator -> bit-identical.
  i2 a[2][QT],b[2][6];
  auto ld=[&](uint s,uint buf){uint gs=s/2,h=s%2;
   _Pragma("unroll") for(uint t=0;t<QT;t++){a[buf][t]=i2{};if(av[t])__builtin_memcpy(&a[buf][t],ap[t]+gs*512+h*16,8);}
   _Pragma("unroll") for(uint j=0;j<6;j++){uint n0=(j/2)*C+head*32+(j%2)*16+row;__builtin_memcpy(&b[buf][j],wb+((n0/16)*16+gs)*512+((h*2+half)*16+n0%16)*8,8);}};
  ld(0,0);
  _Pragma("unroll 2") for(uint s=0;s<32;s++){
   const uint cur=s&1;
   if(s+1<32)ld(s+1,cur^1);
   _Pragma("unroll") for(uint j=0;j<6;j++)_Pragma("unroll") for(uint t=0;t<QT;t++)accq[t][j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b[cur][j],a[cur][t],accq[t][j]);
  }
#elif C512_COMPACT_QKV_DEEP
  // C512_COMPACT_QKV_DEEP=D (2026-10-01, results/c512-qkv-pipeline-20261001): hand software pipeline, D-deep register
  // ring. The 32 K steps are fully unrolled; every fragment address is one of 2+6 base pointers plus a compile-time
  // offset (immediate). Out-of-window A rows load from p=0 (a valid tile) and are zeroed by select, so no branch splits
  // the loop; the select sits at the consuming step so it does not force an early wait. Same fragments, same WMMA order per accumulator -> bit-identical.
  if constexpr(W16){
   constexpr uint D=C512_COMPACT_QKV_DEEP;
   typedef int w16_i4 __attribute__((ext_vector_type(4)));
   const unsigned char*bp[6];
   _Pragma("unroll") for(uint j=0;j<6;j++){uint n0=(j/2)*C+head*32+(j%2)*16+row;bp[j]=wb+(n0/16)*16*512+(half*16+n0%16)*16;}
   i2 a[D][QT];w16_i4 b[2][6];
   auto lda=[&](uint s){const uint gs=s/2,h=s%2,k=s%D;
    _Pragma("unroll") for(uint t=0;t<QT;t++)__builtin_memcpy(&a[k][t],ap[t]+gs*512+h*16,8);};
   auto ldb_half=[&](uint gs,uint hi){const uint kb=gs&1u;
    _Pragma("unroll") for(uint j=0;j<3;j++)__builtin_memcpy(&b[kb][hi*3+j],bp[hi*3+j]+gs*512,16);};
   _Pragma("unroll") for(uint s=0;s<D;s++)lda(s);
   ldb_half(0,0);ldb_half(0,1);ldb_half(1,0);
   _Pragma("unroll") for(uint s=0;s<32;s++){
    const uint gs=s/2,h=s%2,k=s%D,kb=gs&1u;i2 ca[QT],cb[6];
    _Pragma("unroll") for(uint t=0;t<QT;t++)ca[t]=av[t]?a[k][t]:i2{};
    _Pragma("unroll") for(uint j=0;j<6;j++)cb[j]=h?i2{b[kb][j][2],b[kb][j][3]}:i2{b[kb][j][0],b[kb][j][1]};
    if(s+D<32)lda(s+D);
    if(!h){if(gs+1<16)ldb_half(gs+1,1);}
    else{if(gs+2<16)ldb_half(gs+2,0);}
#if C512_COMPACT_QKV_SCHED
    __builtin_amdgcn_sched_barrier(0);
#endif
    _Pragma("unroll") for(uint j=0;j<6;j++)_Pragma("unroll") for(uint t=0;t<QT;t++)accq[t][j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(cb[j],ca[t],accq[t][j]);
#if C512_COMPACT_QKV_SCHED
    __builtin_amdgcn_sched_barrier(0);
#endif
   }
  }else{
   constexpr uint D=C512_COMPACT_QKV_DEEP;
   const unsigned char*bp[6];
   _Pragma("unroll") for(uint j=0;j<6;j++){uint n0=(j/2)*C+head*32+(j%2)*16+row;bp[j]=wb+(n0/16)*16*512+(half*16+n0%16)*8;}
   i2 a[D][QT],b[D][6];
   auto ld=[&](uint s){const uint gs=s/2,h=s%2,k=s%D;
    _Pragma("unroll") for(uint t=0;t<QT;t++)__builtin_memcpy(&a[k][t],ap[t]+gs*512+h*16,8);
    _Pragma("unroll") for(uint j=0;j<6;j++)__builtin_memcpy(&b[k][j],bp[j]+gs*512+h*256,8);};
   _Pragma("unroll") for(uint s=0;s<D;s++)ld(s);
   _Pragma("unroll") for(uint s=0;s<32;s++){
    const uint k=s%D;i2 ca[QT],cb[6];
    _Pragma("unroll") for(uint t=0;t<QT;t++)ca[t]=av[t]?a[k][t]:i2{};
    _Pragma("unroll") for(uint j=0;j<6;j++)cb[j]=b[k][j];
    if(s+D<32)ld(s+D);
#if C512_COMPACT_QKV_SCHED
    __builtin_amdgcn_sched_barrier(0);
#endif
    _Pragma("unroll") for(uint j=0;j<6;j++)_Pragma("unroll") for(uint t=0;t<QT;t++)accq[t][j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(cb[j],ca[t],accq[t][j]);
#if C512_COMPACT_QKV_SCHED
    __builtin_amdgcn_sched_barrier(0);
#endif
   }
  }
#else
#if C512_COMPACT_QKV_UNROLL
  // Deeper static unroll of the K loop so the scheduler can keep several steps of fragment loads in flight
  // (mochizuki's attn keeps ~36 loads outstanding). Same operations and per-accumulator order -> bit-identical.
  C5C_UNROLL(C512_COMPACT_QKV_UNROLL)
#endif
  for(uint gs=0;gs<16;gs++)for(uint h=0;h<2;h++){
   i2 a[QT];
   _Pragma("unroll") for(uint t=0;t<QT;t++){a[t]=i2{};if(av[t])__builtin_memcpy(&a[t],ap[t]+gs*512+h*16,8);}
   _Pragma("unroll") for(uint j=0;j<6;j++){
    uint n0=(j/2)*C+head*32+(j%2)*16+row;i2 b;
    __builtin_memcpy(&b,wb+((n0/16)*16+gs)*512+((h*2+half)*16+n0%16)*8,8);
    _Pragma("unroll") for(uint t=0;t<QT;t++)accq[t][j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a[t],accq[t][j]);
   }
  }
#endif
 }
 _Pragma("unroll") for(uint part=0;part<3;part++){
  f8 acc[QT][2];
  _Pragma("unroll") for(uint t=0;t<QT;t++){acc[t][0]=accq[t][part*2];acc[t][1]=accq[t][part*2+1];}
#else
 _Pragma("clang loop unroll(disable)") for(uint part=0;part<3;part++){
  f8 acc[QT][2]{};
  for(uint gs=0;gs<16;gs++)for(uint h=0;h<2;h++){
   i2 a[QT];
   _Pragma("unroll") for(uint t=0;t<QT;t++){
    uint tok=(wave*QT+t)*16+row;
    int x=int((win%(width/8))*8+tok%8)-int(sx),y=int((win/(width/8))*8+tok/8)-int(sy);
    a[t]=i2{};
    if(x>=0&&y>=0&&x<int(validwidth)&&y<int(validheight)){
     uint p=uint(y)*validwidth+uint(x);
     __builtin_memcpy(&a[t],in8+((p/16)*16+gs)*512+(p%16)*32+half*8+h*16,8);
    }
   }
   _Pragma("unroll 2") for(uint ci=0;ci<2;ci++){
    uint n0=part*C+head*32+ci*16+row;i2 b;
    __builtin_memcpy(&b,wb+((n0/16)*16+gs)*512+((h*2+half)*16+n0%16)*8,8);
    _Pragma("unroll") for(uint t=0;t<QT;t++)acc[t][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a[t],acc[t][ci]);
   }
  }
#endif
  _Pragma("unroll") for(uint t=0;t<QT;t++){
   float inv=1.f;
   if(part<2){float ss=(C512_COMPACT_ABLATE&1)?acc[t][0][0]:c5c_serial_norm(acc[t][0],acc[t][1]);inv=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?w[4*C*C+16*4096+head]:1.f);}
   _Pragma("unroll 2") for(uint ci=0;ci<2;ci++){
#if C512_PAIR_PACK
    f8 qv{};_Pragma("unroll 8") for(uint e=0;e<8;e++){
#if C512_COMPACT_NOF
     qv[e]=c5c_pair_qkv_input(acc[t][ci][e],inv);
#else
     qv[e]=F(acc[t][ci][e]*inv);
#endif
    }i2 a=c5c_pack8_pairs(qv);
#else
    i2 a{};_Pragma("unroll 8") for(uint e=0;e<8;e++)
#if C512_COMPACT_NOF
     c5c_bits(a,e,c5c_q8mz(acc[t][ci][e],inv));
#else
     c5c_bits(a,e,q8(F(acc[t][ci][e]*inv)));
#endif
#endif

    uint tok=(wave*QT+t)*16+row;
#if C512_COMPACT_VT
    if(!DebugNorm&&part==2){_Pragma("unroll 8") for(uint e=0;e<8;e++)packed[128*32+(ci*16+half*8+e)*64+tok]=(unsigned char)((uint(a[e/4])>>(8*(e%4)))&255u);}else
#endif
    __builtin_memcpy(packed+(part*64+tok)*32+ci*16+half*8,&a,8);
   }
  }
 }
 WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);
 if constexpr((C512_COMPACT_ABLATE&2)!=0){out[tid]=packed[tid*7];return;}
 if constexpr(DebugNorm){
  for(uint i=tid;i<3*64*32;i+=NW*32){uint part=i/(64*32),tok=(i/32)%64,col=i%32,p=c5c_pixel(win,tok,width);out[(p*3+part)*512+head*32+col]=packed[i];}
  return;
 }
 // Packed Q/K/V is now read-only. Each wave evaluates its own query tiles,
 // retaining ex/prob fragments in registers; no second workgroup barrier.
 _Pragma("clang loop unroll(disable)") for(uint t=0;t<QT;t++){
  uint first=(wave*QT+t)*16;
  h8 exponent[4];
  _Pragma("unroll 4") for(uint key=0;key<4;key++){
   f8 scores{};
   _Pragma("unroll 2") for(uint kt=0;kt<2;kt++){
    i2 q=c5c_load(packed,first+row,kt*16+half*8);
    i2 k=c5c_load(packed,64+key*16+row,kt*16+half*8);
    scores=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(k,q,scores);
   }
   h8 ex{};
   _Pragma("unroll 8") for(uint e=0;e<8;e++){
    uint q=first+row,k=key*16+half*8+e;
    float score=scores[e]+w[4*C*C+head*4096+q*64+k];
    float affine=c5c_clamp(score*.044921875f+1.30078125f,1.03125f,1.5693359375f);
    uint ah=(bits(affine)>>13)-0x1c000u;
    unsigned short hb=static_cast<unsigned short>(((ah<<5)+0x8000u)&65535u);ex[e]=__builtin_bit_cast(_Float16,hb);
   }
   exponent[key]=ex;
  }
  h8 ones{};_Pragma("unroll 8") for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;
  f8 sums[2]{};
  _Pragma("unroll 2") for(uint side=0;side<2;side++){
   _Pragma("unroll 2") for(uint h=0;h<2;h++)sums[side]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,exponent[side+2*h],sums[side]);
  }
  i2 prob[4];
  _Pragma("unroll 4") for(uint key=0;key<4;key++){
#if C512_PAIR_PACK
   f8 pv{};_Pragma("unroll 8") for(uint e=0;e<8;e++){float inv=c5c_inv(sums[0][e]+sums[1][e]);pv[e]=c5c_pair_plain_input(float(exponent[key][e])*inv);}prob[key]=c5c_pack8_pairs(pv);
#else
   i2 a{};_Pragma("unroll 8") for(uint e=0;e<8;e++){float inv=c5c_inv(sums[0][e]+sums[1][e]);c5c_bits(a,e,c5c_fp8(float(exponent[key][e])*inv));}prob[key]=a;
#endif
  }
  _Pragma("unroll 2") for(uint ci=0;ci<2;ci++){
   f8 av{};
   _Pragma("unroll 4") for(uint kt=0;kt<4;kt++){
#if C512_COMPACT_VT
    i2 v;__builtin_memcpy(&v,packed+128*32+(ci*16+row)*64+kt*16+half*8,8);
#else
    i2 v{};_Pragma("unroll 8") for(uint e=0;e<8;e++)c5c_bits(v,e,packed[(128+kt*16+half*8+e)*32+ci*16+row]);
#endif
    av=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(v,prob[kt],av);
   }
#if C512_PAIR_PACK
   f8 bv{};_Pragma("unroll 8") for(uint e=0;e<8;e++){
#if C512_COMPACT_NOF
    bv[e]=c5c_pair_plain_input(av[e]);
#else
    bv[e]=c5c_pair_plain_input(c5c_F(av[e]));
#endif
   }i2 bytes=c5c_pack8_pairs(bv);
#else
   i2 bytes{};_Pragma("unroll 8") for(uint e=0;e<8;e++)
#if C512_COMPACT_NOF
    c5c_bits(bytes,e,c5c_fp8(av[e]));
#else
    c5c_bits(bytes,e,c5c_fp8(c5c_F(av[e])));
#endif
#endif

   uint tok=first+row;int x=int((win%(width/8))*8+tok%8)-int(sx),y=int((win/(width/8))*8+tok/8)-int(sy);
   if(x>=0&&y>=0&&x<int(validwidth)&&y<int(validheight)){
    uint p=uint(y)*validwidth+uint(x);__builtin_memcpy(out+p*512+head*32+ci*16+half*8,&bytes,8);
   }
  }
 }
}
KERNEL __attribute__((amdgpu_flat_work_group_size(C512_COMPACT_WAVES*32,C512_COMPACT_WAVES*32)))
void c512_qkv_attention_compact(const unsigned char*in8,const float*w,unsigned char*out,uint validwidth,uint validheight,uint workwidth,uint workheight,uint sx,uint sy){c5c_body<false,false>(in8,w,out,validwidth,validheight,workwidth,workheight,sx,sy);}
KERNEL __attribute__((amdgpu_flat_work_group_size(C512_COMPACT_WAVES*32,C512_COMPACT_WAVES*32)))
void c512_qkv_attention_compact_w16(const unsigned char*in8,const float*w,unsigned char*out,uint validwidth,uint validheight,uint workwidth,uint workheight,uint sx,uint sy){c5c_body<false,true>(in8,w,out,validwidth,validheight,workwidth,workheight,sx,sy);}
#endif
