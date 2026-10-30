// The exponent bit map bounds each term to [2^-14, 9.75]; either
// fixed WMMA sum order therefore stays in [1/256,624]. Keep both correction
// steps from C32's exhaustively checked norm_inverse; never use bare rcp.
#ifndef W2_BOUNDED_RCP
#define W2_BOUNDED_RCP 0
#endif
DEV float w2_inverse(float x){
#if W2_FAST_NUM&2
 return __builtin_amdgcn_rcpf(x);
#elif W2_BOUNDED_RCP
 float r=__builtin_amdgcn_rcpf(x);
 r=__builtin_fmaf(r,__builtin_fmaf(-x,r,1.f),r);
 return __builtin_fmaf(r,__builtin_fmaf(-x,r,1.f),r);
#else
 return 1.f/x;
#endif
}
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
#ifndef W2_DIRECT_COORDS
#define W2_DIRECT_COORDS 0 /* workw is a multiple of 8: avoid flattening a tile coordinate only to divide it back */
#endif
#ifndef W2_BYTE_INPUT_LOADS
#define W2_BYTE_INPUT_LOADS 0
#endif
#ifndef W2_HOIST_LOADS
#define W2_HOIST_LOADS 0 /* hoist-loads 2026-09-30, bitmask; loads issued branch-free (out-of-range index clamped to an in-range one, value selected to 0 after):
 1 input staging (8 x b64), 2 FFN residual input bytes, 4 Up low-res input per k step, 8 Up skip reads. ISA of the conditional form: one global_load -> s_wait_loadcnt 0 per branch */
#endif
#ifndef W2_RTZ_PAIR
#define W2_RTZ_PAIR 0
#endif
#ifndef W2_FULL_WINDOW
#define W2_FULL_WINDOW 0
#endif
// C256 FFN token-tile batching. Zero retains the sequential path; 2/4 share
// weight loads across independent accumulators without changing reduction order.
// Optional reuse at smaller channel counts: bit0=C64, bit1=C128.
#ifndef W2_FFN_QT_SMALL_MASK
#define W2_FFN_QT_SMALL_MASK 0
#endif
#if W2_FFN_QT_SMALL_MASK < 0 || W2_FFN_QT_SMALL_MASK > 3
#error W2_FFN_QT_SMALL_MASK must be 0..3
#endif
#ifndef W2_FFN_QT_BATCH
#define W2_FFN_QT_BATCH 0
#endif
#if W2_FFN_QT_BATCH != 0 && W2_FFN_QT_BATCH != 2 && W2_FFN_QT_BATCH != 4
#error W2_FFN_QT_BATCH must be 0, 2 or 4
#endif
struct w2_true{constexpr operator bool()const{return true;}};
struct w2_false{constexpr operator bool()const{return false;}};
DEV uint w2_canonical_byte_word(uint x){return x|(((x&0x7f7f7f7fu)+0x01010101u)&0x80808080u);}
template<uint C> DEV i2 w2_input_bytes_nb(const float*in,uint win,uint tok,uint col,uint workw,uint width,uint height,uint sx,uint sy){
#if W2_DIRECT_COORDS
 int x=int((win%(workw/8))*8+tok%8)-int(sx),y=int((win/(workw/8))*8+tok/8)-int(sy);
#else
 uint p=w2_pixel(win,tok,workw);int x=int(p%workw)-int(sx),y=int(p/workw)-int(sy);
#endif
 bool ok=x>=0&&y>=0&&x<int(width)&&y<int(height);i2 a;
 __builtin_memcpy(&a,reinterpret_cast<const unsigned char*>(in)+(ok?(uint(y)*width+uint(x))*C+col:col),8);
 a[0]=ok?a[0]:0;a[1]=ok?a[1]:0;return a;
}
template<uint C> DEV i2 w2_input_bytes(const float*in,uint win,uint tok,uint col,uint workw,uint width,uint height,uint sx,uint sy){
#if W2_DIRECT_COORDS
 int x=int((win%(workw/8))*8+tok%8)-int(sx),y=int((win/(workw/8))*8+tok/8)-int(sy);i2 a{};
#else
 uint p=w2_pixel(win,tok,workw);int x=int(p%workw)-int(sx),y=int(p/workw)-int(sy);i2 a{};
#endif
 if(x>=0&&y>=0&&x<int(width)&&y<int(height))__builtin_memcpy(&a,reinterpret_cast<const unsigned char*>(in)+(uint(y)*width+uint(x))*C+col,8);
 return a;
}
#if W2_RTZ_PAIR
using w2_h2=_Float16 __attribute__((ext_vector_type(2)));
DEV f8 w2_rtz8(f8 x){f8 out;_Pragma("unroll") for(uint j=0;j<4;j++){uint packed;asm("v_cvt_pkrtz_f16_f32 %0, %1, %2" : "=v"(packed):"v"(x[2*j]),"v"(x[2*j+1]));w2_h2 pair=__builtin_bit_cast(w2_h2,packed);out[2*j]=float(pair[0]);out[2*j+1]=float(pair[1]);}return out;}
#endif
// W2_Q8_MASK (2026-09-30, results/composite-quant-20260930): at the byte sites whose value is a pure FP8xFP8 WMMA sum from +0
// (FFN contract, attention output) Q8(Hrtz(x)) becomes Q8(bits(x)&0xffffe000): one v_and_b32 instead of v_cvt_pkrtz_f16_f32
// + v_cvt_f32_f16. Exhaustive over all 2^32 patterns with the production instructions (+0, MODE.FP16_OVFL v_cvt_pk_fp8_f32):
// equal bytes except negative |x|<2^-24 and 8191 +NaN payloads; a WMMA FP8 sum is 0 or a multiple of 2^-18, finite, so neither
// reaches these sites. The half rounding itself is kept (1,032,066 in-domain inputs round differently without it).
#ifndef W2_Q8_MASK
#define W2_Q8_MASK 0
#endif
DEV float w2_hmask(float x){return __builtin_bit_cast(float,__builtin_bit_cast(uint,x)&0xffffe000u);}
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

// W2_FFN_W16 (2026-09-30, results/c256-w16-20260930): C256 FFN weights in the wide fragment layout packed by the host
// (FragmentPackedMatrixW16, key @ffn-frag-w16): inside each 512-byte tile a lane's two K16 fragments are adjacent, so one
// 16-byte load feeds two WMMAs. Same bytes, same per-accumulator k order -> bit-identical. The wide read is a template flag
// exported under new names (c256_wave2*_w16, sp_run256_w16/sp_recover256_w16); the original exports keep the old layout, so
// old host + new module and new host + old module both stay on @ffn-frag. The host uses the _w16 names only when present.
#ifndef W2_FFN_W16
#define W2_FFN_W16 0
#endif
#if W2_FFN_W16 && (!W2_FRAGMENT_WEIGHTS || W2_FFN_QT_BATCH==0 || W2_HIDDEN_TILES%2)
#error W2_FFN_W16 needs W2_FRAGMENT_WEIGHTS, W2_FFN_QT_BATCH and an even W2_HIDDEN_TILES
#endif
using w2_i4=int __attribute__((ext_vector_type(4)));
DEV w2_i4 w2_weight16(const float*p,uint row,uint stride,uint k,uint group=w2_gr()){w2_i4 a; /* k%32==0 */
 __builtin_memcpy(&a,reinterpret_cast<const unsigned char*>(p)+((row/16)*(stride/32)+k/32)*512+(group*16+row%16)*16,16);return a;}
DEV void w2_bits(i2&a,uint e,uint v){a[e/4]=int(uint(a[e/4])|((v&255u)<<(8*(e%4))));}
// W2_PACK8 (2026-09-27, results/c64-block-fused-20260927): the same E4M3 bytes as q8_fused_round, two values per
// v_cvt_pk_fp8_f32 written straight into the fragment word. +0.f after the clamp maps -0 to +0 (the old select on |x|==0)
// and leaves every other input unchanged, NaN included; it follows the clamp so it cannot contract into a preceding mul.
#ifndef W2_PACK8
#define W2_PACK8 0
#endif
#if W2_PACK8
#if W2_PACK8==6
// 6 = 4, but where the packed value is a product x*y the multiply and the -0 -> +0 add become one fma(x,y,+0) (W2_Q8_SETF):
// one rounding of the exact product plus an exact zero is the multiply's rounding, and the sum is never -0, so the bits equal
// mul-then-add-0. LLVM does not contract the pair itself (results/c64-hand-asm-20260927, found by hand-editing the ISA). The
// +0 is load-bearing: the FFN activation polynomial is exactly +0 at the clamp g=-4, so every x<=-4 gives x*poly = -0.
// Other stored values get the +0 at store time (w2_z0), outside the MODE segment.
DEV float w2_z(float x){return x;}
DEV float w2_z0(float x){return x+0.f;}
#define W2_OVFL(v) __builtin_amdgcn_s_setreg(1|(23<<6),(v))
#elif W2_PACK8==4
// 4 = segmented MODE.FP16_OVFL (as ACO does in mochizuki's fswin): saturation on only for the four conversions of one
// fragment, so v_cvt_pk_fp8_f32 clamps finite overflow to +-448 itself and the med3 goes. No f16 narrowing runs inside the
// intended segment. LLVM can hoist QKV FP8 conversions out of these intrinsic MODE writes (mh-round1 ISA audit);
// inspect the generated intervals, not only this source. The existing corpus range census covers these sites. Differs from the clamp for
// +-Inf/NaN inputs (E4M3 NaN byte instead of +-448/-448).
DEV float w2_z(float x){return x+0.f;}
#define W2_OVFL(v) __builtin_amdgcn_s_setreg(1|(23<<6),(v))
#elif W2_PACK8==5
// 5 = census probe, never production: as 3, but any input that is non-finite (W2_CENSUS_T 0) or has |x| > W2_CENSUS_T
// becomes 123.0 (E4M3 0x5F rounds it to 120). A set that stays bit-identical to 3 on the regression corpus proves no such
// input reached a C64-C256 pack there (results/ovfl-census-20260927).
#ifndef W2_CENSUS_T
#define W2_CENSUS_T 0
#endif
DEV float w2_z(float x){bool hit=W2_CENSUS_T?!(__builtin_fabsf(x)<=float(W2_CENSUS_T)):!__builtin_isfinite(x);return hit?123.f:__builtin_amdgcn_fmed3f(x+0.f,-448.f,448.f);}
#elif W2_PACK8==3
// 3 = 2 with the clamp as one v_med3_f32 (__builtin_amdgcn_fmed3f) instead of fminf/fmaxf, which the compiler lowers with an
// extra v_max_num(x,x) canonicalisation; same byte for every input, NaN -> -448 (probe in experiments/fp8-sat-mode).
DEV float w2_z(float x){return __builtin_amdgcn_fmed3f(x+0.f,-448.f,448.f);}
#elif W2_PACK8==2
// +0.f before the clamp: clamp(x+0) == clamp(x)+0 for every x (only -0 changes, to +0; x+0 is never -0 under RNE) and lets
// a preceding multiply contract into fma(a,b,+0), which rounds exactly like the multiply (results/transpose-persist-20260927).
DEV float w2_z(float x){return __builtin_amdgcn_fmed3f(x+0.f,-448.f,448.f);}
#else
DEV float w2_z(float x){return __builtin_amdgcn_fmed3f(x,-448.f,448.f)+0.f;}
#endif
#ifndef W2_OVFL
#define W2_OVFL(v) (void)0
#endif
DEV i2 w2_pack8(const float*t){W2_OVFL(1);int lo=__builtin_amdgcn_cvt_pk_fp8_f32(w2_z(t[0]),w2_z(t[1]),0,false);lo=__builtin_amdgcn_cvt_pk_fp8_f32(w2_z(t[2]),w2_z(t[3]),lo,true);
 int hi=__builtin_amdgcn_cvt_pk_fp8_f32(w2_z(t[4]),w2_z(t[5]),0,false);hi=__builtin_amdgcn_cvt_pk_fp8_f32(w2_z(t[6]),w2_z(t[7]),hi,true);W2_OVFL(0);i2 a;a[0]=lo;a[1]=hi;return a;}
#define W2_Q8_DECL(A) i2 A;float A##_t[8]
#if W2_PACK8==6
#define W2_Q8_SET(A,E,X) A##_t[E]=w2_z0(X)
#define W2_Q8_SETF(A,E,X,Y) A##_t[E]=__builtin_fmaf((X),(Y),0.f)
#else
#define W2_Q8_SET(A,E,X) A##_t[E]=(X)
#define W2_Q8_SETF(A,E,X,Y) W2_Q8_SET(A,E,(X)*(Y))
#endif
#define W2_Q8_END(A) A=w2_pack8(A##_t)
#else
#define W2_Q8_DECL(A) i2 A{}
#define W2_Q8_SET(A,E,X) w2_bits(A,E,q8_fused_round(X))
#define W2_Q8_SETF(A,E,X,Y) W2_Q8_SET(A,E,(X)*(Y))
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

#ifndef W2_UP_FUSED
#define W2_UP_FUSED 0
#endif
// Exact deep_fast branchless F, including its signed-zero behavior.
DEV float w2_up_F(float x){float c=__builtin_amdgcn_fmed3f(x+0.f,-448.f,448.f);return __builtin_amdgcn_cvt_f32_fp8(__builtin_amdgcn_cvt_pk_fp8_f32(c,0.f,0,false),0);}
// f-sweep 2026-09-30: W2_UP_ADD0 1 -> up output byte cvt_pk_fp8(med3(merged+0)) instead of cvt_pk_fp8(w2_up_F(merged))
// (same byte for all 2^32 inputs, results/f-sweep-20260930).
#ifndef W2_QKV_FUSE
#define W2_QKV_FUSE 0 /* c128-phase: fused Q/K/V kt loop (bitmask 1 C64, 2 C128, 4 C256), bit-exact */
#endif
#ifndef W2_PHASE_REP
#define W2_PHASE_REP 0 /* c128-phase 2026-10-01 timing probe only: repeat one phase of c128_wave2_bi_bo (1 FFN expand/contract, 2 mix+residual, 4 QKV, 8 attention, 16 output, 32 staging); output unchanged, never in a recipe */
#endif
#ifndef W2_UP_DIRECT
#define W2_UP_DIRECT 0 /* c128-c64-inchain 2026-10-01: Up projection with high-res B columns, no shuffles; bit-exact */
#endif
#ifndef W2_UP_VEC
#define W2_UP_VEC 0 /* c128-c64-inchain 2026-10-01: vectorized/packed Up epilogue, bit-exact; see swin_wave2_body */
#endif
#ifndef W2_UP_ADD0
#define W2_UP_ADD0 0
#endif
DEV float w2_add0(float x){
#pragma clang fp contract(off)
 return __builtin_amdgcn_fmed3f(x+0.f,-448.f,448.f);}
#ifndef W2_EXPLICIT_WINDOW
#define W2_EXPLICIT_WINDOW 0
#endif
/* W2_DOWN_HALF (2026-10-01, swin-body-gap): Up skip read as f16 when the encoder tail stored Hrtz(v) (f16-exact) as half. */
template<bool Half> DEV float w2_skip(const float*skip,uint i){if constexpr(Half)return float(reinterpret_cast<const _Float16*>(skip)[i]);else return skip[i];}
/* W2_SKIP_BYTE (2026-10-01, c128-c64-inchain): the encoder tail also stores the byte of w2_up_F(Hrtz(v)) (0 for +-0, as F); the Up reads it
   back as cvt_f32_fp8(byte) == w2_up_F(skip) and skips its own F. */
DEV uint w2_skip_byte(float x){float c=__builtin_amdgcn_fmed3f(x+0.f,-448.f,448.f);return uint(__builtin_amdgcn_cvt_pk_fp8_f32(c,0.f,0,false))&255u;}
template<bool Byte> DEV float w2_skipF(float v){if constexpr(Byte)return v;else return w2_up_F(v);}
template<uint C,bool ByteIn,bool ByteOut,bool Up=false
#if W2_EXPLICIT_WINDOW
 ,bool ExplicitWindow=false
#endif
 ,bool W16=false,bool LowByte=false,bool HalfOut=false,bool SkipHalf=false,bool SkipByte=false,bool SkipByteOut=false>
DEV void swin_wave2_body(const float*in,const float*fw,const float*aw,void*out_any,
 uint width,uint height,uint workw,uint workh,uint sx,uint sy,uint post,const float*upw=nullptr,const float*skip=nullptr
#if W2_EXPLICIT_WINDOW
 ,uint supplied_win=0
#endif
 ){
#if W2_EXPLICIT_WINDOW
 const uint win=ExplicitWindow?supplied_win:__builtin_amdgcn_workgroup_id_x(),head=__builtin_amdgcn_workitem_id_x()/32;
#else
 const uint win=__builtin_amdgcn_workgroup_id_x(),head=__builtin_amdgcn_workitem_id_x()/32;
#endif
 const uint rc=w2_rc(),gr=w2_gr();
 if(win>=(workw/8)*(workh/8))return;
 __attribute__((shared)) unsigned char plane0[64*C],plane1[64*C];
 auto input=[&](uint tok,uint col)->float{
  if constexpr(Up){
   uint ci=(col%32)/16,e=col%8,qt=tok/16;
   i2 a=w2_load<C>(plane0,qt,head*2+ci);uint byte=(uint(a[e/4])>>(8*(e%4)))&255u;
   return __builtin_amdgcn_cvt_f32_fp8(int(byte),0);
  }

#if W2_DIRECT_COORDS
  int x=int((win%(workw/8))*8+tok%8)-int(sx),y=int((win/(workw/8))*8+tok/8)-int(sy);
#else
  uint p=w2_pixel(win,tok,workw);int x=int(p%workw)-int(sx),y=int(p/workw)-int(sy);
#endif
  bool valid=x>=0&&y>=0&&x<int(width)&&y<int(height);uint idx=valid?(uint(y)*width+uint(x))*C+col:col;
  float v;if constexpr(ByteIn)v=__builtin_amdgcn_cvt_f32_fp8(int(reinterpret_cast<const unsigned char*>(in)[idx]),0);else v=in[idx];
  return valid?v:0.f;
 };
 // For fused Up, even window shifts preserve disjoint 4x4 low-resolution tiles.
 if constexpr(Up){
  const uint IC=2*C;const int x0=int((win%(workw/8))*8)-int(sx),y0=int((win/(workw/8))*8)-int(sy);
#if W2_UP_DIRECT
  /* W2_UP_DIRECT (2026-10-01, c128-c64-inchain): the low-res projection is computed with B columns = the 16 high-res tokens
     of each qt (column rc reads its source low token qt*4+(rc%8)/2), so each lane already holds the value the scalar path
     shuffled in from lane low+gr*16: no ds_bpermute at all. A WMMA column depends only on its own B column (same k order,
     same accumulator chain), so every result is bit-identical; cost is 4x the (tiny) f16 WMMA count. */
  const uint iw=width/2,ih=height/2;
  f8 tq[4][2]{};
  for(uint k=0;k<IC;k+=16){h8 b[2];
   _Pragma("unroll") for(uint ci=0;ci<2;ci++)__builtin_memcpy(&b[ci],reinterpret_cast<const _Float16*>(upw)+(head*32+ci*16+rc)*IC+k+gr*8,16);
   _Pragma("unroll") for(uint qt=0;qt<4;qt++){
    const uint L=qt*4+(rc%8)/2;const int lx=x0/2+int(L%4),ly=y0/2+int(L/4);h8 a{};
    {const bool ok=lx>=0&&ly>=0&&lx<int(iw)&&ly<int(ih);const uint base=ok?(uint(ly)*iw+uint(lx))*IC:0u;for(uint e=0;e<8;e++){float v;if constexpr(LowByte)v=__builtin_amdgcn_cvt_f32_fp8(int(reinterpret_cast<const unsigned char*>(in)[base+k+gr*8+e]),0);else v=in[base+k+gr*8+e];a[e]=ok?(_Float16)v:(_Float16)0.f;}}
    _Pragma("unroll") for(uint ci=0;ci<2;ci++)tq[qt][ci]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(b[ci],a,tq[qt][ci]);
   }
  }
  float uw[2][8];
  _Pragma("unroll") for(uint ci=0;ci<2;ci++)_Pragma("unroll") for(uint e=0;e<8;e++)uw[ci][e]=upw[IC*C+head*32+ci*16+gr*8+e];
  _Pragma("unroll") for(uint qt=0;qt<4;qt++){
   const uint tok=qt*16+rc;const int x=x0+int(tok%8),y=y0+int(tok/8);
   const bool ok=x>=0&&y>=0&&x<int(width)&&y<int(height);const uint base=ok?(uint(y)*width+uint(x))*C:0u;
   _Pragma("unroll") for(uint ci=0;ci<2;ci++){
    const uint c0=head*32+ci*16+gr*8;float sk[8];
    if constexpr(SkipByte){uint wv[2];__builtin_memcpy(wv,reinterpret_cast<const unsigned char*>(skip)+base+c0,8);_Pragma("unroll") for(uint e=0;e<8;e++)sk[e]=__builtin_amdgcn_cvt_f32_fp8(int((wv[e/4]>>(8*(e%4)))&255u),0);}
    else if constexpr(SkipHalf){h8 v;__builtin_memcpy(&v,reinterpret_cast<const _Float16*>(skip)+base+c0,16);_Pragma("unroll") for(uint e=0;e<8;e++)sk[e]=float(v[e]);}
    else __builtin_memcpy(sk,skip+base+c0,32);
    i2 packed{};
    _Pragma("unroll") for(uint e=0;e<8;e++){
     float t=H(tq[qt][ci][e]);
     float merged=H(t+w2_skipF<SkipByte>(sk[e])*uw[ci][e]);
     uint b=uint(__builtin_amdgcn_cvt_pk_fp8_f32(W2_UP_ADD0?w2_add0(merged):w2_up_F(merged),0.f,0,false))&255u;
     w2_bits(packed,e,ok?b:0u);}
    w2_store<C>(plane0,qt,head*2+ci,packed);
   }
  }
#else
  const int lx=x0/2+int(rc%4),ly=y0/2+int(rc/4);const uint iw=width/2,ih=height/2;
  f8 totals[2]{};
  for(uint k=0;k<IC;k+=16){h8 a{},b{};
#if W2_HOIST_LOADS&4
   {const bool ok=lx>=0&&ly>=0&&lx<int(iw)&&ly<int(ih);const uint base=ok?(uint(ly)*iw+uint(lx))*IC:0u;for(uint e=0;e<8;e++){float v;if constexpr(LowByte)v=__builtin_amdgcn_cvt_f32_fp8(int(reinterpret_cast<const unsigned char*>(in)[base+k+gr*8+e]),0);else v=in[base+k+gr*8+e];a[e]=ok?(_Float16)v:(_Float16)0.f;}}
#else
   if(lx>=0&&ly>=0&&lx<int(iw)&&ly<int(ih))for(uint e=0;e<8;e++){const uint i=(uint(ly)*iw+uint(lx))*IC+k+gr*8+e;if constexpr(LowByte)a[e]=(_Float16)__builtin_amdgcn_cvt_f32_fp8(int(reinterpret_cast<const unsigned char*>(in)[i]),0);else a[e]=(_Float16)in[i];}
#endif
   for(uint ci=0;ci<2;ci++){
    __builtin_memcpy(&b,reinterpret_cast<const _Float16*>(upw)+(head*32+ci*16+rc)*IC+k+gr*8,16);
    totals[ci]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(b,a,totals[ci]);
   }
  }
#if W2_UP_VEC && defined(HIP_ISA_HALF)
  /* W2_UP_VEC (2026-10-01, c128-c64-inchain): same values, fewer instructions in the Up epilogue.
     H(shuffle(t)) == shuffle-of-the-f16-bits then v_cvt_f32_f16 (H is exactly those two ISA converts under HIP_ISA_HALF,
     the shuffle is a pure move), so two f16 results travel per ds_bpermute (32 instead of 64). Skip is read as one
     16-byte (f16) / two 16-byte (f32) vectors per (qt,ci) instead of 8 scalar loads, upw is loaded once instead of per qt.
     The merge expression H(t+F(skip)*upw) and the byte select are written exactly as the scalar path. */
  static_assert(sizeof(float)==4,"");
  uint hp[2][4];
  _Pragma("unroll") for(uint ci=0;ci<2;ci++)_Pragma("unroll") for(uint j=0;j<4;j++){uint lo,hi;
   asm("v_cvt_f16_f32 %0, %1" : "=v"(lo) : "v"(totals[ci][2*j]));asm("v_cvt_f16_f32 %0, %1" : "=v"(hi) : "v"(totals[ci][2*j+1]));
   hp[ci][j]=(lo&0xffffu)|(hi<<16);}
  float uw[2][8];
  _Pragma("unroll") for(uint ci=0;ci<2;ci++)_Pragma("unroll") for(uint e=0;e<8;e++)uw[ci][e]=upw[IC*C+head*32+ci*16+gr*8+e];
  _Pragma("clang loop unroll(disable)") for(uint qt=0;qt<4;qt++){
   const uint tok=qt*16+rc,low=(tok/16)*4+(tok%8)/2;const int x=x0+int(tok%8),y=y0+int(tok/8);
   const bool ok=x>=0&&y>=0&&x<int(width)&&y<int(height);const uint base=ok?(uint(y)*width+uint(x))*C:0u;
   _Pragma("unroll") for(uint ci=0;ci<2;ci++){
    const uint c0=head*32+ci*16+gr*8;float sk[8];
    if constexpr(SkipByte){uint wv[2];__builtin_memcpy(wv,reinterpret_cast<const unsigned char*>(skip)+base+c0,8);_Pragma("unroll") for(uint e=0;e<8;e++)sk[e]=__builtin_amdgcn_cvt_f32_fp8(int((wv[e/4]>>(8*(e%4)))&255u),0);}
    else if constexpr(SkipHalf){h8 v;__builtin_memcpy(&v,reinterpret_cast<const _Float16*>(skip)+base+c0,16);_Pragma("unroll") for(uint e=0;e<8;e++)sk[e]=float(v[e]);}
    else __builtin_memcpy(sk,skip+base+c0,32);
    i2 packed{};
    _Pragma("unroll") for(uint j=0;j<4;j++){
     const uint w=uint(__builtin_amdgcn_ds_bpermute(int((low+gr*16)*4),int(hp[ci][j])));
     _Pragma("unroll") for(uint k=0;k<2;k++){const uint e=2*j+k;float t;const uint hb=k?(w>>16):(w&0xffffu);
      asm("v_cvt_f32_f16 %0, %1" : "=v"(t) : "v"(hb));
      float merged=H(t+w2_skipF<SkipByte>(sk[e])*uw[ci][e]);
      uint b=uint(__builtin_amdgcn_cvt_pk_fp8_f32(W2_UP_ADD0?w2_add0(merged):w2_up_F(merged),0.f,0,false))&255u;
      w2_bits(packed,e,ok?b:0u);}
    }
    w2_store<C>(plane0,qt,head*2+ci,packed);
   }
  }
#else
  for(uint qt=0;qt<4;qt++)for(uint ci=0;ci<2;ci++){
   uint tok=qt*16+rc,low=(tok/16)*4+(tok%8)/2;int x=x0+int(tok%8),y=y0+int(tok/8);i2 packed{};
   for(uint e=0;e<8;e++){
    float t=H(w2_shuffle(totals[ci][e],low+gr*16));uint c=head*32+ci*16+gr*8+e;uint byte=0;
#if W2_HOIST_LOADS&8
    {const bool ok=x>=0&&y>=0&&x<int(width)&&y<int(height);
     float merged=H(t+w2_skipF<SkipByte>(SkipByte?__builtin_amdgcn_cvt_f32_fp8(int(reinterpret_cast<const unsigned char*>(skip)[(ok?(uint(y)*width+uint(x))*C:0u)+c]),0):w2_skip<SkipHalf>(skip,(ok?(uint(y)*width+uint(x))*C:0u)+c))*upw[IC*C+c]);
     uint b=uint(__builtin_amdgcn_cvt_pk_fp8_f32(W2_UP_ADD0?w2_add0(merged):w2_up_F(merged),0.f,0,false))&255u;byte=ok?b:0u;}
#else
    if(x>=0&&y>=0&&x<int(width)&&y<int(height)){
     float merged=H(t+w2_skipF<SkipByte>(SkipByte?__builtin_amdgcn_cvt_f32_fp8(int(reinterpret_cast<const unsigned char*>(skip)[(uint(y)*width+uint(x))*C+c]),0):w2_skip<SkipHalf>(skip,(uint(y)*width+uint(x))*C+c))*upw[IC*C+c]);
     byte=uint(__builtin_amdgcn_cvt_pk_fp8_f32(W2_UP_ADD0?w2_add0(merged):w2_up_F(merged),0.f,0,false))&255u;
    }
#endif
    w2_bits(packed,e,byte);
   }
   w2_store<C>(plane0,qt,head*2+ci,packed);
  }
#endif
#endif /* W2_UP_DIRECT */
 }else{
 // Stage original input in A fragments. Preserve the byte cast used by pack4.
#undef W2_REP_BEGIN
#undef W2_REP_END
#if W2_PHASE_REP && !defined(W2_ATTENTION_ONLY)
#define W2_REP_BEGIN(bit) for(uint w2rep_##bit=0;w2rep_##bit<(((W2_PHASE_REP&(bit))&&C==128&&!Up&&ByteIn&&ByteOut&&!W16&&!HalfOut)?2u:1u);w2rep_##bit++){if constexpr((W2_PHASE_REP&(bit))&&C==128&&!Up&&ByteIn&&ByteOut&&!W16&&!HalfOut)__asm__ volatile("":::"memory");
#else
#define W2_REP_BEGIN(bit) {
#endif
#define W2_REP_END }
W2_REP_BEGIN(32)
 for(uint qt=0;qt<4;qt++)for(uint ci=0;ci<2;ci++){
  i2 a{};
#if W2_BYTE_INPUT_LOADS
  if constexpr(ByteIn){a=(W2_HOIST_LOADS&1)?w2_input_bytes_nb<C>(in,win,qt*16+rc,head*32+ci*16+gr*8,workw,width,height,sx,sy):w2_input_bytes<C>(in,win,qt*16+rc,head*32+ci*16+gr*8,workw,width,height,sx,sy);a[0]=int(w2_canonical_byte_word(uint(a[0])));a[1]=int(w2_canonical_byte_word(uint(a[1])));}
  else
#endif
  for(uint e=0;e<8;e++)w2_bits(a,e,q8(input(qt*16+rc,head*32+ci*16+gr*8+e)));
  w2_store<C>(plane0,qt,head*2+ci,a);
 }
W2_REP_END
 }
 w2_sync();
W2_REP_BEGIN(1)
 // Each head contracts only its own 128 hidden channels. Expanded fragments never enter LDS.
#if W2_FFN_QT_BATCH
 if constexpr(C==256 || (C==64 && (W2_FFN_QT_SMALL_MASK&1)) || (C==128 && (W2_FFN_QT_SMALL_MASK&2))){
 // Reuse each weight fragment across independent token tiles. Each accumulator keeps
 // the original kt or ht/tile order, including activation, FP8 and RTZ boundaries.
 constexpr uint B=W2_FFN_QT_BATCH;
 constexpr uint T=W2_HIDDEN_TILES;
 _Pragma("clang loop unroll(disable)") for(uint qb=0;qb<4;qb+=B){
  f8 contract[B][2]{};
  _Pragma("clang loop unroll(disable)") for(uint ht=0;ht<8;ht+=T){
   uint ffn_head=head,ffn_rc=rc,ffn_gr=gr;
   if constexpr(C==64){uint ffn_tid=__builtin_amdgcn_workitem_id_x();__asm__ volatile("":"+v"(ffn_tid));ffn_head=ffn_tid/32;ffn_rc=ffn_tid&15u;ffn_gr=(ffn_tid&31u)>>4;}
   f8 expanded[B][T]{};
   if constexpr(W16){
    for(uint kt=0;kt<C/16;kt+=2){
     i2 in0[B],in1[B];
     _Pragma("unroll") for(uint qi=0;qi<B;qi++){in0[qi]=w2_load<C>(plane0,qb+qi,kt);in1[qi]=w2_load<C>(plane0,qb+qi,kt+1);}
     _Pragma("unroll") for(uint tile=0;tile<T;tile++){
      w2_i4 w=w2_weight16(fw,head*128+(ht+tile)*16+rc,C,kt*16);i2 b0={w[0],w[1]},b1={w[2],w[3]};
      _Pragma("unroll") for(uint qi=0;qi<B;qi++)expanded[qi][tile]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b0,in0[qi],expanded[qi][tile]);
      _Pragma("unroll") for(uint qi=0;qi<B;qi++)expanded[qi][tile]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b1,in1[qi],expanded[qi][tile]);
     }
    }
   }else
   for(uint kt=0;kt<C/16;kt++){
    i2 inputs[B];
    _Pragma("unroll") for(uint qi=0;qi<B;qi++)inputs[qi]=w2_load<C>(plane0,qb+qi,kt);
    _Pragma("unroll") for(uint tile=0;tile<T;tile++){
     i2 b=w2_weight(fw,ffn_head*128+(ht+tile)*16+ffn_rc,C,kt*16,ffn_gr);
     _Pragma("unroll") for(uint qi=0;qi<B;qi++)expanded[qi][tile]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,inputs[qi],expanded[qi][tile]);
    }
   }
   if constexpr(W16){
    _Pragma("unroll") for(uint tile=0;tile<T;tile+=2){
     i2 hidden[2][B];
     _Pragma("unroll") for(uint t2=0;t2<2;t2++)_Pragma("unroll") for(uint qi=0;qi<B;qi++){
      W2_Q8_DECL(packed);
      for(uint e=0;e<8;e++){float a=expanded[qi][tile+t2][e],g=clampf(a,-4.f,4.f),q=__builtin_fmaf(absf(g),-.055908203125f,.447265625f),poly=__builtin_fmaf(g,q,.89453125f);W2_Q8_SETF(packed,e,a,poly);}W2_Q8_END(packed);hidden[t2][qi]=packed;
     }
     for(uint ci=0;ci<2;ci++){
      w2_i4 w=w2_weight16(fw+4*C*C,head*32+ci*16+rc,4*C,head*128+(ht+tile)*16);i2 b0={w[0],w[1]},b1={w[2],w[3]};
      _Pragma("unroll") for(uint qi=0;qi<B;qi++)contract[qi][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b0,hidden[0][qi],contract[qi][ci]);
      _Pragma("unroll") for(uint qi=0;qi<B;qi++)contract[qi][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b1,hidden[1][qi],contract[qi][ci]);
     }
    }
   }else
   _Pragma("unroll") for(uint tile=0;tile<T;tile++){
    i2 hidden[B];
    _Pragma("unroll") for(uint qi=0;qi<B;qi++){
     W2_Q8_DECL(packed);
     for(uint e=0;e<8;e++){float a=expanded[qi][tile][e],g=clampf(a,-4.f,4.f),q=__builtin_fmaf(absf(g),-.055908203125f,.447265625f),poly=__builtin_fmaf(g,q,.89453125f);W2_Q8_SETF(packed,e,a,poly);}W2_Q8_END(packed);hidden[qi]=packed;
    }
    for(uint ci=0;ci<2;ci++){
     i2 b=w2_weight(fw+4*C*C,ffn_head*32+ci*16+ffn_rc,4*C,ffn_head*128+(ht+tile)*16,ffn_gr);
     _Pragma("unroll") for(uint qi=0;qi<B;qi++)contract[qi][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,hidden[qi],contract[qi][ci]);
    }
   }
  }
  _Pragma("unroll") for(uint qi=0;qi<B;qi++)for(uint ci=0;ci<2;ci++){
#if W2_RTZ_PAIR
#if W2_Q8_MASK
   W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,w2_hmask(contract[qi][ci][e]));W2_Q8_END(a);
#else
   f8 rounded=w2_rtz8(contract[qi][ci]);W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,rounded[e]);W2_Q8_END(a);
#endif
#else
   W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,Hrtz(contract[qi][ci][e]));W2_Q8_END(a);
#endif
   w2_store<C>(plane1,qb+qi,head*2+ci,a);
  }
 }
 }else
#endif
 {
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
    for(uint e=0;e<8;e++){float a=expanded[tile][e],g=clampf(a,-4.f,4.f),q=__builtin_fmaf(absf(g),-.055908203125f,.447265625f),poly=__builtin_fmaf(g,q,.89453125f);W2_Q8_SETF(hidden,e,a,poly);}W2_Q8_END(hidden);
    for(uint ci=0;ci<2;ci++){
     i2 b=w2_weight(fw+4*C*C,head*32+ci*16+rc,4*C,head*128+(ht+tile)*16);
     contract[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,hidden,contract[ci]);
    }
   }
  }
#if W2_RTZ_PAIR
#if W2_Q8_MASK
  for(uint ci=0;ci<2;ci++){W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,w2_hmask(contract[ci][e]));W2_Q8_END(a);w2_store<C>(plane1,qt,head*2+ci,a);}
#else
  for(uint ci=0;ci<2;ci++){f8 rounded=w2_rtz8(contract[ci]);W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,rounded[e]);W2_Q8_END(a);w2_store<C>(plane1,qt,head*2+ci,a);}
#endif
#else
  for(uint ci=0;ci<2;ci++){W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,Hrtz(contract[ci][e]));W2_Q8_END(a);w2_store<C>(plane1,qt,head*2+ci,a);}
#endif
 }
 }
W2_REP_END
 w2_sync();
W2_REP_BEGIN(2)
 // Cross-head FFN mixing plus the original residual. plane0 is now free for feature bytes.
 _Pragma("clang loop unroll(disable)") for(uint qt=0;qt<4;qt++){
  f8 result[2]{};
#if W2_RTZ_PAIR || W2_BYTE_INPUT_LOADS
  for(uint ci=0;ci<2;ci++){f8 products;i2 ib{};
   if constexpr(Up){
    ib=w2_load<C>(plane0,qt,head*2+ci);
   }else if constexpr(ByteIn&&W2_BYTE_INPUT_LOADS)ib=(W2_HOIST_LOADS&2)?w2_input_bytes_nb<C>(in,win,qt*16+rc,head*32+ci*16+gr*8,workw,width,height,sx,sy):w2_input_bytes<C>(in,win,qt*16+rc,head*32+ci*16+gr*8,workw,width,height,sx,sy);
   for(uint e=0;e<8;e++){uint c=head*32+ci*16+gr*8+e;float v;if constexpr(ByteIn&&W2_BYTE_INPUT_LOADS)v=__builtin_amdgcn_cvt_f32_fp8(int((uint(ib[e/4])>>(8*(e%4)))&255u),0);else v=input(qt*16+rc,c);products[e]=v*fw[9*C*C+c];}
#if W2_RTZ_PAIR
   result[ci]=w2_rtz8(products);
#else
   for(uint e=0;e<8;e++)result[ci][e]=Hrtz(products[e]);
#endif
  }
#else
  for(uint ci=0;ci<2;ci++)for(uint e=0;e<8;e++){uint c=head*32+ci*16+gr*8+e;result[ci][e]=Hrtz(input(qt*16+rc,c)*fw[9*C*C+c]);}
#endif
  if constexpr(W16){
   for(uint kt=0;kt<C/16;kt+=2){i2 a0=w2_load<C>(plane1,qt,kt),a1=w2_load<C>(plane1,qt,kt+1);for(uint ci=0;ci<2;ci++){
    w2_i4 w=w2_weight16(fw+8*C*C,head*32+ci*16+rc,C,kt*16);i2 b0={w[0],w[1]},b1={w[2],w[3]};
    result[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b0,a0,result[ci]);
    result[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b1,a1,result[ci]);
   }}
  }else
  for(uint kt=0;kt<C/16;kt++)for(uint ci=0;ci<2;ci++){
   i2 a=w2_load<C>(plane1,qt,kt),b=w2_weight(fw+8*C*C,head*32+ci*16+rc,C,kt*16);
   result[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,result[ci]);
  }
  for(uint ci=0;ci<2;ci++){W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,result[ci][e]);W2_Q8_END(a);w2_store<C>(plane0,qt,head*2+ci,a);}
 }
W2_REP_END
 w2_sync();
 // Q/K use A-fragment orientation. V is computed in the opposite orientation,
 // directly becoming the B fragments consumed by P*V; no V transpose/staging needed.
 i2 qkv[3][4][2];
#if W2_ROLL_QUERY && !W2_DEFER_Q
 w2_i16 query_words{};
#endif
W2_REP_BEGIN(4)
 /* W2_QKV_FUSE (2026-10-01, c128-phase; bitmask 1=C64 2=C128 4=C256): Q/K/V in one kt loop per token tile, the A fragment
    read from LDS once for all three (mochizuki fswin / our C512_COMPACT_FUSEQKV organization). Every accumulator sees the
    same WMMA sequence in the same kt order, normalization and stores are unchanged -> bit-exact. */
 constexpr bool W2QkvFuse=(C==64&&(W2_QKV_FUSE&1))||(C==128&&(W2_QKV_FUSE&2))||(C==256&&(W2_QKV_FUSE&4));
 /* bit 8 (C256 only): fuse Q+K in one kt loop, V in a second (4 live accumulators instead of 6; register pressure) */
 constexpr bool W2QkvPair=C==256&&(W2_QKV_FUSE&8);
 if constexpr(W2QkvFuse){
  constexpr uint ps=W2_DEFER_Q?1:0;
  _Pragma("unroll") for(uint qt=0;qt<4;qt++){
   uint qlane=w2_lane();
#if W2_LAUNDER_QKV
   __asm__ volatile("" : "+v"(qlane));qlane&=31u;
#endif
   uint qrc=qlane&15u,qgr=qlane>>4;
   f8 q[3][2]{};
   _Pragma("unroll") for(uint grp=0;grp<(W2QkvPair?2u:1u);grp++)
#if W2_QKV_FUSE&16
   _Pragma("clang loop unroll(disable)")
#endif
   for(uint kt=0;kt<C/16;kt++){
    i2 a=w2_load<C>(plane0,qt,kt,qlane);
    _Pragma("unroll") for(uint part=(W2QkvPair&&grp?2u:ps);part<(W2QkvPair&&!grp?2u:3u);part++)_Pragma("unroll") for(uint ci=0;ci<2;ci++){
     i2 b=w2_weight(aw,part*C+head*32+ci*16+qrc,C,kt*16,qgr);
     if(part<2)q[part][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,q[part][ci]);
     else q[part][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,q[part][ci]);
    }
   }
   _Pragma("unroll") for(uint part=ps;part<3;part++){
    float inv=1.f;
    if(part<2){float ss=w2_serial_norm(q[part][0],q[part][1]);inv=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?aw[4*C*C+(C/32)*4096+head]:1.f);}
    for(uint ci=0;ci<2;ci++){W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SETF(a,e,q[part][ci][e],inv);W2_Q8_END(a);
#if W2_ROLL_QUERY && !W2_DEFER_Q
     if(part==0){query_words[qt*4+ci*2]=a[0];query_words[qt*4+ci*2+1]=a[1];}else
#endif
     qkv[part][qt][ci]=a;
    }
   }
   w2_schedule();
  }
 }else
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
   for(uint ci=0;ci<2;ci++){W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SETF(a,e,q[ci][e],inv);W2_Q8_END(a);
#if W2_ROLL_QUERY && !W2_DEFER_Q
    if(part==0){query_words[qt*4+ci*2]=a[0];query_words[qt*4+ci*2+1]=a[1];}else
#endif
    qkv[part][qt][ci]=a;
   }
   w2_schedule();
  }
 }
W2_REP_END
 constexpr bool W2SkipB8=SkipByteOut;void*const w2_sb=const_cast<float*>(skip);(void)w2_sb; // W2_SKIP_BYTE: *_bi_hob byte copy target
 constexpr bool W2HalfTail=HalfOut; // post-3 tail stored as f16 (Hrtz is f16-exact); attention-only copies define it false
 // One wave owns all keys and queries of a head. Existing WMMA half row-sum order is retained.
#undef W2_REP_BEGIN
#undef W2_REP_END
#if W2_PHASE_REP && !defined(W2_ATTENTION_ONLY)
#define W2_REP_BEGIN(bit) for(uint w2rep_##bit=0;w2rep_##bit<(((W2_PHASE_REP&(bit))&&C==128&&!Up&&ByteIn&&ByteOut&&!W16&&!HalfOut)?2u:1u);w2rep_##bit++){if constexpr((W2_PHASE_REP&(bit))&&C==128&&!Up&&ByteIn&&ByteOut&&!W16&&!HalfOut)__asm__ volatile("":::"memory");
#else
#define W2_REP_BEGIN(bit) {
#endif
#define W2_REP_END }
W2_REP_BEGIN(8)
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
  i2 query_frag[2];for(uint ci=0;ci<2;ci++){W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SETF(a,e,query[ci][e],qinv);W2_Q8_END(a);query_frag[ci]=a;}
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
  const float inv=w2_inverse(sums[0][0]+sums[1][0]);
  i2 prob[4];
  _Pragma("unroll 4") for(uint key=0;key<4;key++){W2_Q8_DECL(a);for(uint e=0;e<8;e++){W2_Q8_SETF(a,e,float(exponent[key][e]),inv);}W2_Q8_END(a);prob[key]=a;}
  for(uint ci=0;ci<2;ci++){
   f8 av{};
   _Pragma("unroll 4") for(uint key=0;key<4;key++)av=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(qkv[2][key][ci],prob[key],av);
   W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,av[e]);W2_Q8_END(a);w2_store<C>(plane1,qt,head*2+ci,a);
  }
  w2_schedule();
 }
W2_REP_END
 w2_sync();
 // All heads' AV ready; feature still lives in plane0. Preserve diagonal residual and K order.
W2_REP_BEGIN(16)
 const unsigned char*diag=reinterpret_cast<const unsigned char*>(aw)+4*(4*C*C+(C/32)*4096+C/32+C);
#if W2_FULL_WINDOW
 auto finish=[&](auto full){
 _Pragma("clang loop unroll(disable)") for(uint qt=0;qt<4;qt++){
  f8 result[2]{};
  for(uint ci=0;ci<2;ci++){
   uint ct=head*2+ci;i2 a;
#if defined(W2_ATTENTION_ONLY) && W2_DIRECT_FEATURE
   __builtin_memcpy(&a,feature+w2_pixel(win,qt*16+rc,workw)*C+ct*16+gr*8,8);
#else
   a=w2_load<C>(plane0,qt,ct);
#endif
   for(uint part=0;part<3;part++){i2 b;__builtin_memcpy(&b,diag+(part*(C/16)+ct)*512+(gr*16+rc)*8,8);result[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,result[ci]);}
  }
  for(uint kt=0;kt<C/16;kt++)for(uint ci=0;ci<2;ci++){
   i2 a=w2_load<C>(plane1,qt,kt),b=w2_weight(aw+3*C*C,head*32+ci*16+rc,C,kt*16);
   result[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,result[ci]);
  }
#if W2_DIRECT_COORDS
  uint x=(win%(workw/8))*8+(qt*16+rc)%8,y=(win/(workw/8))*8+(qt*16+rc)/8;
#else
  uint p=w2_pixel(win,qt*16+rc,workw),x=p%workw,y=p/workw;
#endif
  if(full||(x>=sx&&y>=sy&&x<sx+width&&y<sy+height)){
   uint dest=((y-sy)*width+x-sx)*C+head*32+gr*8;
   for(uint ci=0;ci<2;ci++){
    if constexpr(ByteOut){W2_Q8_DECL(a);for(uint e=0;e<8;e++){float v=result[ci][e];W2_Q8_SET(a,e,post==0?(W2_Q8_MASK?w2_hmask(v):Hrtz(v)):v);}W2_Q8_END(a);__builtin_memcpy(reinterpret_cast<unsigned char*>(out_any)+dest+ci*16,&a,8);}
    else if constexpr(W2HalfTail){for(uint e=0;e<8;e++){float v=result[ci][e];static_cast<_Float16*>(out_any)[dest+ci*16+e]=(_Float16)Hrtz(v);}if constexpr(W2SkipB8){i2 sb{};for(uint e=0;e<8;e++)w2_bits(sb,e,w2_skip_byte(Hrtz(result[ci][e])));__builtin_memcpy(static_cast<unsigned char*>(w2_sb)+dest+ci*16,&sb,8);}}
    else for(uint e=0;e<8;e++){float v=result[ci][e],h=Hrtz(v),hi=post==0?h:v,hc=__builtin_amdgcn_fmed3f(hi+0.f,-448.f,448.f),f=__builtin_amdgcn_cvt_f32_fp8(__builtin_amdgcn_cvt_pk_fp8_f32(hc,0.f,0,false),0);static_cast<float*>(out_any)[dest+ci*16+e]=post==3?h:f;}
   }
  }
 }
 };
 uint tx=(win%(workw/8))*8,ty=(win/(workw/8))*8;
 if(tx>=sx&&ty>=sy&&tx+8<=sx+width&&ty+8<=sy+height)finish(w2_true{});else finish(w2_false{});
#else
 _Pragma("clang loop unroll(disable)") for(uint qt=0;qt<4;qt++){
  f8 result[2]{};
  for(uint ci=0;ci<2;ci++){
   uint ct=head*2+ci;i2 a;
#if defined(W2_ATTENTION_ONLY) && W2_DIRECT_FEATURE
   __builtin_memcpy(&a,feature+w2_pixel(win,qt*16+rc,workw)*C+ct*16+gr*8,8);
#else
   a=w2_load<C>(plane0,qt,ct);
#endif
   for(uint part=0;part<3;part++){i2 b;__builtin_memcpy(&b,diag+(part*(C/16)+ct)*512+(gr*16+rc)*8,8);result[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,result[ci]);}
  }
  for(uint kt=0;kt<C/16;kt++)for(uint ci=0;ci<2;ci++){
   i2 a=w2_load<C>(plane1,qt,kt),b=w2_weight(aw+3*C*C,head*32+ci*16+rc,C,kt*16);
   result[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,result[ci]);
  }
#if W2_DIRECT_COORDS
  uint x=(win%(workw/8))*8+(qt*16+rc)%8,y=(win/(workw/8))*8+(qt*16+rc)/8;
#else
  uint p=w2_pixel(win,qt*16+rc,workw),x=p%workw,y=p/workw;
#endif
  if(x>=sx&&y>=sy&&x<sx+width&&y<sy+height){
   uint dest=((y-sy)*width+x-sx)*C+head*32+gr*8;
   for(uint ci=0;ci<2;ci++){
    if constexpr(ByteOut){W2_Q8_DECL(a);for(uint e=0;e<8;e++){float v=result[ci][e];W2_Q8_SET(a,e,post==0?(W2_Q8_MASK?w2_hmask(v):Hrtz(v)):v);}W2_Q8_END(a);__builtin_memcpy(reinterpret_cast<unsigned char*>(out_any)+dest+ci*16,&a,8);}
    else if constexpr(W2HalfTail){for(uint e=0;e<8;e++){float v=result[ci][e];static_cast<_Float16*>(out_any)[dest+ci*16+e]=(_Float16)Hrtz(v);}if constexpr(W2SkipB8){i2 sb{};for(uint e=0;e<8;e++)w2_bits(sb,e,w2_skip_byte(Hrtz(result[ci][e])));__builtin_memcpy(static_cast<unsigned char*>(w2_sb)+dest+ci*16,&sb,8);}}
    else for(uint e=0;e<8;e++){float v=result[ci][e],h=Hrtz(v),hi=post==0?h:v,hc=__builtin_amdgcn_fmed3f(hi+0.f,-448.f,448.f),f=__builtin_amdgcn_cvt_f32_fp8(__builtin_amdgcn_cvt_pk_fp8_f32(hc,0.f,0,false),0);static_cast<float*>(out_any)[dest+ci*16+e]=post==3?h:f;}
   }
  }
 }
#endif
W2_REP_END
}
#define W2_KERNEL(NAME,C,BI,BO) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post){swin_wave2_body<C,BI,BO>(in,fw,aw,out,w,h,ww,hh,sx,sy,post);}
#if !defined(W2_NO_EXPORTS) || !W2_NO_EXPORTS
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
#if W2_FFN_W16
#define W2_KERNEL_W16(NAME,C,BI,BO) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post){swin_wave2_body<C,BI,BO,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post);}
W2_KERNEL_W16(c256_wave2_w16,256,false,false)
W2_KERNEL_W16(c256_wave2_bi_w16,256,true,false)
W2_KERNEL_W16(c256_wave2_bo_w16,256,false,true)
W2_KERNEL_W16(c256_wave2_bi_bo_w16,256,true,true)
#endif
// W2_FFN_W16_SMALL (2026-09-30, results/w16-c64-c128-20260930): the same wide fragments for C64/C128 (bit0 C64, bit1 C128),
// again only as new exports (c64/c128_wave2*_w16, *_wave2_up_w16); the host uses them only when present.
#ifndef W2_FFN_W16_SMALL
#define W2_FFN_W16_SMALL 0
#endif
#if W2_FFN_W16_SMALL && !W2_FFN_W16
#error W2_FFN_W16_SMALL needs W2_FFN_W16
#endif
#if W2_FFN_W16_SMALL&1
W2_KERNEL_W16(c64_wave2_w16,64,false,false)
W2_KERNEL_W16(c64_wave2_bi_w16,64,true,false)
W2_KERNEL_W16(c64_wave2_bo_w16,64,false,true)
W2_KERNEL_W16(c64_wave2_bi_bo_w16,64,true,true)
#endif
#if W2_FFN_W16_SMALL&2
W2_KERNEL_W16(c128_wave2_w16,128,false,false)
W2_KERNEL_W16(c128_wave2_bi_w16,128,true,false)
W2_KERNEL_W16(c128_wave2_bo_w16,128,false,true)
W2_KERNEL_W16(c128_wave2_bi_bo_w16,128,true,true)
#endif

#if W2_UP_FUSED
#define W2_UP_KERNEL(NAME,C) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post,const float*upw,const float*skip){swin_wave2_body<C,true,true,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post,upw,skip);}
W2_UP_KERNEL(c64_wave2_up,64)
W2_UP_KERNEL(c128_wave2_up,128)
#define W2_UP_KERNEL_W16(NAME,C) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post,const float*upw,const float*skip){swin_wave2_body<C,true,true,true,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post,upw,skip);}
#if W2_FFN_W16_SMALL&1
W2_UP_KERNEL_W16(c64_wave2_up_w16,64)
#endif
#if W2_FFN_W16_SMALL&2
W2_UP_KERNEL_W16(c128_wave2_up_w16,128)
#endif
/* W2_UP_LOW_BYTES (2026-10-01, swin-body-gap): the fused Up reads its low-resolution input as E4M3 bytes. The producer is the
   previous decoder chain's tail (post 0: F(Hrtz(v)), always on the E4M3 lattice), which then writes bytes instead of f32; cvt back
   is exact, so every (_Float16) operand is unchanged. New exports only (*_up_lb); the host uses them only when present. */
#ifndef W2_UP_LOW_BYTES
#define W2_UP_LOW_BYTES 0
#endif
#if W2_UP_LOW_BYTES
#define W2_UP_KERNEL_LB(NAME,C) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post,const float*upw,const float*skip){swin_wave2_body<C,true,true,true,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post,upw,skip);}
W2_UP_KERNEL_LB(c64_wave2_up_lb,64)
W2_UP_KERNEL_LB(c128_wave2_up_lb,128)
#endif
/* W2_DOWN_HALF (2026-10-01): encoder chain tail (post 3 only: Hrtz(v), exactly an f16) stored as f16 (*_bi_ho), the pool (mh_pool_project_group_c*_hin)
   and the fused Up's skip (*_up_sh, *_up_lb_sh) read it back as f16 -> the same float. New exports only. */
#ifndef W2_DOWN_HALF
#define W2_DOWN_HALF 0
#endif
#if W2_DOWN_HALF
#define W2_KERNEL_HO(NAME,C) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post){swin_wave2_body<C,true,false,false,false,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post);}
W2_KERNEL_HO(c64_wave2_bi_ho,64)
W2_KERNEL_HO(c128_wave2_bi_ho,128)
#define W2_UP_KERNEL_SH(NAME,C,LB) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post,const float*upw,const float*skip){swin_wave2_body<C,true,true,true,false,LB,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post,upw,skip);}
W2_UP_KERNEL_SH(c64_wave2_up_sh,64,false)
W2_UP_KERNEL_SH(c128_wave2_up_sh,128,false)
W2_UP_KERNEL_SH(c64_wave2_up_lb_sh,64,true)
W2_UP_KERNEL_SH(c128_wave2_up_lb_sh,128,true)
#ifndef W2_SKIP_BYTE
#define W2_SKIP_BYTE 0
#endif
#if W2_SKIP_BYTE
/* *_bi_hob: *_bi_ho plus a byte copy of F(Hrtz(v)) at arg 12 (passed through the body's skip slot); *_up_lb_sb: *_up_lb reading that byte skip. */
#define W2_KERNEL_HOB(NAME,C) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post,void*sb){swin_wave2_body<C,true,false,false,false,false,true,false,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post,nullptr,static_cast<const float*>(sb));}
W2_KERNEL_HOB(c64_wave2_bi_hob,64)
W2_KERNEL_HOB(c128_wave2_bi_hob,128)
#define W2_UP_KERNEL_SB(NAME,C) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post,const float*upw,const float*skip){swin_wave2_body<C,true,true,true,false,true,false,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post,upw,skip);}
W2_UP_KERNEL_SB(c64_wave2_up_lb_sb,64)
W2_UP_KERNEL_SB(c128_wave2_up_lb_sb,128)
#endif
#endif
#endif
#endif
