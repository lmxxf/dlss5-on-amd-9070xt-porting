// small-wins-20260930 (from mochizuki-022 I/P): exact representation changes only; every arithmetic rounding kept, its half bits reused.
#ifndef CW_INPUT_HALF
#define CW_INPUT_HALF 0 /* bit0: post Hrtz value; bit1: mapped input (half/FP8 producers); bit2: Up H() value -- skip float((_Float16)v) */
#endif
#ifndef CW_PREFIX_HALF_SOURCE
#define CW_PREFIX_HALF_SOURCE 0 /* prefix: keep the RTZ half bit pattern through the shuffle into the WMMA operand */
#endif
#if CW_PREFIX_HALF_SOURCE
using cw_pv_t=_Float16;
DEV cw_pv_t cw_prefix_rtz(float x){uint p;asm("v_cvt_pkrtz_f16_f32 %0, %1, %1":"=v"(p):"v"(x));return __builtin_bit_cast(_Float16,(unsigned short)p);}
DEV cw_pv_t cw_prefix_shuffle(cw_pv_t x,uint lane){uint p=uint(__builtin_bit_cast(unsigned short,x));p=uint(__builtin_amdgcn_ds_bpermute(lane*4,int(p)));return __builtin_bit_cast(_Float16,(unsigned short)p);}
#else
using cw_pv_t=float;
DEV cw_pv_t cw_prefix_rtz(float x){return Hrtz(x);}
DEV cw_pv_t cw_prefix_shuffle(cw_pv_t x,uint lane){return __builtin_bit_cast(float,__builtin_amdgcn_ds_bpermute(lane*4,__builtin_bit_cast(int,x)));}
#endif
// One complete 64-token C32 window per wave; chain FP8 input/output.
#ifndef CW_LAUNDER
#define CW_LAUNDER 0
#endif
#ifndef CW_ROLL_HIDDEN
#define CW_ROLL_HIDDEN 0
#endif
#ifndef CW_QKV_FENCE
#define CW_QKV_FENCE 0
#endif
#ifndef CW_ROLL_WINDOW
#define CW_ROLL_WINDOW 0
#endif
#ifndef CW_PREFIX_SPLIT
#define CW_PREFIX_SPLIT 0 /* 1: prefix features split across both half-waves (bit-exact; 1080 -0.03ms, results/vit-attn-c32-input-20260926) */
#endif
#ifndef CW_TAIL_QUAD
#define CW_TAIL_QUAD 0 /* 1: finish/prefix tail walks 2x2 quads once: 4 LDS reads feed both main and down (was 64+64 reads), 32-bit byte offsets for stores; arithmetic unchanged */
#endif
#ifndef CW_VEC_INPUT
#define CW_VEC_INPUT 0 /* 1: mapped/post input rows read as b128/b64 instead of per-element loads (bit-exact; results/c32-wave-phase-20260926) */
#endif
// C32 ACO audit (Development/results/c32-aco-20260927). Switches default off; the build recipe selects the accepted pair.
#ifndef CW_FMED3_CLAMP
#define CW_FMED3_CLAMP 0
#endif
#ifndef CW_DIRECT_OUT
#define CW_DIRECT_OUT 0
#endif
#ifndef CW_RTZ_PAIR
#define CW_RTZ_PAIR 0
#endif
DEV float cw_clampf(float x,float lo,float hi){
#if CW_FMED3_CLAMP
 return __builtin_amdgcn_fmed3f(x,lo,hi);
#else
 return clampf(x,lo,hi);
#endif
}
// Activation clamp only: the polynomial retains all original rounding
// boundaries. fmed3(x,-4,4) returns -4 for NaN just like the ternary clamp.
#ifndef CW_ACT_FMED3
#define CW_ACT_FMED3 0
#endif
DEV float cw_act_gate(float x){
#if CW_ACT_FMED3
 return __builtin_amdgcn_fmed3f(x,-4.f,4.f);
#else
 return cw_clampf(x,-4.f,4.f);
#endif
}
// Two independent RTZ conversions in the instruction we already use; no packed-half arithmetic,
// no MODE change, and no contraction across an arithmetic/rounding boundary.
#if CW_RTZ_PAIR
using cw_h2=_Float16 __attribute__((ext_vector_type(2)));
DEV h8 cw_rtz_half8(f8 x){h8 out;_Pragma("unroll") for(uint j=0;j<4;j++){
 uint packed;asm("v_cvt_pkrtz_f16_f32 %0, %1, %2" : "=v"(packed) : "v"(x[2*j]),"v"(x[2*j+1]));
 cw_h2 pair=__builtin_bit_cast(cw_h2,packed);out[2*j]=pair[0];out[2*j+1]=pair[1];}return out;}
#endif
DEV float cw_output_value(float v){
#if CW_DIRECT_OUT
 // fp8(F(v)) == fp8(v), except F canonicalizes an exact -0. Preserve underflow-generated -0.
 return v+0.f;
#else
 return F(float((_Float16)v));
#endif
}
#ifndef CW_PREFIX_DIRECT_OUT
#define CW_PREFIX_DIRECT_OUT 0 /* prefix byte tail: avoid fp8(F(half-value)); retain exact-zero canonicalization */
#endif
#ifndef CW_PREFIX_FULL_TILE
#define CW_PREFIX_FULL_TILE 0 /* prefix only: host emits complete 8x8 tiles, sx=sy=0, so its per-pixel bounds tests are redundant */
#endif
#ifndef CW_POST_HEAD_VEC
#define CW_POST_HEAD_VEC 0 /* RGB head: read each contiguous eight-half group together, retain channel reduction order */
#endif
#ifndef CW_POST_FULL_TILE
#define CW_POST_FULL_TILE 0 /* specialize interior RGB output windows once per wave */
#endif
#ifndef CW_FINISH_FULL_TILE
#define CW_FINISH_FULL_TILE 0 /* specialize interior main/down output windows once per wave */
#endif
DEV uint cw_prefix_byte(float v){
#if CW_PREFIX_DIRECT_OUT
 return fp8(v+0.f);
#else
 return fp8(F(v));
#endif
}
struct cw_true {constexpr operator bool()const{return true;}};
struct cw_false {constexpr operator bool()const{return false;}};
using cw_i16=int __attribute__((ext_vector_type(16)));
// CW_PACK8 (2026-09-27, results/pack8-20260927): the same bytes as put_bits(a,e,fp8(x)) -- the identical clamp, then
// v_cvt_pk_fp8_f32 on two values at once written straight into the fragment word instead of one cvt + and/shift/or per byte.
// fp8() has no -0 select in C32, so nothing else changes.
#ifndef CW_PACK8
#define CW_PACK8 0
#endif
#if CW_PACK8
#if HIP_FP8_SAT_MODE==3
DEV float cw_z(float x){return __builtin_amdgcn_fmed3f(x,-448.f,448.f);}
#elif HIP_C32_BRANCHLESS_F
DEV float cw_z(float x){return __builtin_fminf(__builtin_fmaxf(x,-448.f),448.f);}
#else
DEV float cw_z(float x){return clampf(x,-448.f,448.f);}
#endif
#ifndef CW_PACK_CENSUS
#define CW_PACK_CENSUS 0 /* diagnostic only: per-kind, per-site pack counts / nonfinite / >448 / >1 / maximum absolute finite value */
#endif
#ifndef CW_PACK_MODE_MASK
#define CW_PACK_MODE_MASK 0 /* bit per pack site: input, hidden, feature, QKV, probability, AV, output */
#endif
#if CW_PACK_CENSUS
extern "C" __attribute__((device)) uint cw_pack_census[6*7*5]={};
template<uint Site,uint Kind> DEV void cw_census(const float*t){
 uint lane=__builtin_amdgcn_workitem_id_x(),flags=0,mx=0;
 for(uint e=0;e<8;e++){uint ab=bits(t[e])&0x7fffffffu;flags|=(ab>=0x7f800000u?1u:0u)|(ab>0x43e00000u?2u:0u)|(ab>0x3f800000u?4u:0u);if(ab<0x7f800000u&&ab>mx)mx=ab;}
 for(uint sh=16;sh;sh>>=1){uint other=uint(__builtin_amdgcn_ds_bpermute((lane^sh)*4,int(mx)));if(other>mx)mx=other;flags|=uint(__builtin_amdgcn_ds_bpermute((lane^sh)*4,int(flags)));}
 if(lane==0){uint*p=cw_pack_census+(Kind*7+Site)*5;__hip_atomic_fetch_add(p,1u,__ATOMIC_RELAXED,__HIP_MEMORY_SCOPE_AGENT);
  if(flags&1)__hip_atomic_fetch_add(p+1,1u,__ATOMIC_RELAXED,__HIP_MEMORY_SCOPE_AGENT);
  if(flags&2)__hip_atomic_fetch_add(p+2,1u,__ATOMIC_RELAXED,__HIP_MEMORY_SCOPE_AGENT);
  if(flags&4)__hip_atomic_fetch_add(p+3,1u,__ATOMIC_RELAXED,__HIP_MEMORY_SCOPE_AGENT);
  __hip_atomic_fetch_max(p+4,mx,__ATOMIC_RELAXED,__HIP_MEMORY_SCOPE_AGENT);}
}
#endif
template<uint Site,uint Kind> DEV i2 cw_pack8(const float*t){
#if CW_PACK_CENSUS
 cw_census<Site,Kind>(t);
#endif
// Keep MODE and its four conversions indivisible: LLVM can hoist the FP8 intrinsics outside s_setreg.
 if constexpr((CW_PACK_MODE_MASK&(1u<<Site))!=0){int lo,hi;
  asm volatile("s_setreg_imm32_b32 hwreg(HW_REG_MODE, 23, 1), 1\n\t"
               "v_cvt_pk_fp8_f32 %0, %2, %3\n\t"
               "v_cvt_pk_fp8_f32 %1, %6, %7\n\t"
               "v_cvt_pk_fp8_f32 %0, %4, %5 op_sel:[0,0,1]\n\t"
               "v_cvt_pk_fp8_f32 %1, %8, %9 op_sel:[0,0,1]\n\t"
               "s_setreg_imm32_b32 hwreg(HW_REG_MODE, 23, 1), 0"
               : "=&v"(lo),"=&v"(hi) : "v"(t[0]),"v"(t[1]),"v"(t[2]),"v"(t[3]),"v"(t[4]),"v"(t[5]),"v"(t[6]),"v"(t[7]) : "memory");
  return i2{lo,hi};}
 int lo=__builtin_amdgcn_cvt_pk_fp8_f32(cw_z(t[0]),cw_z(t[1]),0,false);lo=__builtin_amdgcn_cvt_pk_fp8_f32(cw_z(t[2]),cw_z(t[3]),lo,true);
 int hi=__builtin_amdgcn_cvt_pk_fp8_f32(cw_z(t[4]),cw_z(t[5]),0,false);hi=__builtin_amdgcn_cvt_pk_fp8_f32(cw_z(t[6]),cw_z(t[7]),hi,true);
 i2 a;a[0]=lo;a[1]=hi;return a;}

#define CW_Q8_DECL(A) i2 A;float A##_t[8]
#define CW_Q8_SET(A,E,X) A##_t[E]=(X)
#define CW_Q8_END_SITE(A,S) A=cw_pack8<S,cw_kind>(A##_t)
#define CW_Q8_END(A) CW_Q8_END_SITE(A,0)
#else
#define CW_Q8_DECL(A) i2 A{}
#define CW_Q8_SET(A,E,X) put_bits(A,E,fp8(X))
#define CW_Q8_END(A) (void)0
#define CW_Q8_END_SITE(A,S) (void)0
#endif

DEV i2 cw_input(const unsigned char*in,uint win,uint qt,uint ci,uint width,uint height,uint sx,uint sy,uint prevw,uint px,uint py,uint r,uint g){
 uint ww=width+2*sx;int x=int((win%(ww/8))*8+(qt*16+r)%8)-int(sx),y=int((win/(ww/8))*8+(qt*16+r)/8)-int(sy);i2 a{};
 if(x>=0&&y>=0&&x<int(width)&&y<int(height)){uint xx=uint(x)+px,yy=uint(y)+py,tile=(yy/8)*(prevw/8)+xx/8,tok=(yy%8)*8+xx%8;__builtin_memcpy(&a,in+(tile*64+tok)*32+ci*16+g*8,8);}return a;
}
#ifndef CW_PREPOST_BYTE
#define CW_PREPOST_BYTE 0 /* 1: extra exports c32_wave1_prefix_b8d / c32_wave1_mapped_b8 (block0 pooled output as E4M3 bytes, read by block1) and c32_wave1_finish_b8 / c32_wave1_post_b8 (block69 main as E4M3 bytes, read by the post): both tensors hold F(v), E4M3-exact (results/prefix-post-20260930) */
#endif
#if CW_PREPOST_BYTE && (!CW_VEC_INPUT || CW_TAIL_QUAD)
#error "CW_PREPOST_BYTE needs CW_VEC_INPUT and no CW_TAIL_QUAD"
#endif
#ifndef CW_PREFIX_TAIL_VEC
#define CW_PREFIX_TAIL_VEC 0 /* 1: prefix byte tail (main + down bytes) stored per lane as 8-channel groups: ds_load_b128 + cw_pack8 + 8-byte store instead of 64+16 per-channel byte loops; bytes unchanged (results/prefix-post-arith-20260930) */
#endif
#ifndef CW_FINISH_TAIL_VEC
#define CW_FINISH_TAIL_VEC 0 /* 1: finish_b8 / finish_dcrop_b8d byte tails (main bytes, DownCrop down bytes) per lane as 8-channel groups, same as CW_PREFIX_TAIL_VEC; bytes unchanged (results/tail-vec-20260930) */
#endif
#ifndef CW_UP_FUSED
#define CW_UP_FUSED 0
#endif
#ifndef CW_HOIST_UP
#define CW_HOIST_UP 0 /* hoist-loads 2026-09-30, bitmask, c32_wave1_up_b8: 1 low-res input per k step read branch-free (clamped index, select 0);
 2 per qt/ci the skip bytes (clamped), upw[2048+c] and fw[8704+c] for the lane's 8 channels are read up front instead of one b32 -> s_wait_loadcnt 0 per element */
#endif
#ifndef CW_DIAG_ONLY
#define CW_DIAG_ONLY 0 /* 1: chain residual skips the all-zero off-diagonal K16 half (6 of 12 WMMA); results/c32-diag-zero-20260925 proved the float32 bits equal for every finite E4M3 input */
#endif
#ifndef CW_SKIP_BYTE
#define CW_SKIP_BYTE 0 /* 1: extra exports c32_wave1_finish_dcrop_b8 / c32_wave1_up_b8: block4 main (the C32 skip) stored as E4M3 bytes fp8(F(v)) instead of f32 F(v); F(v) is E4M3-exact, so the up reads back the same value (results/c32-align-20260930) */
#endif
#if CW_SKIP_BYTE && CW_TAIL_QUAD
#error "CW_SKIP_BYTE: byte main store only on the non-quad finish tail"
#endif
DEV float cw_up_F(float x){uint a=bits(x)&0x7fffffffu;float c=__builtin_fminf(__builtin_fmaxf(x,-448.f),448.f);float r=__builtin_amdgcn_cvt_f32_fp8(__builtin_amdgcn_cvt_pk_fp8_f32(c,0.f,0,false),0);return a==0?0.f:r;}
DEV float cw_up_shuffle(float v,uint lane){return __builtin_bit_cast(float,__builtin_amdgcn_ds_bpermute(lane*4,__builtin_bit_cast(int,v)));}
template<bool Chain,bool Finish=false,bool DownCrop=false,bool Post=false,bool Prefix=false,bool Up=false,bool Byte=false,bool DownByte=false,bool InByte=false,bool UpLowByte=false,bool PostRGBA=false,bool PostTap=false>
DEV void cw_body(const unsigned char*in,const float*fw,const float*w,unsigned char*out,uint windows,uint mode,uint raw,uint width,uint height,uint sx,uint sy,uint prevw,uint px,uint py,float*main=nullptr,float*down=nullptr,const unsigned char*skip=nullptr,const float*scales=nullptr,const float*color=nullptr,const float*headw=nullptr,float*rgb=nullptr,float rgbscale=0.f,const float*history=nullptr,uint seed=0,uint temporal=0,const float*upw=nullptr,_Float16*post_features=nullptr,const float*logitw=nullptr,float*logit=nullptr){fp8_sat_mode();
 constexpr uint cw_kind=Prefix?5:Post?4:Finish?(DownCrop?3:2):Chain?0:1;
 uint win=bid();if(win>=windows)return;
 __attribute__((shared)) _Float16 residual[4*2*32*8];
 cw_i16 keys{},values{},queries{};
 f8 up_totals[2]{};
 if constexpr(Up){
  uint lane=__builtin_amdgcn_workitem_id_x(),r=lane&15u,g=lane>>4;
  uint ww=width+2*sx,iw=width/2,ih=height/2;
  int lx=(int((win%(ww/8))*8)-int(sx))/2+int(r%4),ly=(int((win/(ww/8))*8)-int(sy))/2+int(r/4);
  for(uint k=0;k<64;k+=16){h8 a{},b{};
#if CW_HOIST_UP&1
   {const bool ok=lx>=0&&ly>=0&&lx<int(iw)&&ly<int(ih);const uint base=ok?(uint(ly)*iw+uint(lx))*64:0u;for(uint e=0;e<8;e++){float v;if constexpr(UpLowByte)v=__builtin_amdgcn_cvt_f32_fp8(int(in[base+k+g*8+e]),0);else v=reinterpret_cast<const float*>(in)[base+k+g*8+e];a[e]=ok?(_Float16)v:(_Float16)0.f;}}
#else
   if(lx>=0&&ly>=0&&lx<int(iw)&&ly<int(ih))for(uint e=0;e<8;e++){const uint i=(uint(ly)*iw+uint(lx))*64+k+g*8+e;if constexpr(UpLowByte)a[e]=(_Float16)__builtin_amdgcn_cvt_f32_fp8(int(in[i]),0);else a[e]=(_Float16)reinterpret_cast<const float*>(in)[i];}
#endif
   for(uint ci=0;ci<2;ci++){
    __builtin_memcpy(&b,reinterpret_cast<const _Float16*>(upw)+(ci*16+r)*64+k+g*8,16);
    up_totals[ci]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(b,a,up_totals[ci]);
   }
  }
 }
#if CW_ROLL_WINDOW
 _Pragma("clang loop unroll(disable)")
#else
 _Pragma("unroll 4")
#endif
 for(uint qt=0;qt<4;qt++){
  uint lane=__builtin_amdgcn_workitem_id_x();
#if CW_LAUNDER
  __asm__ volatile("" : "+v"(lane));lane&=31u;
#endif
  uint r=lane&15u,g=lane>>4;
  i2 input[2];f8 ffn[2]{};
  if constexpr(Chain){for(uint ci=0;ci<2;ci++)input[ci]=cw_input(in,win,qt,ci,width,height,sx,sy,prevw,px,py,r,g);}
  else if constexpr(Prefix){
#if CW_PREFIX_SPLIT
   // B1 prefix split: fused_prefix_values ran on the g==0 half only (16 lanes idle) and then shipped values 8..15 with eight
   // bpermutes. Here both halves run the same instruction stream on the same token r: the half-wave selects which Box-Muller
   // pair it evaluates (g==1: a,c -> g0; g==0: b,d -> g1,g2), and g0 is the only value exchanged (one bpermute). Each value's
   // operation sequence is identical to fused_prefix_values, so the features, and everything downstream, are bit-identical.
   h8 a{};
   {const float*rgba=reinterpret_cast<const float*>(in);uint p=win*64+qt*16+r,tile=p/64,x=(tile%(width/8))*8+p%8,y=(tile/(width/8))*8+(p%64)/8,rgbp=y*width+x;
    uint h=production_pcg((x*0x8da6b343u)^(y*0xd8163841u)^(seed*0x9e3779b9u)^0x243f6a88u);
    float u1=production_uniform(g?h*0xcaa5b80du+0x21dd796bu:h*0x2c9277b5u+0xac564b05u),u2=production_uniform(g?h*0x83232c31u+0x3463e0acu:h*0xfa6dc5f9u+0x4712a88eu);
    float rr=__builtin_amdgcn_sqrtf(-2.f*(__builtin_amdgcn_logf(u1)*.6931471805599453f)),tt=(6.283185482025146f*u2)*.15915494309189535f;
    cw_pv_t gc=cw_prefix_rtz(rr*__builtin_amdgcn_cosf(tt)),gs=cw_prefix_rtz(rr*__builtin_amdgcn_sinf(tt));
    cw_pv_t g0=cw_prefix_shuffle(gc,r+16);
    cw_pv_t red=cw_prefix_rtz(Hrtz(Hrtz(rgba[rgbp*4])-.5f)*.125f),green=cw_prefix_rtz(Hrtz(Hrtz(rgba[rgbp*4+1])-.5f)*.125f),blue=cw_prefix_rtz(Hrtz(Hrtz(rgba[rgbp*4+2])-.5f)*.125f);
    cw_pv_t hr=red,hg=green,hb=blue;
    if(temporal){uint q=(y*width+x)*4;hr=cw_prefix_rtz(Hrtz(Hrtz(history[q])-.5f)*.125f);hg=cw_prefix_rtz(Hrtz(Hrtz(history[q+1])-.5f)*.125f);hb=cw_prefix_rtz(Hrtz(Hrtz(history[q+2])-.5f)*.125f);}
    const cw_pv_t lo[8]={gc,gs,hg,hb,g0,1,(cw_pv_t)dlss5_style_feature,1},hi[8]={red,green,1,1,blue,hr,1,0};
    for(uint e=0;e<8;e++)a[e]=(_Float16)(g?hi[e]:lo[e]);}
#else
   float pv[16]{};if(g==0)fused_prefix_values(reinterpret_cast<const float*>(in),history,win*64+qt*16+r,width,seed,temporal,pv);
   h8 a{};for(uint e=0;e<8;e++){float hi=__builtin_bit_cast(float,__builtin_amdgcn_ds_bpermute(r*4,__builtin_bit_cast(int,pv[8+e])));a[e]=(_Float16)(g?hi:pv[e]);}
#endif
   for(uint ci=0;ci<2;ci++){
    h8 b{};for(uint e=0;e<8;e++)b[e]=(_Float16)fw[(ci*16+r)*16+g*8+e];
    f8 pre=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(b,a,f8{});h8 zero{};
    pre=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(zero,zero,pre);
    CW_Q8_DECL(packed);for(uint e=0;e<8;e++){float v=Hrtz(pre[e]);CW_Q8_SET(packed,e,v);ffn[ci][e]=cw_clampf(v,-448.f,448.f)*fw[8704+ci*16+g*8+e];}CW_Q8_END(packed);input[ci]=packed;
   }
  }
  else if constexpr(Up){
   uint ww=width+2*sx,tok=qt*16+r,lo=(tok/16)*4+(tok%8)/2;
   int x=int((win%(ww/8))*8+tok%8)-int(sx),y=int((win/(ww/8))*8+tok/8)-int(sy);
   bool valid=x>=0&&y>=0&&x<int(width)&&y<int(height);
#if CW_HOIST_UP&2
   if constexpr(Byte){
   for(uint ci=0;ci<2;ci++){
    const uint c0=ci*16+g*8;unsigned long long sk;float uw[8],fv[8];
    __builtin_memcpy(&sk,__builtin_assume_aligned(skip+(valid?(uint(y)*width+uint(x))*32:0u)+c0,8),8);sk=valid?sk:0ull;
    __builtin_memcpy(uw,upw+2048+c0,32);__builtin_memcpy(fv,fw+8704+c0,32);
    CW_Q8_DECL(a);for(uint e=0;e<8;e++){
     float t=H(cw_up_shuffle(up_totals[ci][e],lo+g*16));
     float m=H(t+__builtin_amdgcn_cvt_f32_fp8(int((sk>>(8*e))&255u),0)*uw[e]);float v=valid?m:0.f;
     CW_Q8_SET(a,e,v);ffn[ci][e]=cw_clampf((CW_INPUT_HALF&4u)?v:float((_Float16)v),-448.f,448.f)*fv[e];
    }CW_Q8_END(a);input[ci]=a;
   }
   }else
#endif
   for(uint ci=0;ci<2;ci++){
    unsigned long long sk=0;if constexpr(Byte){if(valid)__builtin_memcpy(&sk,__builtin_assume_aligned(skip+(uint(y)*width+uint(x))*32+ci*16+g*8,8),8);}
    CW_Q8_DECL(a);for(uint e=0;e<8;e++){
     uint c=ci*16+g*8+e;float v=0.f,t=H(cw_up_shuffle(up_totals[ci][e],lo+g*16));
     if constexpr(Byte){if(valid)v=H(t+__builtin_amdgcn_cvt_f32_fp8(int((sk>>(8*e))&255u),0)*upw[2048+c]);}
     else if(valid)v=H(t+cw_up_F(reinterpret_cast<const float*>(skip)[(uint(y)*width+uint(x))*32+c])*upw[2048+c]);
     // Keep raw half-valued Up output for FFN residual; quantize only its matrix operand.
     CW_Q8_SET(a,e,v);ffn[ci][e]=cw_clampf((CW_INPUT_HALF&4u)?v:float((_Float16)v),-448.f,448.f)*fw[8704+c];
    }CW_Q8_END(a);input[ci]=a;
   }
  }
  else{
   uint ww=width+2*sx;int x=int((win%(ww/8))*8+(qt*16+r)%8)-int(sx),y=int((win/(ww/8))*8+(qt*16+r)/8)-int(sy);
   bool valid=x>=0&&y>=0&&x<int(width)&&y<int(height);
#if CW_VEC_INPUT
   // The lane's 8 channels c=ci*16+g*8..+7 are contiguous in every source (f32 features, FP8 skip bytes, scales, FFN residual
   // scale), 32-byte aligned (hipMalloc bases, channel offsets multiple of 8). Read them as whole rows (2x b128 / 1x b64)
   // instead of 8 per-element loads each; the per-element arithmetic and its order are unchanged.
   for(uint ci=0;ci<2;ci++){uint c0=ci*16+g*8;float src[8]{};
    if(valid){
     if constexpr(Post){uint p=uint(y)*width+uint(x),low=(uint(y)/2)*(width/2)+uint(x)/2;
      float lo[8],s0[8],s1[8];unsigned long long sk;
      if constexpr(InByte){unsigned long long lb;__builtin_memcpy(&lb,__builtin_assume_aligned(in+low*32+c0,8),8);for(uint e=0;e<8;e++)lo[e]=__builtin_amdgcn_cvt_f32_fp8(int((lb>>(8*e))&255u),0);}
      else __builtin_memcpy(lo,__builtin_assume_aligned(reinterpret_cast<const float*>(in)+low*32+c0,32),32);
      __builtin_memcpy(s0,__builtin_assume_aligned(scales+c0,32),32);__builtin_memcpy(s1,__builtin_assume_aligned(scales+32+c0,32),32);
      __builtin_memcpy(&sk,__builtin_assume_aligned(skip+p*32+c0,8),8);
      for(uint e=0;e<8;e++){float high=__builtin_amdgcn_cvt_f32_fp8(int((sk>>(8*e))&255u),0);src[e]=Hrtz(Hrtz(lo[e]*s0[e])+high*s1[e]);}
     }else if constexpr(InByte){unsigned long long ib;__builtin_memcpy(&ib,__builtin_assume_aligned(in+(uint(y)*width+uint(x))*32+c0,8),8);for(uint e=0;e<8;e++)src[e]=__builtin_amdgcn_cvt_f32_fp8(int((ib>>(8*e))&255u),0);}
     else __builtin_memcpy(src,__builtin_assume_aligned(reinterpret_cast<const float*>(in)+(uint(y)*width+uint(x))*32+c0,32),32);
    }
    float res[8];__builtin_memcpy(res,__builtin_assume_aligned(fw+8704+c0,32),32);
    CW_Q8_DECL(a);for(uint e=0;e<8;e++){float v=src[e];CW_Q8_SET(a,e,v);ffn[ci][e]=cw_clampf((CW_INPUT_HALF&(Post?1u:2u))?v:float((_Float16)v),-448.f,448.f)*res[e];}CW_Q8_END(a);input[ci]=a;}
#else
   for(uint ci=0;ci<2;ci++){CW_Q8_DECL(a);for(uint e=0;e<8;e++){uint c=ci*16+g*8+e;float v=0.f;if(valid){if constexpr(Post){uint p=uint(y)*width+uint(x),low=(uint(y)/2)*(width/2)+uint(x)/2;float high=__builtin_amdgcn_cvt_f32_fp8(int(skip[p*32+c]),0);v=Hrtz(Hrtz(reinterpret_cast<const float*>(in)[low*32+c]*scales[c])+high*scales[32+c]);}else v=reinterpret_cast<const float*>(in)[(uint(y)*width+uint(x))*32+c];}CW_Q8_SET(a,e,v);ffn[ci][e]=cw_clampf(float((_Float16)v),-448.f,448.f)*fw[8704+c];}CW_Q8_END(a);input[ci]=a;}
#endif
  }
  if constexpr(Chain)for(uint part=0;part<3;part++)for(uint ci=0;ci<2;ci++)for(uint kt=0;kt<2;kt++){
#if CW_DIAG_ONLY
   if(kt!=ci)continue; /* the three residual matrices are diagonal: the off-diagonal K16 half-block is all zero */
#endif
   i2 b=matrix8(fw,34944+((part*2+ci)*2+kt)*512+(g*16+r)*8);
   ffn[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,input[kt],ffn[ci]);
  }
#if CW_ROLL_HIDDEN
  _Pragma("clang loop unroll(disable)")
#endif
  for(uint ht=0;ht<8;ht++){
   f8 expanded{};for(uint kt=0;kt<2;kt++){i2 b=matrix8(fw+512,(ht*16+r)*32+kt*16+g*8);expanded=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,input[kt],expanded);}
#if CW_FAST_NUM&4
   /* FAST TIER (lossy): activation in packed half pairs (v_pk_max/min/fma/mul_f16), widened back to f32 for the fp8 pack */
   CW_Q8_DECL(a);for(uint e=0;e<8;e+=2){using hh=_Float16 __attribute__((ext_vector_type(2)));hh v={(_Float16)expanded[e],(_Float16)expanded[e+1]};
    hh g=__builtin_elementwise_min(__builtin_elementwise_max(v,hh{-4,-4}),hh{4,4});hh ag=__builtin_elementwise_abs(g);
    hh q=ag*hh{(_Float16)-.055908203125f,(_Float16)-.055908203125f}+hh{(_Float16).447265625f,(_Float16).447265625f};hh p=g*q+hh{(_Float16).89453125f,(_Float16).89453125f};hh o=v*p;
    CW_Q8_SET(a,e,float(o[0]));CW_Q8_SET(a,e+1,float(o[1]));}CW_Q8_END_SITE(a,1);
#else
   CW_Q8_DECL(a);for(uint e=0;e<8;e++){float v=expanded[e],g=cw_act_gate(v),q=__builtin_fmaf(absf(g),-.055908203125f,.447265625f),p=__builtin_fmaf(g,q,.89453125f);CW_Q8_SET(a,e,v*p);}CW_Q8_END_SITE(a,1);
#endif
   for(uint ci=0;ci<2;ci++){i2 b=matrix8(fw+4608,(ci*16+r)*128+ht*16+g*8);ffn[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,ffn[ci]);}
  }
  i2 feature[2];for(uint ci=0;ci<2;ci++){CW_Q8_DECL(a);
#if CW_RTZ_PAIR
   h8 rounded=cw_rtz_half8(ffn[ci]);__builtin_memcpy(residual+((qt*2+ci)*32+(g*16+r))*8,&rounded,16);
   for(uint e=0;e<8;e++)CW_Q8_SET(a,e,float(rounded[e]));
#else
   for(uint e=0;e<8;e++){float v=Hrtz(ffn[ci][e]);residual[((qt*2+ci)*32+(g*16+r))*8+e]=(_Float16)v;CW_Q8_SET(a,e,v);}
#endif
   CW_Q8_END_SITE(a,2);feature[ci]=a;}
  for(uint part=0;part<3;part++){
   uint qlane=__builtin_amdgcn_workitem_id_x();
#if CW_QKV_FENCE
   __asm__ volatile("" : "+v"(qlane));qlane&=31u;
#endif
   uint qr=qlane&15u,qg=qlane>>4;
   f8 q[2]{};for(uint ci=0;ci<2;ci++)for(uint kt=0;kt<2;kt++){i2 b=matrix8(w,part*1024+(ci*16+qr)*32+kt*16+qg*8);q[ci]=part<2?__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,feature[kt],q[ci]):__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(feature[kt],b,q[ci]);}
   f8 sum{};if(part<2)for(uint ci=0;ci<2;ci++){h8 squares{},ones{};for(uint e=0;e<8;e++){squares[e]=(_Float16)(q[ci][e]*q[ci][e]);ones[e]=(_Float16)1.f;}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,squares,sum);}
#if CW_NORM_HOIST
   float lane_rsq=part<2?__builtin_amdgcn_rsqf(maxf(sum[0],6.198883056640625e-5f)):0.f;
   for(uint ci=0;ci<2;ci++){CW_Q8_DECL(a);for(uint e=0;e<8;e++){float v=q[ci][e];if(part<2)v*=lane_rsq*(part==0?w[8192]:1.f);
#else
   for(uint ci=0;ci<2;ci++){CW_Q8_DECL(a);for(uint e=0;e<8;e++){float v=q[ci][e];if(part<2)v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*(part==0?w[8192]:1.f);
#endif
CW_Q8_SET(a,e,v);}CW_Q8_END_SITE(a,3);
    if(part==0){queries[qt*4+ci*2]=a[0];queries[qt*4+ci*2+1]=a[1];}else if(part==1){keys[qt*4+ci*2]=a[0];keys[qt*4+ci*2+1]=a[1];}else{values[qt*4+ci*2]=a[0];values[qt*4+ci*2+1]=a[1];}
   }
#if CW_QKV_FENCE
   __builtin_amdgcn_sched_barrier(0);
#endif
  }
  __builtin_amdgcn_sched_barrier(0);
 }
 _Pragma("clang loop unroll(disable)") for(uint qt=0;qt<4;qt++){
  uint lane=__builtin_amdgcn_workitem_id_x();
#if CW_LAUNDER
  __asm__ volatile("" : "+v"(lane));lane&=31u;
#endif
  uint r=lane&15u,g=lane>>4;
  i2 q[2];for(uint ci=0;ci<2;ci++)q[ci]=i2{queries[qt*4+ci*2],queries[qt*4+ci*2+1]};
  h8 ex[4];
  _Pragma("unroll 4") for(uint kt=0;kt<4;kt++){
   f8 acc{};for(uint ci=0;ci<2;ci++)acc=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12((i2{keys[kt*4+ci*2],keys[kt*4+ci*2+1]}),q[ci],acc);
   h8 a{};for(uint e=0;e<8;e++){uint row=qt*16+r,k=kt*16+g*8+e;float score=acc[e]+w[4096+row*64+k],affine=cw_clampf(score*.044921875f+1.30078125f,1.03125f,1.5693359375f);uint ah=(bits(affine)>>13)-0x1c000u;ushort hb=static_cast<ushort>(((ah<<5)+0x8000u)&65535u);a[e]=__builtin_bit_cast(_Float16,hb);}ex[kt]=a;
  }
  f8 sums{};h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;
  _Pragma("unroll 4") for(uint kt=0;kt<4;kt++)sums=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[kt],sums);
  i2 prob[4];_Pragma("unroll 4") for(uint kt=0;kt<4;kt++){CW_Q8_DECL(a);for(uint e=0;e<8;e++)CW_Q8_SET(a,e,float(ex[kt][e])*norm_inverse(sums[e]));CW_Q8_END_SITE(a,4);prob[kt]=a;}
  i2 av[2];for(uint ci=0;ci<2;ci++){f8 acc{};_Pragma("unroll 4") for(uint kt=0;kt<4;kt++)acc=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12((i2{values[kt*4+ci*2],values[kt*4+ci*2+1]}),prob[kt],acc);CW_Q8_DECL(a);for(uint e=0;e<8;e++)CW_Q8_SET(a,e,acc[e]);CW_Q8_END_SITE(a,5);av[ci]=a;}
  for(uint ci=0;ci<2;ci++){
   f8 acc{};for(uint kt=0;kt<2;kt++){i2 b=matrix8(w,3072+(ci*16+r)*32+kt*16+g*8);acc=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,av[kt],acc);}
   CW_Q8_DECL(a);
#if CW_RTZ_PAIR
   f8 summed;for(uint e=0;e<8;e++){float res=float(residual[((qt*2+ci)*32+(g*16+r))*8+e]),prod=res*w[8193+ci*16+g*8+e];summed[e]=acc[e]+prod;}
   h8 rounded=cw_rtz_half8(summed);
   if constexpr(Finish||Post)__builtin_memcpy(residual+((qt*2+ci)*32+(g*16+r))*8,&rounded,16);
   else for(uint e=0;e<8;e++)CW_Q8_SET(a,e,cw_output_value(float(rounded[e])));
#else
   for(uint e=0;e<8;e++){float res=float(residual[((qt*2+ci)*32+(g*16+r))*8+e]),prod=res*w[8193+ci*16+g*8+e],v=Hrtz(acc[e]+prod);if constexpr(Finish||Post)residual[((qt*2+ci)*32+(g*16+r))*8+e]=(_Float16)v;else CW_Q8_SET(a,e,cw_output_value(v));}
#endif
   if constexpr(!Finish&&!Post)CW_Q8_END_SITE(a,6);
   if constexpr(!Finish&&!Post)__builtin_memcpy(out+(win*64+qt*16+r)*32+ci*16+g*8,&a,8);
  }
  __builtin_amdgcn_sched_barrier(0);
 }

#if CW_POST_HEAD_VEC || CW_POST_FULL_TILE
 if constexpr(Post){
  WG_FENCE(3);WG_FENCE(2);
  uint lane=__builtin_amdgcn_workitem_id_x(),ww=width+2*sx,tx=(win%(ww/8))*8,ty=(win/(ww/8))*8;
  auto emit=[&](auto full){
  for(uint t=lane;t<64;t+=32){int x=int(tx+t%8)-int(sx),y=int(ty+t/8)-int(sy);if(full||(x>=0&&y>=0&&x<int(width)&&y<int(height))){
   uint p=uint(y)*width+uint(x);float sums[3]{};
   if constexpr(PostTap)for(uint c=0;c<32;c++)post_features[p*32+c]=residual[((t/16*2+c/16)*32+(c%16/8*16+t%16))*8+c%8];
#if CW_POST_HEAD_VEC
   for(uint cg=0;cg<4;cg++){h8 fs;float hr[8],hg[8],hb[8];uint c=cg*8;
    __builtin_memcpy(&fs,residual+((t/16*2+c/16)*32+(c%16/8*16+t%16))*8,16);
    __builtin_memcpy(hr,headw+c,32);__builtin_memcpy(hg,headw+32+c,32);__builtin_memcpy(hb,headw+64+c,32);
    for(uint e=0;e<8;e++){float f=float(fs[e]);sums[0]+=f*hr[e];sums[1]+=f*hg[e];sums[2]+=f*hb[e];}}
#else
   for(uint c=0;c<32;c++){float f=float(residual[((t/16*2+c/16)*32+(c%16/8*16+t%16))*8+c%8]);sums[0]+=f*headw[c];sums[1]+=f*headw[32+c];sums[2]+=f*headw[64+c];}
#endif
   for(uint c=0;c<3;c++){float acc=Hrtz(sums[c]),centered=color[p*4+c]*.125f-.0625f,v=(acc*rgbscale+centered)*8.f+.5f;rgb[p*(PostRGBA?4:3)+c]=cw_clampf(v,0.f,1.f);}if constexpr(PostRGBA)rgb[p*4+3]=1.f;
   if(logit){float value=0.f;
    for(uint c=0;c<32;c++){float f=float(residual[((t/16*2+c/16)*32+(c%16/8*16+t%16))*8+c%8]);value+=f*logitw[c];}
    logit[p*2]=value;logit[p*2+1]=Hrtz(value);
   }
  }}};
#if CW_POST_FULL_TILE
  if(tx>=sx&&ty>=sy&&tx+8<=width+sx&&ty+8<=height+sy)emit(cw_true{});else emit(cw_false{});
#else
  emit(cw_false{});
#endif
 }
#else
 if constexpr(Post){
  WG_FENCE(3);WG_FENCE(2);
  uint lane=__builtin_amdgcn_workitem_id_x(),ww=width+2*sx,tx=(win%(ww/8))*8,ty=(win/(ww/8))*8;
  for(uint t=lane;t<64;t+=32){int x=int(tx+t%8)-int(sx),y=int(ty+t/8)-int(sy);if(x>=0&&y>=0&&x<int(width)&&y<int(height)){
   uint p=uint(y)*width+uint(x);float sums[3]{};
   if constexpr(PostTap)for(uint c=0;c<32;c++)post_features[p*32+c]=residual[((t/16*2+c/16)*32+(c%16/8*16+t%16))*8+c%8];
   for(uint c=0;c<32;c++){float f=float(residual[((t/16*2+c/16)*32+(c%16/8*16+t%16))*8+c%8]);sums[0]+=f*headw[c];sums[1]+=f*headw[32+c];sums[2]+=f*headw[64+c];}
   for(uint c=0;c<3;c++){float acc=Hrtz(sums[c]),centered=color[p*4+c]*.125f-.0625f,v=(acc*rgbscale+centered)*8.f+.5f;rgb[p*(PostRGBA?4:3)+c]=cw_clampf(v,0.f,1.f);}if constexpr(PostRGBA)rgb[p*4+3]=1.f;
   if(logit){float value=0.f;
    for(uint c=0;c<32;c++){float f=float(residual[((t/16*2+c/16)*32+(c%16/8*16+t%16))*8+c%8]);value+=f*logitw[c];}
    logit[p*2]=value;logit[p*2+1]=Hrtz(value);
   }
  }}
 }
#endif
 if constexpr(CW_FINISH_FULL_TILE&&!Prefix){
 if constexpr(Finish){
  WG_FENCE(3);WG_FENCE(2);
  uint c=__builtin_amdgcn_workitem_id_x(),ww=width+2*sx,tx=(win%(ww/8))*8,ty=(win/(ww/8))*8;
  auto load=[&](uint t)->float{return float(residual[((t/16*2+c/16)*32+(c%16/8*16+t%16))*8+c%8]);};
#if CW_TAIL_QUAD
  auto put_main=[&](uint t,float raw_v){int x=int(tx+t%8)-int(sx),y=int(ty+t/8)-int(sy);if(x>=0&&y>=0&&x<int(width)&&y<int(height)){uint dest=(uint(y)*width+uint(x))*32+c;float v=F(raw_v);
   if constexpr(Prefix)*reinterpret_cast<unsigned char*>(reinterpret_cast<char*>(main)+dest)=static_cast<unsigned char>(fp8(v));else *reinterpret_cast<float*>(reinterpret_cast<char*>(main)+dest*4u)=v;}};
  for(uint k=0;k<16;k++){uint t=(k/4)*16+(k%4)*2;float a0=load(t),a1=load(t+1),b0=load(t+8),b1=load(t+9);
   if(main){put_main(t,a0);put_main(t+1,a1);put_main(t+8,b0);put_main(t+9,b1);}
   if(down){float top=Hrtz(a0+a1),bottom=Hrtz(b0+b1);uint x=tx+t%8,y=ty+t/8;
    if constexpr(DownCrop){int cx=int(x/2)-int(sx/2),cy=int(y/2)-int(sy/2);if(cx>=0&&cy>=0&&cx<int(width/2)&&cy<int(height/2))*reinterpret_cast<float*>(reinterpret_cast<char*>(down)+((uint(cy)*(width/2)+uint(cx))*32+c)*4u)=F(Hrtz(Hrtz(top+bottom)*.25f));}
    else *reinterpret_cast<float*>(reinterpret_cast<char*>(down)+(((y/2)*(ww/2)+x/2)*32+c)*4u)=F(Hrtz(Hrtz(top+bottom)*.25f));}
  }
  // The quad route above emits both outputs.
#endif
#if CW_FINISH_TAIL_VEC && !CW_TAIL_QUAD
  // Byte main (and byte DownCrop down) tail, vectorised like CW_PREFIX_TAIL_VEC: lane = (pixel slot c>>2, 8-channel group c&3),
  // one ds_load_b128 of 8 contiguous residual halves, one MODE-saturated cw_pack8, one 8-byte store; bounds tested per pixel
  // exactly as the per-channel loop. Bytes unchanged: fp8(F(x)) == fp8(x+0) for the finite half values x held here.
  if constexpr(Byte&&(DownByte||!DownCrop)){
   uint cg=c&3u,sl=c>>2;
   auto row=[&](uint t)->h8{h8 v;__builtin_memcpy(&v,__builtin_assume_aligned(residual+((t/16*2+cg/2)*32+((cg&1u)*16+t%16))*8,16),16);return v;};
   auto emitv=[&](auto full){
    if(main)for(uint i=0;i<8;i++){uint t=i*8+sl;int x=int(tx+t%8)-int(sx),y=int(ty+t/8)-int(sy);
     if(full||(x>=0&&y>=0&&x<int(width)&&y<int(height))){h8 hv=row(t);float q[8];for(uint e=0;e<8;e++)q[e]=float(hv[e])+0.f;
      i2 b=cw_pack8<0,cw_kind>(q);__builtin_memcpy(__builtin_assume_aligned(reinterpret_cast<unsigned char*>(main)+((uint(y)*width+uint(x))*32+cg*8),8),&b,8);}}
    if constexpr(DownByte&&DownCrop)if(down)for(uint j=0;j<2;j++){uint k=j*8+sl,t=(k/4)*16+(k%4)*2;uint x=tx+t%8,y=ty+t/8;int cx=int(x/2)-int(sx/2),cy=int(y/2)-int(sy/2);
     if(full||(cx>=0&&cy>=0&&cx<int(width/2)&&cy<int(height/2))){h8 a0=row(t),a1=row(t+1),b0=row(t+8),b1=row(t+9);float q[8];
      for(uint e=0;e<8;e++){float top=Hrtz(float(a0[e])+float(a1[e])),bottom=Hrtz(float(b0[e])+float(b1[e]));q[e]=Hrtz(Hrtz(top+bottom)*.25f)+0.f;}
      i2 b=cw_pack8<0,cw_kind>(q);__builtin_memcpy(__builtin_assume_aligned(reinterpret_cast<unsigned char*>(down)+((uint(cy)*(width/2)+uint(cx))*32+cg*8),8),&b,8);}}
   };
   if(tx>=sx&&ty>=sy&&tx+8<=width+sx&&ty+8<=height+sy&&(!DownCrop||((sx|sy)&1u)==0))emitv(cw_true{});else emitv(cw_false{});
   if constexpr(!DownCrop)if(down)for(uint k=0;k<16;k++){uint t=(k/4)*16+(k%4)*2;float top=Hrtz(load(t)+load(t+1)),bottom=Hrtz(load(t+8)+load(t+9));uint x=tx+t%8,y=ty+t/8;
    if constexpr(DownByte)reinterpret_cast<unsigned char*>(down)[((y/2)*(ww/2)+x/2)*32+c]=static_cast<unsigned char>(fp8(F(Hrtz(Hrtz(top+bottom)*.25f))));
    else down[((y/2)*(ww/2)+x/2)*32+c]=F(Hrtz(Hrtz(top+bottom)*.25f));}
   return;
  }
#endif
  auto emit=[&](auto full){if(main)for(uint t=0;t<64;t++){int x=int(tx+t%8)-int(sx),y=int(ty+t/8)-int(sy);if(full||(Prefix&&CW_PREFIX_FULL_TILE)||(x>=0&&y>=0&&x<int(width)&&y<int(height))){uint dest=(uint(y)*width+uint(x))*32+c;float v=load(t);if constexpr(Prefix)reinterpret_cast<unsigned char*>(main)[dest]=static_cast<unsigned char>(cw_prefix_byte(v));else if constexpr(Byte)reinterpret_cast<unsigned char*>(main)[dest]=static_cast<unsigned char>(fp8(F(v)));else main[dest]=F(v);}}
  if(!CW_TAIL_QUAD&&down)for(uint k=0;k<16;k++){uint t=(k/4)*16+(k%4)*2;float top=Hrtz(load(t)+load(t+1)),bottom=Hrtz(load(t+8)+load(t+9));uint x=tx+t%8,y=ty+t/8;
   if constexpr(DownCrop){int cx=int(x/2)-int(sx/2),cy=int(y/2)-int(sy/2);if(full||(cx>=0&&cy>=0&&cx<int(width/2)&&cy<int(height/2))){float dv=F(Hrtz(Hrtz(top+bottom)*.25f));uint di=(uint(cy)*(width/2)+uint(cx))*32+c;if constexpr(DownByte)reinterpret_cast<unsigned char*>(down)[di]=static_cast<unsigned char>(fp8(dv));else down[di]=dv;}}
   else if constexpr(DownByte)reinterpret_cast<unsigned char*>(down)[((y/2)*(ww/2)+x/2)*32+c]=static_cast<unsigned char>(fp8(F(Hrtz(Hrtz(top+bottom)*.25f))));
   else down[((y/2)*(ww/2)+x/2)*32+c]=F(Hrtz(Hrtz(top+bottom)*.25f));
  }
  };
#if !CW_TAIL_QUAD
  // DownCrop uses sx/2,sy/2; the production shifts are even. Keep the generic edge path for odd shifts.
  if(tx>=sx&&ty>=sy&&tx+8<=width+sx&&ty+8<=height+sy&&(!DownCrop||((sx|sy)&1u)==0))emit(cw_true{});else emit(cw_false{});
#endif
 }
 }else{
 if constexpr(Finish){
  WG_FENCE(3);WG_FENCE(2);
  uint c=__builtin_amdgcn_workitem_id_x(),ww=width+2*sx,tx=(win%(ww/8))*8,ty=(win/(ww/8))*8;
  auto load=[&](uint t)->float{return float(residual[((t/16*2+c/16)*32+(c%16/8*16+t%16))*8+c%8]);};
#if CW_TAIL_QUAD
  auto put_main=[&](uint t,float raw_v){int x=int(tx+t%8)-int(sx),y=int(ty+t/8)-int(sy);if(x>=0&&y>=0&&x<int(width)&&y<int(height)){uint dest=(uint(y)*width+uint(x))*32+c;float v=F(raw_v);
   if constexpr(Prefix)*reinterpret_cast<unsigned char*>(reinterpret_cast<char*>(main)+dest)=static_cast<unsigned char>(fp8(v));else *reinterpret_cast<float*>(reinterpret_cast<char*>(main)+dest*4u)=v;}};
  for(uint k=0;k<16;k++){uint t=(k/4)*16+(k%4)*2;float a0=load(t),a1=load(t+1),b0=load(t+8),b1=load(t+9);
   if(main){put_main(t,a0);put_main(t+1,a1);put_main(t+8,b0);put_main(t+9,b1);}
   if(down){float top=Hrtz(a0+a1),bottom=Hrtz(b0+b1);uint x=tx+t%8,y=ty+t/8;
    if constexpr(DownCrop){int cx=int(x/2)-int(sx/2),cy=int(y/2)-int(sy/2);if(cx>=0&&cy>=0&&cx<int(width/2)&&cy<int(height/2))*reinterpret_cast<float*>(reinterpret_cast<char*>(down)+((uint(cy)*(width/2)+uint(cx))*32+c)*4u)=F(Hrtz(Hrtz(top+bottom)*.25f));}
    else *reinterpret_cast<float*>(reinterpret_cast<char*>(down)+(((y/2)*(ww/2)+x/2)*32+c)*4u)=F(Hrtz(Hrtz(top+bottom)*.25f));}
  }
  if(false)
#endif
#if CW_PREFIX_TAIL_VEC
  // Prefix byte tail, vectorised: lane = (pixel slot c>>2, 8-channel group c&3); one ds_load_b128 of 8 contiguous residual
  // halves, one MODE-saturated cw_pack8 (same bytes as fp8() for the finite half values held here, the site-2 argument),
  // one 8-byte store; 8 lanes x 4 groups cover a 256-byte pixel row. Values per byte unchanged: main fp8(v+0),
  // down fp8(F(x)) == fp8(x+0) for finite x (F only differs at +-0, where both give 0x00).
  if constexpr(Prefix&&DownByte&&CW_PREFIX_FULL_TILE){
   uint cg=c&3u,sl=c>>2;
   auto row=[&](uint t)->h8{h8 v;__builtin_memcpy(&v,__builtin_assume_aligned(residual+((t/16*2+cg/2)*32+((cg&1u)*16+t%16))*8,16),16);return v;};
   if(main)for(uint i=0;i<8;i++){uint t=i*8+sl;h8 hv=row(t);float q[8];for(uint e=0;e<8;e++)q[e]=float(hv[e])+0.f;
    i2 b=cw_pack8<0,cw_kind>(q);__builtin_memcpy(__builtin_assume_aligned(reinterpret_cast<unsigned char*>(main)+((ty+t/8)*width+tx+t%8)*32+cg*8,8),&b,8);}
   if(down)for(uint j=0;j<2;j++){uint k=j*8+sl,t=(k/4)*16+(k%4)*2;h8 a0=row(t),a1=row(t+1),b0=row(t+8),b1=row(t+9);float q[8];
    for(uint e=0;e<8;e++){float top=Hrtz(float(a0[e])+float(a1[e])),bottom=Hrtz(float(b0[e])+float(b1[e]));q[e]=Hrtz(Hrtz(top+bottom)*.25f)+0.f;}
    uint x=tx+t%8,y=ty+t/8;i2 b=cw_pack8<0,cw_kind>(q);__builtin_memcpy(__builtin_assume_aligned(reinterpret_cast<unsigned char*>(down)+((y/2)*(ww/2)+x/2)*32+cg*8,8),&b,8);}
   return;
  }
#endif
  if(main)for(uint t=0;t<64;t++){int x=int(tx+t%8)-int(sx),y=int(ty+t/8)-int(sy);if((Prefix&&CW_PREFIX_FULL_TILE)||(x>=0&&y>=0&&x<int(width)&&y<int(height))){uint dest=(uint(y)*width+uint(x))*32+c;float v=load(t);if constexpr(Prefix)reinterpret_cast<unsigned char*>(main)[dest]=static_cast<unsigned char>(cw_prefix_byte(v));else if constexpr(Byte)reinterpret_cast<unsigned char*>(main)[dest]=static_cast<unsigned char>(fp8(F(v)));else main[dest]=F(v);}}
  if(!CW_TAIL_QUAD&&down)for(uint k=0;k<16;k++){uint t=(k/4)*16+(k%4)*2;float top=Hrtz(load(t)+load(t+1)),bottom=Hrtz(load(t+8)+load(t+9));uint x=tx+t%8,y=ty+t/8;
   if constexpr(DownCrop){int cx=int(x/2)-int(sx/2),cy=int(y/2)-int(sy/2);if(cx>=0&&cy>=0&&cx<int(width/2)&&cy<int(height/2)){float dv=F(Hrtz(Hrtz(top+bottom)*.25f));uint di=(uint(cy)*(width/2)+uint(cx))*32+c;if constexpr(DownByte)reinterpret_cast<unsigned char*>(down)[di]=static_cast<unsigned char>(fp8(dv));else down[di]=dv;}}
   else if constexpr(DownByte)reinterpret_cast<unsigned char*>(down)[((y/2)*(ww/2)+x/2)*32+c]=static_cast<unsigned char>(fp8(F(Hrtz(Hrtz(top+bottom)*.25f))));
   else down[((y/2)*(ww/2)+x/2)*32+c]=F(Hrtz(Hrtz(top+bottom)*.25f));
  }
 }
 }
}
#define CW_ENTRY KERNEL __attribute__((amdgpu_flat_work_group_size(32,32)))
CW_ENTRY void c32_wave1_chain(const unsigned char*in,const float*fw,const float*w,unsigned char*out,uint windows,uint mode,uint raw,uint width,uint height,uint sx,uint sy,uint prevw,uint px,uint py){cw_body<true>(in,fw,w,out,windows,mode,raw,width,height,sx,sy,prevw,px,py);}
CW_ENTRY void c32_wave1_mapped(const unsigned char*in,const float*fw,const float*w,unsigned char*out,uint windows,uint mode,uint raw,uint width,uint height,uint sx,uint sy){cw_body<false>(in,fw,w,out,windows,mode,raw,width,height,sx,sy,0,0,0);}
#define CW_FINISH(NAME,DC) CW_ENTRY void NAME(const unsigned char*in,const float*fw,const float*w,float*main,float*down,uint windows,uint mode,uint raw,uint width,uint height,uint sx,uint sy,uint prevw,uint px,uint py){cw_body<true,true,DC>(in,fw,w,nullptr,windows,mode,raw,width,height,sx,sy,prevw,px,py,main,down);}
CW_FINISH(c32_wave1_finish,false)
CW_FINISH(c32_wave1_finish_dcrop,true)
CW_ENTRY void c32_wave1_post(const unsigned char*low,const unsigned char*skip,const float*scales,const float*fw,const float*w,const float*color,const float*headw,float*rgb,uint windows,uint width,uint height,uint sx,uint sy,float scale){cw_body<false,false,false,true>(low,fw,w,nullptr,windows,0,1,width,height,sx,sy,0,0,0,nullptr,nullptr,skip,scales,color,headw,rgb,scale);}
CW_ENTRY void c32_wave1_post_logit(const unsigned char*low,const unsigned char*skip,const float*scales,const float*fw,const float*w,const float*color,const float*headw,float*rgb,uint windows,uint width,uint height,uint sx,uint sy,float scale,const float*logitw,float*logit){cw_body<false,false,false,true>(low,fw,w,nullptr,windows,0,1,width,height,sx,sy,0,0,0,nullptr,nullptr,skip,scales,color,headw,rgb,scale,nullptr,0,0,nullptr,nullptr,logitw,logit);}
CW_ENTRY void c32_wave1_prefix(const unsigned char*rgba,const float*history,const float*fw,const float*w,float*main,float*down,uint windows,uint mode,uint raw,uint width,uint height,uint seed,uint temporal){cw_body<false,true,false,false,true>(rgba,fw,w,nullptr,windows,mode,raw,width,height,0,0,0,0,0,main,down,nullptr,nullptr,nullptr,nullptr,nullptr,0.f,history,seed,temporal);}

#if CW_SKIP_BYTE
CW_ENTRY void c32_wave1_finish_dcrop_b8(const unsigned char*in,const float*fw,const float*w,unsigned char*main,float*down,uint windows,uint mode,uint raw,uint width,uint height,uint sx,uint sy,uint prevw,uint px,uint py){cw_body<true,true,true,false,false,false,true>(in,fw,w,nullptr,windows,mode,raw,width,height,sx,sy,prevw,px,py,reinterpret_cast<float*>(main),down);}
CW_ENTRY void c32_wave1_finish_dcrop_b8d(const unsigned char*in,const float*fw,const float*w,unsigned char*main,unsigned char*down,uint windows,uint mode,uint raw,uint width,uint height,uint sx,uint sy,uint prevw,uint px,uint py){cw_body<true,true,true,false,false,false,true,true>(in,fw,w,nullptr,windows,mode,raw,width,height,sx,sy,prevw,px,py,reinterpret_cast<float*>(main),reinterpret_cast<float*>(down));}
#endif
#if CW_PREPOST_BYTE
CW_ENTRY void c32_wave1_prefix_b8d(const unsigned char*rgba,const float*history,const float*fw,const float*w,unsigned char*main,unsigned char*down,uint windows,uint mode,uint raw,uint width,uint height,uint seed,uint temporal){cw_body<false,true,false,false,true,false,false,true>(rgba,fw,w,nullptr,windows,mode,raw,width,height,0,0,0,0,0,reinterpret_cast<float*>(main),reinterpret_cast<float*>(down),nullptr,nullptr,nullptr,nullptr,nullptr,0.f,history,seed,temporal);}
CW_ENTRY void c32_wave1_mapped_b8(const unsigned char*in,const float*fw,const float*w,unsigned char*out,uint windows,uint mode,uint raw,uint width,uint height,uint sx,uint sy){cw_body<false,false,false,false,false,false,false,false,true>(in,fw,w,out,windows,mode,raw,width,height,sx,sy,0,0,0);}
CW_ENTRY void c32_wave1_finish_b8(const unsigned char*in,const float*fw,const float*w,unsigned char*main,float*down,uint windows,uint mode,uint raw,uint width,uint height,uint sx,uint sy,uint prevw,uint px,uint py){cw_body<true,true,false,false,false,false,true>(in,fw,w,nullptr,windows,mode,raw,width,height,sx,sy,prevw,px,py,reinterpret_cast<float*>(main),down);}
CW_ENTRY void c32_wave1_post_b8(const unsigned char*low,const unsigned char*skip,const float*scales,const float*fw,const float*w,const float*color,const float*headw,float*rgb,uint windows,uint width,uint height,uint sx,uint sy,float scale){cw_body<false,false,false,true,false,false,false,false,true>(low,fw,w,nullptr,windows,0,1,width,height,sx,sy,0,0,0,nullptr,nullptr,skip,scales,color,headw,rgb,scale);}
CW_ENTRY void c32_wave1_post_b8_logit(const unsigned char*low,const unsigned char*skip,const float*scales,const float*fw,const float*w,const float*color,const float*headw,float*rgb,uint windows,uint width,uint height,uint sx,uint sy,float scale,const float*logitw,float*logit){cw_body<false,false,false,true,false,false,false,false,true>(low,fw,w,nullptr,windows,0,1,width,height,sx,sy,0,0,0,nullptr,nullptr,skip,scales,color,headw,rgb,scale,nullptr,0,0,nullptr,nullptr,logitw,logit);}
CW_ENTRY void c32_wave1_post_b8_rgba(const unsigned char*low,const unsigned char*skip,const float*scales,const float*fw,const float*w,const float*color,const float*headw,float*rgb,uint windows,uint width,uint height,uint sx,uint sy,float scale){cw_body<false,false,false,true,false,false,false,false,true,false,true>(low,fw,w,nullptr,windows,0,1,width,height,sx,sy,0,0,0,nullptr,nullptr,skip,scales,color,headw,rgb,scale);}
// Diagnostic/paired tap: final post features before the head. Old RGB ABI and arithmetic stay intact.
CW_ENTRY void c32_wave1_post_b8_features(const unsigned char*low,const unsigned char*skip,const float*scales,const float*fw,const float*w,const float*color,const float*headw,float*rgb,uint windows,uint width,uint height,uint sx,uint sy,float scale,_Float16*features){cw_body<false,false,false,true,false,false,false,false,true,false,false,true>(low,fw,w,nullptr,windows,0,1,width,height,sx,sy,0,0,0,nullptr,nullptr,skip,scales,color,headw,rgb,scale,nullptr,0,0,nullptr,features);}

#endif
#if CW_UP_FUSED
#if CW_SKIP_BYTE
CW_ENTRY void c32_wave1_up_b8(const unsigned char*low,const float*upw,const unsigned char*skip,const float*fw,const float*aw,unsigned char*out,uint windows,uint width,uint height,uint sx,uint sy){cw_body<false,false,false,false,false,true,true>(low,fw,aw,out,windows,0,1,width,height,sx,sy,0,0,0,nullptr,nullptr,skip,nullptr,nullptr,nullptr,nullptr,0.f,nullptr,0,0,upw);}
#endif
CW_ENTRY void c32_wave1_up(const unsigned char*low,const float*upw,const float*skip,const float*fw,const float*aw,unsigned char*out,uint windows,uint width,uint height,uint sx,uint sy){cw_body<false,false,false,false,false,true>(low,fw,aw,out,windows,0,1,width,height,sx,sy,0,0,0,nullptr,nullptr,reinterpret_cast<const unsigned char*>(skip),nullptr,nullptr,nullptr,nullptr,0.f,nullptr,0,0,upw);}
/* CW_UP_LOW_BYTES (2026-10-01, swin-body-gap): the fused Up's low-resolution input (block 65, post 0: F(Hrtz), on the E4M3 lattice)
   arrives as E4M3 bytes; cvt back gives the same (_Float16) operand. New exports only (*_lb); the host uses them only when present. */
#ifndef CW_UP_LOW_BYTES
#define CW_UP_LOW_BYTES 0
#endif
#if CW_UP_LOW_BYTES
CW_ENTRY void c32_wave1_up_lb(const unsigned char*low,const float*upw,const float*skip,const float*fw,const float*aw,unsigned char*out,uint windows,uint width,uint height,uint sx,uint sy){cw_body<false,false,false,false,false,true,false,false,false,true>(low,fw,aw,out,windows,0,1,width,height,sx,sy,0,0,0,nullptr,nullptr,reinterpret_cast<const unsigned char*>(skip),nullptr,nullptr,nullptr,nullptr,0.f,nullptr,0,0,upw);}
#if CW_SKIP_BYTE
CW_ENTRY void c32_wave1_up_b8_lb(const unsigned char*low,const float*upw,const unsigned char*skip,const float*fw,const float*aw,unsigned char*out,uint windows,uint width,uint height,uint sx,uint sy){cw_body<false,false,false,false,false,true,true,false,false,true>(low,fw,aw,out,windows,0,1,width,height,sx,sy,0,0,0,nullptr,nullptr,skip,nullptr,nullptr,nullptr,nullptr,0.f,nullptr,0,0,upw);}
#endif
#endif
#endif
