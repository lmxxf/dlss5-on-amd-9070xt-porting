#ifndef VIT_CONTRACT_OCC_LDS
#define VIT_CONTRACT_OCC_LDS 0 /* bytes of dummy LDS on vit_contract_blocked_body (occupancy cap) */
#endif
// Production ViT matrix math. FP8 MLP, F16 QKV, raw FP32 accumulation.
// Flat grid ceil(output_work_items/256), block32. One block = 16 tokens x 16 channels.
// Tokens and output channels must be divisible by16; no scalar GEMM fallback.
#pragma clang fp contract(off)
#pragma clang fp reassociate(off)
typedef unsigned uint;
#define DEV __attribute__((device)) __attribute__((always_inline))
#define KERNEL extern "C" __attribute__((global))
#ifndef HIP_LDS_FENCE
#define HIP_LDS_FENCE 1 /* 2026-09-22 fence scope: workgroup fences here only publish LDS; the default "workgroup" scope made gfx12 emit global_inv (vector L0 invalidate) and wait for all outstanding global loads at every barrier. Restricting the fence to the LDS address space removes both; bit-exact, see Development/results/fence-scope-all-20260922 */
#endif
#if HIP_LDS_FENCE
#define WG_FENCE(k) __builtin_amdgcn_fence(k,"workgroup","local")
#else
#define WG_FENCE(k) __builtin_amdgcn_fence(k,"workgroup")
#endif
DEV uint bits(float x){union{float f;uint u;}v;v.f=x;return v.u;}
DEV float value(uint x){union{float f;uint u;}v;v.u=x;return v.f;}
DEV float absf(float x){return value(bits(x)&0x7fffffffu);}
DEV float maxf(float a,float b){return a>b?a:b;}
DEV float minf(float a,float b){return a<b?a:b;}
DEV float clampf(float a,float lo,float hi){return minf(maxf(a,lo),hi);}
DEV uint shift_rne(uint x,uint n){if(n>31)return 0;uint q=x>>n,mask=(1u<<n)-1u,r=x&mask,mid=1u<<(n-1);return q+uint(r>mid||(r==mid&&(q&1u)));}
DEV uint half_bits(float x){uint b=bits(x),a=b&0x7fffffffu,s=(b>>16)&0x8000u;int e=int(a>>23)-127;
 if(a>=0x7f800000u)return s|0x7c00u|((a&0x7fffffu)?0x200u:0u);
 if(e<-25)return s;
 if(e<-14)return s|shift_rne((a&0x7fffffu)|0x800000u,uint(-e-1));
 if(e>15)return s|0x7c00u;
 uint h=((a+0xfffu+((a>>13)&1u))>>13)-0x1c000u;return s|(h>=0x7c00u?0x7c00u:h);
}
DEV float from_half(uint h){uint s=(h&0x8000u)<<16,e=(h>>10)&31u,m=h&1023u;if(e==31)return value(s|0x7f800000u|(m<<13));if(!e){if(!m)return value(s);float f=float(m)*5.9604644775390625e-8f;return s?-f:f;}return value(s|((e+112)<<23)|(m<<13));}
#ifdef HIP_ISA_HALF
// Keep f32 expression evaluation and both rounding boundaries opaque to fusion.
DEV float H(float x){uint h;float result;asm("v_cvt_f16_f32 %0, %1" : "=v"(h) : "v"(x));asm("v_cvt_f32_f16 %0, %1" : "=v"(result) : "v"(h));return result;}
#else
DEV float H(float x){return from_half(half_bits(x));}
#endif
#ifndef HIP_NATIVE_FP8_F
#define HIP_NATIVE_FP8_F 1
#endif
#ifndef VIT_EXPAND_TAIL_VEC
#define VIT_EXPAND_TAIL_VEC 0 /* 1: vit_expand_blocked_body hidden bytes staged in LDS, 2 x b128 per lane; bytes unchanged (results/deep-tail2-20260930) */
#endif
#ifndef C512_T8_TAIL_VEC
#define C512_T8_TAIL_VEC 0 /* 1: split_ffn_fused_fp8_t8 / split_projection_frag tiled E4M3 copy staged in LDS, 8/16-byte stores per lane; bytes unchanged (results/deep-tail-20260930) */
#endif
#ifndef C512_T8_NO_F32
#define C512_T8_NO_F32 0 /* 1: split_ffn_fused_fp8_t8 skips the f32 contract store (host c512_proj_tiles: only the E4M3 tiles feed split_projection_frag; the f32 copy has no reader). results/gap-fusion-20260930 */
#endif
#ifndef HIP_VIT_GATHER_FOLD
#define HIP_VIT_GATHER_FOLD 0 /* 1: export decoder_project2x_h16w_gin (inverse ViT gather folded into the A-operand address). results/gap-fusion-20260930 */
#endif
#ifndef HIP_DEC_HOIST_SCALE
#define HIP_DEC_HOIST_SCALE 0 /* 1: decoder_project2x_h16w skip scale read once per lane */
#endif
#ifndef HIP_VIT_HOIST_SCALE
#define HIP_VIT_HOIST_SCALE 0 /* 1: vit_project_frag residual scale and the QKV frag kernel's row scale read once per lane instead of per output element */
#endif
#ifndef HIP_BRANCHLESS_F
#define HIP_BRANCHLESS_F 1 /* 2026-09-17: with the two multihead files -0.12ms, bit-exact. F() as clamp+cvt+cndmask instead of a divergent branch pair per call (same values for every non-NaN input; NaN gives -448 instead of +-448) */
#endif
#if HIP_NATIVE_FP8_F && HIP_BRANCHLESS_F
DEV float F(float x){uint a=bits(x)&0x7fffffffu;float c=__builtin_fminf(__builtin_fmaxf(x,-448.f),448.f);float r=__builtin_amdgcn_cvt_f32_fp8(__builtin_amdgcn_cvt_pk_fp8_f32(c,0.f,0,false),0);return a==0?0.f:r;}
#elif HIP_NATIVE_FP8_F
DEV float F(float x){
 uint b=bits(x),a=b&0x7fffffffu;
 if(a==0)return 0.f;
 // Preserve the existing signed saturation, including nonfinite inputs.
 if(a>=0x43e00000u)return value((b&0x80000000u)|0x43e00000u);
 int packed=__builtin_amdgcn_cvt_pk_fp8_f32(x,0.f,0,false);
 return __builtin_amdgcn_cvt_f32_fp8(packed,0);
}
#else
DEV float F(float x){uint b=bits(x),a=b&0x7fffffffu,s=b&0x80000000u;if(a==0)return 0.f;
 if(a>=0x43e00000u)return value(s|0x43e00000u); // finite E4M3 saturation, including overflow
 if(a<0x3c800000u){float scaled=absf(x)*512.f;uint q=uint(scaled);float r=scaled-float(q);q+=uint(r>.5f||(r==.5f&&(q&1u)));float f=float(q)/512.f;return s?-f:f;}
 uint r=(a+0x7ffffu+((a>>20)&1u))&0xfff00000u;return value(s|(r>0x43e00000u?0x43e00000u:r));}
#endif

using f8=float __attribute__((ext_vector_type(8)));
using h8=_Float16 __attribute__((ext_vector_type(8)));
using i2=int __attribute__((ext_vector_type(2)));
#define WAVE KERNEL __attribute__((amdgpu_flat_work_group_size(32,32)))
DEV uint rc(){return __builtin_amdgcn_workitem_id_x()&15u;}
DEV uint gr(){return (__builtin_amdgcn_workitem_id_x()>>4)&1u;}
DEV uint bid(){return __builtin_amdgcn_workgroup_id_x();}
DEV void pack(i2&x,uint e,float v){uint b=uint(__builtin_amdgcn_cvt_pk_fp8_f32(clampf(v,-448.f,448.f),0.f,0,false))&255;x[e/4]=int(uint(x[e/4])|(b<<(8*(e%4))));}
// DF_PACK8 (2026-09-27, results/pack8-20260927): an 8-byte fragment filled by pack() from zero, as two values per
// v_cvt_pk_fp8_f32 straight into the words (same clamp, same per-element conversion) instead of cvt + and/shift/or per byte.
#ifndef DF_PACK8
#define DF_PACK8 0
#endif
#if DF_PACK8
DEV float df_z(float v){return clampf(v,-448.f,448.f);}
DEV i2 df_pack8(const float*t){int lo=__builtin_amdgcn_cvt_pk_fp8_f32(df_z(t[0]),df_z(t[1]),0,false);lo=__builtin_amdgcn_cvt_pk_fp8_f32(df_z(t[2]),df_z(t[3]),lo,true);
 int hi=__builtin_amdgcn_cvt_pk_fp8_f32(df_z(t[4]),df_z(t[5]),0,false);hi=__builtin_amdgcn_cvt_pk_fp8_f32(df_z(t[6]),df_z(t[7]),hi,true);i2 x;x[0]=lo;x[1]=hi;return x;}
#define DF_PACK8_LOOP(V,...) {float df_t_[8];for(uint e=0;e<8;e++)df_t_[e]=(__VA_ARGS__);V=df_pack8(df_t_);}
#else
#define DF_PACK8_LOOP(V,...) for(uint e=0;e<8;e++)pack(V,e,__VA_ARGS__)
#endif
template<bool ByteInput=false>
DEV f8 dot8(const float*in,const float*w,uint first,uint row,uint channels,uint begin,uint end,f8 a){
 for(uint k=begin;k<end;k+=16){i2 x{},y{};
  for(uint e=0;e<8;e++){uint j=k+gr()*8+e;if constexpr(!ByteInput)pack(x,e,in[(first+rc())*channels+j]);
#ifndef HIP_PREPACKED_WEIGHTS
   pack(y,e,w[(row+rc())*channels+j]);
#endif
  }
if constexpr(ByteInput){const uint*pi=reinterpret_cast<const uint*>(in);uint off=((first+rc())*channels+k+gr()*8)/4;x={int(pi[off]),int(pi[off+1])};}
#ifdef HIP_PREPACKED_WEIGHTS
  const uint*pw=reinterpret_cast<const uint*>(w);uint offset=((row+rc())*channels+k+gr()*8)/4;y={int(pw[offset]),int(pw[offset+1])};
#endif
  a=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(x,y,a);
 }return a;
}
#ifdef HIP_VIT_EXPAND_PAIR
// Two adjacent output tiles reuse each input load/FP8 pack. Keep the legacy
// launch ABI: odd column tiles return; even tiles own both output regions.
// Each accumulator retains the original increasing-K WMMA order.
WAVE void vit_expand(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint tile=bid()%(outputs/16),first=bid()/(outputs/16)*16,row=tile*16;
 if(first>=tokens||(tile&1u))return;
 bool second=row+16<outputs;f8 a{},b{};
 for(uint k=0;k<inputs;k+=16){
  i2 x{},y{},z{};
  for(uint e=0;e<8;e++){
   uint j=k+gr()*8+e;pack(x,e,in[(first+rc())*inputs+j]);
#ifndef HIP_PREPACKED_WEIGHTS
   pack(y,e,w[(row+rc())*inputs+j]);
   if(second)pack(z,e,w[(row+16+rc())*inputs+j]);
#endif
  }
#ifdef HIP_PREPACKED_WEIGHTS
  const uint*pw=reinterpret_cast<const uint*>(w);
  uint offset=((row+rc())*inputs+k+gr()*8)/4;
  y={int(pw[offset]),int(pw[offset+1])};
  if(second){offset+=16*inputs/4;z={int(pw[offset]),int(pw[offset+1])};}
#endif
  a=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(x,y,a);
  if(second)b=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(x,z,b);
 }
 for(uint e=0;e<8;e++){
  float v=a[e],g=clampf(v,-4.f,4.f),poly=__builtin_fmaf(g,__builtin_fmaf(absf(g),-.055908203125f,.447265625f),.89453125f);
  out[(first+gr()*8+e)*outputs+row+rc()]=F(v*poly);
  if(second){v=b[e];g=clampf(v,-4.f,4.f);poly=__builtin_fmaf(g,__builtin_fmaf(absf(g),-.055908203125f,.447265625f),.89453125f);
   out[(first+gr()*8+e)*outputs+row+16+rc()]=F(v*poly);}
 }
}
#else
WAVE void vit_expand(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;uint first=bid()/(outputs/16)*16,row=bid()%(outputs/16)*16;if(first>=tokens)return;f8 a{};a=dot8(in,w,first,row,inputs,0,inputs,a);for(uint e=0;e<8;e++){float v=a[e],g=clampf(v,-4.f,4.f),poly=__builtin_fmaf(g,__builtin_fmaf(absf(g),-.055908203125f,.447265625f),.89453125f);out[(first+gr()*8+e)*outputs+row+rc()]=F(v*poly);}}
#endif
WAVE void vit_project(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;uint first=bid()/(outputs/16)*16,row=bid()%(outputs/16)*16;if(first>=tokens)return;f8 total{};
 if(inputs==4096){for(uint e=0;e<8;e++)total[e]=H(skip[(first+gr()*8+e)*outputs+row+rc()]*w[inputs*outputs+row+rc()]);for(uint p=0;p<4;p++){f8 a{};a=dot8(in,w,first,row,inputs,p*(inputs/4),(p+1)*(inputs/4),a);for(uint e=0;e<8;e++)total[e]+=a[e];}}
 else{for(uint p=0;p<4;p++){f8 a{};if(!p)for(uint e=0;e<8;e++)a[e]=H(skip[(first+gr()*8+e)*outputs+row+rc()]*w[inputs*outputs+row+rc()]);a=dot8(in,w,first,row,inputs,p*(inputs/4),(p+1)*(inputs/4),a);for(uint e=0;e<8;e++)total[e]=p?total[e]+a[e]:a[e];}}
 for(uint e=0;e<8;e++)out[(first+gr()*8+e)*outputs+row+rc()]=F(H(total[e]));
}
// Production fused QKV keeps F32 dot until square normalization; no two512 partition rounds.
WAVE void vit_qkv_project(const float*in,const float*w,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;uint perpart=(tokens/16)*64,part=bid()/perpart,local=bid()%perpart,first=local/64*16,row=local%64*16;if(part>=3)return;f8 a{};for(uint k=0;k<1024;k+=16){h8 x{},y{};for(uint e=0;e<8;e++){uint j=k+gr()*8+e;x[e]=(_Float16)in[(first+rc())*1024+j];y[e]=(_Float16)w[part*1048576+(row+rc())*1024+j];}a=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,y,a);}for(uint e=0;e<8;e++)out[(part*tokens+first+gr()*8+e)*1024+row+rc()]=a[e];}
WAVE void vit_qkv_normalize(const float*in,const float*w,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;uint perpart=(tokens/16)*64,part=bid()/perpart,local=bid()%perpart,first=local/64*16,row=local%64*16,head=row/32;if(part>=3)return;f8 sum{};if(part<2){for(uint k=0;k<32;k+=16){h8 x{},ones{};for(uint e=0;e<8;e++){uint j=head*32+k+gr()*8+e;float v=in[(part*tokens+first+rc())*1024+j];x[e]=(_Float16)(v*v);ones[e]=(_Float16)1.f;}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,sum);}}
 for(uint e=0;e<8;e++){uint i=(part*tokens+first+gr()*8+e)*1024+row+rc();float a=in[i];if(part<2){float scale=part==0?5.65625f*w[3145728+head]:1.f;a*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*scale;}out[i]=F(a);}
}
// Explicit f32tof16 conversion in production attention is RTZ, unlike Matrix.Cast<F16>.
#ifndef HIP_NATIVE_RTZ
#define HIP_NATIVE_RTZ 1
#endif
#if HIP_NATIVE_RTZ
DEV float Hrtz(float x){uint h;float r;asm("v_cvt_pkrtz_f16_f32 %0, %1, %1":"=v"(h):"v"(x));asm("v_cvt_f32_f16 %0, %1":"=v"(r):"v"(h));return r;}
#else
DEV float Hrtz(float x){uint b=bits(x),a=b&0x7fffffffu,s=(b>>16)&0x8000u;int e=int(a>>23)-127;if(a>=0x7f800000u)return from_half(s|0x7c00u|((a&0x7fffffu)?0x200u:0u));if(e>15)return from_half(s|0x7bffu);if(e<-24)return from_half(s);if(e<-14)return from_half(s|(((a&0x7fffffu)|0x800000u)>>uint(-e-1)));return from_half(s|((a>>13)-0x1c000u));}
#endif
// C512_F_MASK (2026-09-30, results/composite-quant-c512-20260930): F(Hrtz(x)) -> F(bits(x)&0xffffe000) at the C512 f32 exits whose x
// is a WMMA sum from +0 of E4M3-grid products (split_ffn_fused_fp8_t8 contract; split_mix_blocked_h16w_m32, whose FP8-valued
// inputs times half weights on a 2^-9 grid are also 2^-18 multiples). Exhaustive over all 2^32 patterns with the production
// instructions: the float results are identical except negative 0<|x|<2^-24 (+0 vs -0) and +NaN payloads, none of which a
// finite sum on the 2^-18 grid can produce. The half rounding is kept; only the f32->f16->f32 round trip goes.
#ifndef C512_F_MASK
#define C512_F_MASK 0
#endif
DEV float c512_hq(float x){
#if C512_F_MASK
 return __builtin_bit_cast(float,__builtin_bit_cast(uint,x)&0xffffe000u);
#else
 return Hrtz(x);
#endif
}
// scores work=T*T*32; T%16=0; ex[query][head32][key].
WAVE void vit_attention_scores_fast(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;uint kt= tokens/16,key=(bid()%kt)*16,head=(bid()/kt)%32,first=bid()/(kt*32)*16;if(first>=tokens)return;f8 a{};for(uint k=0;k<32;k+=16){i2 x{},y{};for(uint e=0;e<8;e++){uint j=k+gr()*8+e;pack(x,e,in[(first+rc())*1024+head*32+j]);pack(y,e,in[(tokens+key+rc())*1024+head*32+j]);}a=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(x,y,a);}for(uint e=0;e<8;e++){float af=clampf(a[e]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);uint hb=(bits(af)>>13)-0x1c000u;out[((first+gr()*8+e)*32+head)*tokens+key+rc()]=from_half(((hb<<4)+0x4000u)&65535u);}}
// inverse SPECIAL work=T*32*16, not T*32. One wave handles16 queries for one head.
WAVE void vit_attention_inverse_fast(const float*ex,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;uint head=bid()%32,first=bid()/32*16;if(first>=tokens)return;f8 a{};for(uint k=0;k<tokens;k+=16){h8 x{},ones{};for(uint e=0;e<8;e++){uint key=k+gr()*8+e;x[e]=(_Float16)ex[((first+rc())*32+head)*tokens+key];ones[e]=(_Float16)1.f;}a=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,a);}if(rc()==0)for(uint e=0;e<8;e++)out[(first+gr()*8+e)*32+head]=1.f/a[e];}
// FUSED ViT attention (host option vit_attn_fused): one wave = 16 queries x one head over every key, bid = query tile*32 + head.
// Same per-element operations, operand packing and MMA K order as vit_attention_scores_fast / vit_attention_inverse_fast /
// vit_attention_av_fast: Q packed once, scores per16-key tile through the same exp bit map, the half bit pattern those
// kernels exchange through the f32 ex tensor kept in LDS ([query16][key] uint16, exact: ex is an f32 copy of a half),
// denominator = f16 MMA of the LDS rows against ones (K16 steps in key order), PV = FP8 MMA of pack(ex) against V for
// both16-column halves of the head, epilogue F(Hrtz(acc*inv)). Requires tokens%16==0 and tokens<=640.
// LDS per wave: P as E4M3 bytes [16][MAXT+16] plus a double-buffered16x16 half tile that transposes each score tile from
// accumulator layout to the A-operand layout (keys along K) for the denominator MMA and the byte store. Three token
// buckets keep the static LDS (and so the occupancy) matched to the geometry: 256 (8.2KB), 400 (8.2KB), 640 (12KB).
// f-sweep 2026-09-30 (results/f-sweep-20260930): HIP_BYTE_F_ADD0 1 stores byte_F(x) and the decoder byte as
// cvt_pk_fp8(med3(x+0,-448,448)) instead of cvt_pk_fp8(F(x)): F only turns -0 into +0 before a second (idempotent)
// E4M3 rounding, the +0 add does the same; GPU exhaustive over all 2^32 x. The add stays apart from any producer mul.
#ifndef HIP_BYTE_F_ADD0
#define HIP_BYTE_F_ADD0 0
#endif
DEV float fp8_add0(float x){
#pragma clang fp contract(off)
 return __builtin_amdgcn_fmed3f(x+0.f,-448.f,448.f);}
// HIP_VIT_ATTN_RCP 1: ViT attention 1/sum as rcp + two Newton steps (c32 norm_inverse). sum = f16 MMA of up to 640
// probabilities in [0x1c20,0x3e90] half = [0.00403,1.640625], so sum is in [1/256,2048]; checked bit-for-bit against
// 1.f/x for every float in that interval on gfx1201 (probe_r).
#ifndef HIP_VIT_ATTN_RCP
#define HIP_VIT_ATTN_RCP 0
#endif
DEV float vit_inv(float x){
#if HIP_VIT_ATTN_RCP
 float r=__builtin_amdgcn_rcpf(x);r=__builtin_fmaf(r,__builtin_fmaf(-x,r,1.f),r);return __builtin_fmaf(r,__builtin_fmaf(-x,r,1.f),r);
#else
 return 1.f/x;
#endif
}
// ByteInput: Q/K/V arrive as the E4M3 bytes vit_qkv_project_normalize_fused_f16compact_fp8 stores (byte_F of the same
// F() values the f32 tensor holds, so pack() of the f32 value and the stored byte are identical): Q/K rows are one
// 8-byte load per lane, V columns eight byte loads placed exactly as pack() places them.
#ifndef HIP_VIT_ATTN_PROB_PAIR
#define HIP_VIT_ATTN_PROB_PAIR 0
#endif
#ifndef HIP_VIT_ATTN_TRANSPOSED_AV
#define HIP_VIT_ATTN_TRANSPOSED_AV 0
#endif
#ifndef HIP_VIT_ATTN_NATIVE_HALF
#define HIP_VIT_ATTN_NATIVE_HALF 0
#endif
#ifndef HIP_VIT_ATTN_TRANSPOSED_SCORE
#define HIP_VIT_ATTN_TRANSPOSED_SCORE 0
#endif
#if HIP_VIT_ATTN_TRANSPOSED_SCORE
template<uint MAXT,bool ByteInput=false,bool ByteOut=false>
DEV void vit_attention_transposed_score_body(const float*in,float*out,uint tokens){
 uint head=bid()%32,first=bid()/32*16;if(first>=tokens)return;

 const unsigned char*in8=reinterpret_cast<const unsigned char*>(in);
 i2 q[2]{};for(uint k=0;k<32;k+=16){if constexpr(ByteInput)__builtin_memcpy(&q[k/16],in8+(first+rc())*1024+head*32+k+gr()*8,8);else for(uint e=0;e<8;e++){uint j=k+gr()*8+e;pack(q[k/16],e,in[(first+rc())*1024+head*32+j]);}}
 h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;
 f8 sum{},acc[2]{};
 for(uint key=0,tile=0;key<tokens;key+=16,tile^=1u){f8 a{};
  for(uint k=0;k<32;k+=16){i2 y{};if constexpr(ByteInput)__builtin_memcpy(&y,in8+(tokens+key+rc())*1024+head*32+k+gr()*8,8);else for(uint e=0;e<8;e++){uint j=k+gr()*8+e;pack(y,e,in[(tokens+key+rc())*1024+head*32+j]);}a=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(y,q[k/16],a);}
  // Swapping Q/K operands transposes the score output, directly matching
  // the A-fragment consumed by denominator and PV. Same K0 then K16 order.
  h8 x{};i2 xb{};
#if HIP_VIT_ATTN_PROB_PAIR
  f8 prob{};
#endif
  for(uint e=0;e<8;e++){
   float af=clampf(a[e]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);
   uint hb=(bits(af)>>13)-0x1c000u;unsigned short u=(unsigned short)(((hb<<4)+0x4000u)&65535u);
   union{unsigned short u;_Float16 h;}cv;cv.u=u;x[e]=cv.h;
#if HIP_VIT_ATTN_NATIVE_HALF
   // Clamp/bit-map confines u to 0x1c20..0x3e90 (positive normal half).
   // Exact widening; avoid the general software decoder's impossible cases.
   float probability=float(cv.h);
#else
   float probability=from_half(u);
#endif
#if HIP_VIT_ATTN_PROB_PAIR
   prob[e]=probability;
#else
   pack(xb,e,probability);
#endif
  }
#if HIP_VIT_ATTN_PROB_PAIR
  // The same bounded half values encoded two at a time; no tensor repacking.
  xb[0]=__builtin_amdgcn_cvt_pk_fp8_f32(prob[0],prob[1],0,false);
  xb[0]=__builtin_amdgcn_cvt_pk_fp8_f32(prob[2],prob[3],xb[0],true);
  xb[1]=__builtin_amdgcn_cvt_pk_fp8_f32(prob[4],prob[5],0,false);
  xb[1]=__builtin_amdgcn_cvt_pk_fp8_f32(prob[6],prob[7],xb[1],true);
#endif
#if HIP_VIT_ATTN_TRANSPOSED_AV
  sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,x,sum);
#else
  sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,sum);
#endif
  for(uint c=0;c<2;c++){i2 y{};for(uint e=0;e<8;e++){uint vkey=key+gr()*8+e;if constexpr(ByteInput){uint b=in8[(2*tokens+vkey)*1024+head*32+c*16+rc()];y[e/4]=int(uint(y[e/4])|(b<<(8*(e%4))));}else pack(y,e,in[(2*tokens+vkey)*1024+head*32+c*16+rc()]);}
#if HIP_VIT_ATTN_TRANSPOSED_AV
  acc[c]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(y,xb,acc[c]);
#else
  acc[c]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(xb,y,acc[c]);
#endif
}
 }
#if HIP_VIT_ATTN_TRANSPOSED_AV
 // Query is now in the lane, feature in the accumulator element. All eight
 // denominator rows are identical; keep the same K16 reduction, divide once.
 float inv=vit_inv(sum[0]);
 for(uint c=0;c<2;c++)for(uint e=0;e<8;e++){if constexpr(ByteOut)reinterpret_cast<unsigned char*>(out)[(first+rc())*1024+head*32+c*16+gr()*8+e]=byte_F(Hrtz(acc[c][e]*inv));else out[(first+rc())*1024+head*32+c*16+gr()*8+e]=F(Hrtz(acc[c][e]*inv));}
#else
 float inv[8];for(uint e=0;e<8;e++)inv[e]=vit_inv(sum[e]);

 for(uint c=0;c<2;c++)for(uint e=0;e<8;e++){if constexpr(ByteOut)reinterpret_cast<unsigned char*>(out)[(first+gr()*8+e)*1024+head*32+c*16+rc()]=byte_F(Hrtz(acc[c][e]*inv[e]));else out[(first+gr()*8+e)*1024+head*32+c*16+rc()]=F(Hrtz(acc[c][e]*inv[e]));}
#endif
}
#endif
// HIP_VIT_ATTN_M32 (2026-10-01, results/vit-1080-gap-20261001): mochizuki's vitattn takes 32 queries per group; here one
// wave keeps two independent 16-query chains (first, first+16) of the production transposed-score body and loads each
// K fragment / V fragment once for both. Per query every WMMA, its operands and the K16 order are those of
// vit_attention_transposed_score_body, so outputs are identical; grid unchanged (host), waves with pair>=tokens/32 exit.
#ifndef HIP_VIT_ATTN_M32
#define HIP_VIT_ATTN_M32 0
#endif
#if HIP_VIT_ATTN_M32
#if !(HIP_VIT_ATTN_TRANSPOSED_SCORE&&HIP_VIT_ATTN_NATIVE_HALF&&HIP_VIT_ATTN_PROB_PAIR&&HIP_VIT_ATTN_TRANSPOSED_AV)
#error HIP_VIT_ATTN_M32 is written for the production transposed-score/native-half/pair/transposed-AV body
#endif
DEV void vit_attn_m32_prob(f8 a,h8&x,i2&xb){f8 prob{};
 for(uint e=0;e<8;e++){float af=clampf(a[e]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);
  uint hb=(bits(af)>>13)-0x1c000u;unsigned short u=(unsigned short)(((hb<<4)+0x4000u)&65535u);
  union{unsigned short u;_Float16 h;}cv;cv.u=u;x[e]=cv.h;prob[e]=float(cv.h);}
 xb[0]=__builtin_amdgcn_cvt_pk_fp8_f32(prob[0],prob[1],0,false);xb[0]=__builtin_amdgcn_cvt_pk_fp8_f32(prob[2],prob[3],xb[0],true);
 xb[1]=__builtin_amdgcn_cvt_pk_fp8_f32(prob[4],prob[5],0,false);xb[1]=__builtin_amdgcn_cvt_pk_fp8_f32(prob[6],prob[7],xb[1],true);}
template<uint MAXT>
DEV void vit_attention_m32_body(const float*in,float*out,uint tokens){
 uint head=bid()%32,first=bid()/32*32;if(first>=tokens)return;
 const bool two=first+16<tokens;
 const unsigned char*in8=reinterpret_cast<const unsigned char*>(in);
 i2 q0[2]{},q1[2]{};for(uint k=0;k<32;k+=16){__builtin_memcpy(&q0[k/16],in8+(first+rc())*1024+head*32+k+gr()*8,8);if(two)__builtin_memcpy(&q1[k/16],in8+(first+16+rc())*1024+head*32+k+gr()*8,8);}
 h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;
 f8 sum0{},sum1{},acc0[2]{},acc1[2]{};
 for(uint key=0;key<tokens;key+=16){f8 a0{},a1{};
  for(uint k=0;k<32;k+=16){i2 y{};__builtin_memcpy(&y,in8+(tokens+key+rc())*1024+head*32+k+gr()*8,8);
   a0=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(y,q0[k/16],a0);a1=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(y,q1[k/16],a1);}
  h8 x0{},x1{};i2 xb0{},xb1{};vit_attn_m32_prob(a0,x0,xb0);vit_attn_m32_prob(a1,x1,xb1);
  sum0=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,x0,sum0);sum1=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,x1,sum1);
  for(uint c=0;c<2;c++){i2 y{};for(uint e=0;e<8;e++){uint vkey=key+gr()*8+e;uint b=in8[(2*tokens+vkey)*1024+head*32+c*16+rc()];y[e/4]=int(uint(y[e/4])|(b<<(8*(e%4))));}
   acc0[c]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(y,xb0,acc0[c]);acc1[c]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(y,xb1,acc1[c]);}
 }
 float inv0=vit_inv(sum0[0]);
 for(uint c=0;c<2;c++)for(uint e=0;e<8;e++)reinterpret_cast<unsigned char*>(out)[(first+rc())*1024+head*32+c*16+gr()*8+e]=byte_F(Hrtz(acc0[c][e]*inv0));
 if(two){float inv1=vit_inv(sum1[0]);
 for(uint c=0;c<2;c++)for(uint e=0;e<8;e++)reinterpret_cast<unsigned char*>(out)[(first+16+rc())*1024+head*32+c*16+gr()*8+e]=byte_F(Hrtz(acc1[c][e]*inv1));}
}
#endif
template<uint MAXT,bool ByteInput=false,bool ByteOut=false>
DEV void vit_attention_fused_body(const float*in,float*out,uint tokens){
 uint head=bid()%32,first=bid()/32*16;if(first>=tokens)return;
 __attribute__((shared)) unsigned short t16[2*16*24];
 const unsigned char*in8=reinterpret_cast<const unsigned char*>(in);
 i2 q[2]{};for(uint k=0;k<32;k+=16){if constexpr(ByteInput)__builtin_memcpy(&q[k/16],in8+(first+rc())*1024+head*32+k+gr()*8,8);else for(uint e=0;e<8;e++){uint j=k+gr()*8+e;pack(q[k/16],e,in[(first+rc())*1024+head*32+j]);}}
 h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;
 f8 sum{},acc[2]{};
 for(uint key=0,tile=0;key<tokens;key+=16,tile^=1u){f8 a{};
  for(uint k=0;k<32;k+=16){i2 y{};if constexpr(ByteInput)__builtin_memcpy(&y,in8+(tokens+key+rc())*1024+head*32+k+gr()*8,8);else for(uint e=0;e<8;e++){uint j=k+gr()*8+e;pack(y,e,in[(tokens+key+rc())*1024+head*32+j]);}a=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(q[k/16],y,a);}
  unsigned short*t=t16+tile*16*24;
  for(uint e=0;e<8;e++){float af=clampf(a[e]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);uint hb=(bits(af)>>13)-0x1c000u;t[(gr()*8+e)*24+rc()]=(unsigned short)(((hb<<4)+0x4000u)&65535u);}
  WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);
  h8 x{};i2 xb{};for(uint e=0;e<8;e++){unsigned short u=t[rc()*24+gr()*8+e];union{unsigned short u;_Float16 h;}cv;cv.u=u;x[e]=cv.h;pack(xb,e,from_half(u));}
  sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,sum);
  for(uint c=0;c<2;c++){i2 y{};for(uint e=0;e<8;e++){uint vkey=key+gr()*8+e;if constexpr(ByteInput){uint b=in8[(2*tokens+vkey)*1024+head*32+c*16+rc()];y[e/4]=int(uint(y[e/4])|(b<<(8*(e%4))));}else pack(y,e,in[(2*tokens+vkey)*1024+head*32+c*16+rc()]);}acc[c]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(xb,y,acc[c]);}
 }
 float inv[8];for(uint e=0;e<8;e++)inv[e]=vit_inv(sum[e]);
 WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);
 for(uint c=0;c<2;c++)for(uint e=0;e<8;e++){if constexpr(ByteOut)reinterpret_cast<unsigned char*>(out)[(first+gr()*8+e)*1024+head*32+c*16+rc()]=byte_F(Hrtz(acc[c][e]*inv[e]));else out[(first+gr()*8+e)*1024+head*32+c*16+rc()]=F(Hrtz(acc[c][e]*inv[e]));}
}
// Byte-stream twins: byte Q/K/V in, attention output stored as its E4M3 byte for vit_project_bytein_bout.
WAVE void vit_attention_fused_256_bytein_bout(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_fused_body<256,true,true>(in,out,tokens);}
WAVE void vit_attention_fused_400_bytein_bout(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
#if HIP_VIT_ATTN_M32
vit_attention_m32_body<400>(in,out,tokens);
#elif HIP_VIT_ATTN_TRANSPOSED_SCORE
vit_attention_transposed_score_body<400,true,true>(in,out,tokens);
#else
vit_attention_fused_body<400,true,true>(in,out,tokens);
#endif
}
WAVE void vit_attention_fused_640_bytein_bout(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
#if HIP_VIT_ATTN_M32
vit_attention_m32_body<640>(in,out,tokens);
#elif HIP_VIT_ATTN_TRANSPOSED_SCORE
vit_attention_transposed_score_body<640,true,true>(in,out,tokens);
#else
vit_attention_fused_body<640,true,true>(in,out,tokens);
#endif
}
// ViT projection on the byte stream: attention bytes as the A operand (dot8<ByteInput>), residual skip decoded from the
// contract bytes, F(H(total)) stored as f32 (block output / final gather) and, when out8 is given, as the E4M3 byte
// the next block's expand consumes directly (identical to what vit_pack_input would produce from the f32 value).
template<bool HalfSkip>
DEV void vit_project_bytein_bout_body(const float*in,const float*w,const float*skip,float*out,unsigned char*out8,uint tokens){
 uint first=bid()/64*16,row=bid()%64*16;if(first>=tokens)return;f8 total{};const unsigned char*s8=reinterpret_cast<const unsigned char*>(skip);const _Float16*s16=reinterpret_cast<const _Float16*>(skip);
 for(uint p=0;p<4;p++){f8 a{};if(!p)for(uint e=0;e<8;e++){uint i=(first+gr()*8+e)*1024+row+rc();float s;if constexpr(HalfSkip)s=(float)s16[i];else s=__builtin_amdgcn_cvt_f32_fp8(int(s8[i]),0);a[e]=H(s*w[1048576+row+rc()]);}a=dot8<true>(in,w,first,row,1024,p*256,(p+1)*256,a);for(uint e=0;e<8;e++)total[e]=p?total[e]+a[e]:a[e];}
 for(uint e=0;e<8;e++){float v=F(H(total[e]));uint i=(first+gr()*8+e)*1024+row+rc();out[i]=v;if(out8)out8[i]=static_cast<unsigned char>(__builtin_amdgcn_cvt_pk_fp8_f32(v,0.f,0,false));}
}
WAVE void vit_project_bytein_bout(const float*in,const float*w,const float*skip,float*out,unsigned char*out8,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_project_bytein_bout_body<false>(in,w,skip,out,out8,tokens);}
WAVE void vit_project_bytein_bout_hskip(const float*in,const float*w,const float*skip,float*out,unsigned char*out8,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_project_bytein_bout_body<true>(in,w,skip,out,out8,tokens);}
WAVE void vit_attention_fused_256(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_fused_body<256>(in,out,tokens);}
WAVE void vit_attention_fused_400(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_fused_body<400>(in,out,tokens);}
WAVE void vit_attention_fused_640(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_fused_body<640>(in,out,tokens);}
WAVE void vit_attention_fused_256_bytein(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_fused_body<256,true>(in,out,tokens);}
WAVE void vit_attention_fused_400_bytein(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_fused_body<400,true>(in,out,tokens);}
WAVE void vit_attention_fused_640_bytein(const float*in,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_attention_fused_body<640,true>(in,out,tokens);}
WAVE void vit_attention_av_fast(const float*in,const float*ex,const float*inv,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;uint first=bid()/64*16,head=(bid()%64)/2,col=(bid()%2)*16;if(first>=tokens)return;f8 a{};for(uint k=0;k<tokens;k+=16){i2 x{},y{};for(uint e=0;e<8;e++){uint key=k+gr()*8+e;pack(x,e,ex[((first+rc())*32+head)*tokens+key]);pack(y,e,in[(2*tokens+key)*1024+head*32+col+rc()]);}a=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(x,y,a);}for(uint e=0;e<8;e++){uint q=first+gr()*8+e;out[q*1024+head*32+col+rc()]=F(Hrtz(a[e]*inv[q*32+head]));}}
// Production decoder build leaves NATIVE_DECODER_HW_H=0: H is integer RNE.
WAVE void decoder_project2x(const float*in,const float*w,const float*skip,float*out,uint iw,uint ih,uint ow,uint oh,uint inputs,uint outputs){uint first=bid()/(outputs/16)*16,row=bid()%(outputs/16)*16;if(first>=iw*ih)return;uint parts=inputs==1024?4:1;f8 total{};for(uint p=0;p<parts;p++){f8 a{};for(uint k=p*(inputs/parts);k<(p+1)*(inputs/parts);k+=16){h8 x{},y{};for(uint e=0;e<8;e++){uint j=k+gr()*8+e;x[e]=first+rc()<iw*ih?(_Float16)in[(first+rc())*inputs+j]:(_Float16)0;y[e]=(_Float16)w[(row+rc())*inputs+j];}a=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,y,a);}for(uint e=0;e<8;e++)total[e]=p?total[e]+a[e]:a[e];}for(uint e=0;e<8;e++){uint token=first+gr()*8+e;if(token>=iw*ih)continue;float t=H(total[e]);for(uint dy=0;dy<2;dy++)for(uint dx=0;dx<2;dx++){uint x=(token%iw)*2+dx,y=(token/iw)*2+dy;if(x>=ow||y>=oh)continue;uint i=(y*ow+x)*outputs+row+rc();float merged=H(t+F(skip[i])*w[inputs*outputs+row+rc()]);out[i]=outputs==32?merged:F(merged);}}}
// Prepacked-half twin of decoder_project2x (host option decoder_h16w): the [outputs][inputs] matrix arrives as RNE halves
// (same values the (_Float16) cast produces), one 16-byte load per B fragment; the skip scale stays f32 at its offset.
// Inverse of the ViT token/channel gather (host Gather(...,true)): the ViT group output row t, channel c lands at raster
// row vg_raster(t), channel vg_ch(c). With Gin the decoder reads the ungathered tensor through vg_tinv/vg_cinv instead of a
// separate vit_gather launch: same f32 values in the same A slots, so identical bits.
DEV uint vg_tinv(uint r){return (r&~15u)|((r&7u)<<1)|((r>>3)&1u);}
DEV uint vg_cinv(uint k){return (k&~31u)|((k>>1)&1u)|((k&1u)<<1)|((k>>4)&1u)<<2|((k>>2)&1u)<<3|((k>>3)&1u)<<4;}
template<bool FullTile,bool ByteOutput=false,bool Gin=false>
DEV void decoder_project2x_h16w_body(const float*in,const float*w,const float*skip,float*out,uint iw,uint ih,uint ow,uint oh,uint inputs,uint outputs){uint first=bid()/(outputs/16)*16,row=bid()%(outputs/16)*16;if(first>=iw*ih)return;uint parts=inputs==1024?4:1;f8 total{};const _Float16*w16=reinterpret_cast<const _Float16*>(w);for(uint p=0;p<parts;p++){f8 a{};for(uint k=p*(inputs/parts);k<(p+1)*(inputs/parts);k+=16){h8 x{},y;for(uint e=0;e<8;e++){uint j=k+gr()*8+e;x[e]=(FullTile||first+rc()<iw*ih)?(_Float16)(Gin?in[vg_tinv(first+rc())*inputs+vg_cinv(j)]:in[(first+rc())*inputs+j]):(_Float16)0;}__builtin_memcpy(&y,w16+(row+rc())*inputs+k+gr()*8,16);a=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,y,a);}for(uint e=0;e<8;e++)total[e]=p?total[e]+a[e]:a[e];}
#if HIP_DEC_HOIST_SCALE == 2
 // results/fill-cu-20260930: all 32 skip reads (and the lane's scale) issued before the first out[] store -- the default
 // form re-read skip/scale behind each store (possible aliasing), 177 load->wait pairs in series. Same values/arithmetic.
 const float ss=w[inputs*outputs+row+rc()];float sk[8][4];
 for(uint e=0;e<8;e++){uint token=first+gr()*8+e;for(uint q=0;q<4;q++){uint dx=q&1,dy=q>>1,x=(token%iw)*2+dx,y=(token/iw)*2+dy;bool ok=(FullTile||token<iw*ih)&&x<ow&&y<oh;sk[e][q]=ok?skip[(y*ow+x)*outputs+row+rc()]:0.f;}}
 for(uint e=0;e<8;e++){uint token=first+gr()*8+e;if(!FullTile&&token>=iw*ih)continue;float t=H(total[e]);for(uint dy=0;dy<2;dy++)for(uint dx=0;dx<2;dx++){uint x=(token%iw)*2+dx,y=(token/iw)*2+dy;if(x>=ow||y>=oh)continue;uint i=(y*ow+x)*outputs+row+rc();float merged=H(t+F(sk[e][dy*2+dx])*ss);if constexpr(ByteOutput)reinterpret_cast<unsigned char*>(out)[i]=static_cast<unsigned char>(__builtin_amdgcn_cvt_pk_fp8_f32(HIP_BYTE_F_ADD0?fp8_add0(merged):F(merged),0.f,0,false));else out[i]=outputs==32?merged:F(merged);}}}
#elif HIP_DEC_HOIST_SCALE
 const float ss=w[inputs*outputs+row+rc()]; // skip scale once per lane (was re-read per output element behind the out[] stores)
 for(uint e=0;e<8;e++){uint token=first+gr()*8+e;if(!FullTile&&token>=iw*ih)continue;float t=H(total[e]);for(uint dy=0;dy<2;dy++)for(uint dx=0;dx<2;dx++){uint x=(token%iw)*2+dx,y=(token/iw)*2+dy;if(x>=ow||y>=oh)continue;uint i=(y*ow+x)*outputs+row+rc();float merged=H(t+F(skip[i])*ss);if constexpr(ByteOutput)reinterpret_cast<unsigned char*>(out)[i]=static_cast<unsigned char>(__builtin_amdgcn_cvt_pk_fp8_f32(HIP_BYTE_F_ADD0?fp8_add0(merged):F(merged),0.f,0,false));else out[i]=outputs==32?merged:F(merged);}}}
#else
 for(uint e=0;e<8;e++){uint token=first+gr()*8+e;if(!FullTile&&token>=iw*ih)continue;float t=H(total[e]);for(uint dy=0;dy<2;dy++)for(uint dx=0;dx<2;dx++){uint x=(token%iw)*2+dx,y=(token/iw)*2+dy;if(x>=ow||y>=oh)continue;uint i=(y*ow+x)*outputs+row+rc();float merged=H(t+F(skip[i])*w[inputs*outputs+row+rc()]);if constexpr(ByteOutput)reinterpret_cast<unsigned char*>(out)[i]=static_cast<unsigned char>(__builtin_amdgcn_cvt_pk_fp8_f32(HIP_BYTE_F_ADD0?fp8_add0(merged):F(merged),0.f,0,false));else out[i]=outputs==32?merged:F(merged);}}}
#endif
DEV void decoder_project2x_h16w_fixed_body(const float*in,const float*w,const float*skip,float*out,uint iw,uint ih,uint ow,uint oh,uint inputs,uint outputs){
 uint first=bid()/(outputs/16)*16,n=iw*ih;
 if(first<n&&n-first>=16)decoder_project2x_h16w_body<true>(in,w,skip,out,iw,ih,ow,oh,inputs,outputs);
 else decoder_project2x_h16w_body<false>(in,w,skip,out,iw,ih,ow,oh,inputs,outputs);
}
WAVE void decoder_project2x_h16w(const float*in,const float*w,const float*skip,float*out,uint iw,uint ih,uint ow,uint oh,uint inputs,uint outputs){if(inputs==1024&&outputs==512)decoder_project2x_h16w_fixed_body(in,w,skip,out,iw,ih,ow,oh,1024,512);else if(inputs==512&&outputs==256)decoder_project2x_h16w_fixed_body(in,w,skip,out,iw,ih,ow,oh,512,256);else if(inputs==256&&outputs==128)decoder_project2x_h16w_fixed_body(in,w,skip,out,iw,ih,ow,oh,256,128);else if(inputs==128&&outputs==64)decoder_project2x_h16w_fixed_body(in,w,skip,out,iw,ih,ow,oh,128,64);else if(inputs==64&&outputs==32)decoder_project2x_h16w_fixed_body(in,w,skip,out,iw,ih,ow,oh,64,32);else decoder_project2x_h16w_fixed_body(in,w,skip,out,iw,ih,ow,oh,inputs,outputs);}
#if HIP_VIT_GATHER_FOLD
// ViT group -> decoder39 (1024 -> 512 only): reads the ungathered ViT output (see vg_tinv/vg_cinv).
WAVE void decoder_project2x_h16w_gin(const float*in,const float*w,const float*skip,float*out,uint iw,uint ih,uint ow,uint oh,uint inputs,uint outputs){if(inputs!=1024||outputs!=512)return;uint first=bid()/32*16,n=iw*ih;
 if(first<n&&n-first>=16)decoder_project2x_h16w_body<true,false,true>(in,w,skip,out,iw,ih,ow,oh,1024,512);
 else decoder_project2x_h16w_body<false,false,true>(in,w,skip,out,iw,ih,ow,oh,1024,512);}
#endif

DEV void decoder_project2x_h16w_byteout_fixed_body(const float*in,const float*w,const float*skip,float*out,uint iw,uint ih,uint ow,uint oh,uint inputs,uint outputs){
 uint first=bid()/(outputs/16)*16,n=iw*ih;
 if(first<n&&n-first>=16)decoder_project2x_h16w_body<true,true>(in,w,skip,out,iw,ih,ow,oh,inputs,outputs);
 else decoder_project2x_h16w_body<false,true>(in,w,skip,out,iw,ih,ow,oh,inputs,outputs);
}
WAVE void decoder_project2x_h16w_byteout(const float*in,const float*w,const float*skip,float*out,uint iw,uint ih,uint ow,uint oh,uint inputs,uint outputs){if(inputs==1024&&outputs==512)decoder_project2x_h16w_byteout_fixed_body(in,w,skip,out,iw,ih,ow,oh,1024,512);else if(inputs==512&&outputs==256)decoder_project2x_h16w_byteout_fixed_body(in,w,skip,out,iw,ih,ow,oh,512,256);else if(inputs==256&&outputs==128)decoder_project2x_h16w_byteout_fixed_body(in,w,skip,out,iw,ih,ow,oh,256,128);else if(inputs==128&&outputs==64)decoder_project2x_h16w_byteout_fixed_body(in,w,skip,out,iw,ih,ow,oh,128,64);else if(inputs==64&&outputs==32)decoder_project2x_h16w_byteout_fixed_body(in,w,skip,out,iw,ih,ow,oh,64,32);else decoder_project2x_h16w_byteout_fixed_body(in,w,skip,out,iw,ih,ow,oh,inputs,outputs);}

template<bool ByteInput=false>
DEV f8 dot16(const float*in,const float*w,uint first,uint row,uint instride,uint kcols,uint inoff,uint woff,f8 a){for(uint k=0;k<kcols;k+=16){h8 x{},y{};for(uint e=0;e<8;e++){uint j=k+gr()*8+e;if constexpr(ByteInput)x[e]=(_Float16)__builtin_amdgcn_cvt_f32_fp8(int(reinterpret_cast<const unsigned char*>(in)[(first+rc())*instride+inoff+j]),0);else x[e]=(_Float16)in[(first+rc())*instride+inoff+j];y[e]=(_Float16)w[woff+(row+rc())*kcols+j];}a=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,y,a);}return a;}
WAVE void split_mix(const float*in,const float*w,float*out,uint tokens){uint first=bid()/32*16,row=bid()%32*16;if(first>=tokens)return;f8 a{};a=dot16(in,w,first,row,512,512,0,0,a);for(uint e=0;e<8;e++)out[(first+gr()*8+e)*512+row+rc()]=F(Hrtz(a[e]));}
WAVE void split_expand(const float*in,const float*w,float*out,uint tokens){uint first=bid()/128*16,col=bid()%128*16,g=col/256,row=col%256;if(first>=tokens)return;f8 a{};a=dot16(in,w,first,row,512,64,g*64,262144+g*16384,a);for(uint e=0;e<8;e++){float v=Hrtz(a[e]),gate=clampf(v,-4.f,4.f),poly=__builtin_fmaf(gate,__builtin_fmaf(absf(gate),-.055908203125f,.447265625f),.89453125f);out[(first+gr()*8+e)*2048+col+rc()]=F(v*poly);}}
WAVE void split_contract(const float*in,const float*w,float*out,uint tokens){uint first=bid()/32*16,col=bid()%32*16,g=col/64,row=col%64;if(first>=tokens)return;f8 a{};a=dot16(in,w,first,row,2048,256,g*256,393216+g*16384,a);for(uint e=0;e<8;e++)out[(first+gr()*8+e)*512+col+rc()]=F(Hrtz(a[e]));}
// stream0 FFWD projection build omits NATIVE_HW_H: initial and final H are integer RNE.
WAVE void split_projection(const float*in,const float*w,const float*skip,float*out,uint tokens){uint first=bid()/32*16,row=bid()%32*16;if(first>=tokens)return;f8 a{};for(uint e=0;e<8;e++)a[e]=H(skip[(first+gr()*8+e)*512+row+rc()]*w[262144+row+rc()]);a=dot8(in,w,first,row,512,0,512,a);for(uint e=0;e<8;e++)out[(first+gr()*8+e)*512+row+rc()]=F(H(a[e]));}

#if HIP_BYTE_F_ADD0
DEV unsigned char byte_F(float x){return static_cast<unsigned char>(__builtin_amdgcn_cvt_pk_fp8_f32(fp8_add0(x),0.f,0,false));}
#else
DEV unsigned char byte_F(float x){return static_cast<unsigned char>(__builtin_amdgcn_cvt_pk_fp8_f32(F(x),0.f,0,false));}
#endif

WAVE void vit_expand_fp8(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;uint first=bid()/(outputs/16)*16,row=bid()%(outputs/16)*16;if(first>=tokens)return;f8 a{};a=dot8(in,w,first,row,inputs,0,inputs,a);for(uint e=0;e<8;e++){float v=a[e],g=clampf(v,-4.f,4.f),poly=__builtin_fmaf(g,__builtin_fmaf(absf(g),-.055908203125f,.447265625f),.89453125f);reinterpret_cast<unsigned char*>(out)[(first+gr()*8+e)*outputs+row+rc()]=byte_F(v*poly);}}

WAVE void vit_project_fp8(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;uint first=bid()/(outputs/16)*16,row=bid()%(outputs/16)*16;if(first>=tokens)return;f8 total{};
 if(inputs==4096){for(uint e=0;e<8;e++)total[e]=H(skip[(first+gr()*8+e)*outputs+row+rc()]*w[inputs*outputs+row+rc()]);for(uint p=0;p<4;p++){f8 a{};a=dot8<true>(in,w,first,row,inputs,p*(inputs/4),(p+1)*(inputs/4),a);for(uint e=0;e<8;e++)total[e]+=a[e];}}
 else{for(uint p=0;p<4;p++){f8 a{};if(!p)for(uint e=0;e<8;e++)a[e]=H(skip[(first+gr()*8+e)*outputs+row+rc()]*w[inputs*outputs+row+rc()]);a=dot8<true>(in,w,first,row,inputs,p*(inputs/4),(p+1)*(inputs/4),a);for(uint e=0;e<8;e++)total[e]=p?total[e]+a[e]:a[e];}}
 for(uint e=0;e<8;e++)out[(first+gr()*8+e)*outputs+row+rc()]=F(H(total[e]));
}

WAVE void split_expand_fp8(const float*in,const float*w,float*out,uint tokens){uint first=bid()/128*16,col=bid()%128*16,g=col/256,row=col%256;if(first>=tokens)return;f8 a{};a=dot16(in,w,first,row,512,64,g*64,262144+g*16384,a);for(uint e=0;e<8;e++){float v=Hrtz(a[e]),gate=clampf(v,-4.f,4.f),poly=__builtin_fmaf(gate,__builtin_fmaf(absf(gate),-.055908203125f,.447265625f),.89453125f);reinterpret_cast<unsigned char*>(out)[(first+gr()*8+e)*2048+col+rc()]=byte_F(v*poly);}}

WAVE void split_contract_fp8(const float*in,const float*w,float*out,uint tokens){uint first=bid()/32*16,col=bid()%32*16,g=col/64,row=col%64;if(first>=tokens)return;f8 a{};a=dot16<true>(in,w,first,row,2048,256,g*256,393216+g*16384,a);for(uint e=0;e<8;e++)out[(first+gr()*8+e)*512+col+rc()]=F(Hrtz(a[e]));}

// Four independent K1024 partitions of the existing K4096 contract.
WAVE void vit_contract_parts_fp8(const float*in,const float*w,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint blocks=(tokens/16)*64,part=bid()/blocks,local=bid()%blocks,first=local/64*16,row=local%64*16;
 if(part>=4)return;f8 a{};a=dot8<true>(in,w,first,row,4096,part*1024,(part+1)*1024,a);
 for(uint e=0;e<8;e++)out[(part*tokens+first+gr()*8+e)*1024+row+rc()]=a[e];
}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256)))
void vit_contract_combine(const float*partial,const float*w,const float*skip,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint i=bid()*256+__builtin_amdgcn_workitem_id_x(),count=tokens*1024;if(i>=count)return;
 float total=H(skip[i]*w[4194304+i%1024]);
 for(uint part=0;part<4;part++)total+=partial[part*count+i];
 out[i]=F(H(total));
}

template<bool Tiled> DEV i2 vit_weight_fragment(const float*w,uint row,uint channels,uint k){
 i2 b{};
 if constexpr(Tiled){const unsigned char*p=reinterpret_cast<const unsigned char*>(w);uint off=((row/16)*(channels/32)+k/32)*512+(k%32+gr()*8)*16+row%16;for(uint e=0;e<8;e++)b[e/4]=int(uint(b[e/4])|(uint(p[off+16*e])<<(8*(e%4))));}
 else{const uint*p=reinterpret_cast<const uint*>(w);uint off=(row*channels+k+gr()*8)/4;b={int(p[off]),int(p[off+1])};}
 return b;
}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256)))
void vit_pack_input(const float*in,uint*out,uint count,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint word=bid()*256+__builtin_amdgcn_workitem_id_x(),i=word*4;if(i>=count)return;
 uint a=uint(__builtin_amdgcn_cvt_pk_fp8_f32(clampf(in[i],-448.f,448.f),clampf(in[i+1],-448.f,448.f),0,false));
 uint b=uint(__builtin_amdgcn_cvt_pk_fp8_f32(clampf(in[i+2],-448.f,448.f),clampf(in[i+3],-448.f,448.f),0,false));
 out[word]=(a&65535u)|((b&65535u)<<16);
}
// A-operand tile layout for the ViT expand input (host option vit_input_tiled): 16 tokens x 32 channels per 512-byte
// tile, inside a tile byte ((k%32)/8)*128+(row%16)*8+k%8, so one wave's A fragment (lanes rc=row, gr=k half) is a
// contiguous 256-byte load. Same bytes as vit_pack_input, only placed differently.
DEV uint vit_tile_offset(uint row,uint k,uint channels){return ((row/16)*(channels/32)+k/32)*512+((k%32)/8)*128+(row%16)*8+k%8;}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256)))
void vit_pack_input_tiled(const float*in,uint*out,uint count,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint word=bid()*256+__builtin_amdgcn_workitem_id_x(),i=word*4;if(i>=count)return;uint row=i/1024,k=i%1024;
 uint a=uint(__builtin_amdgcn_cvt_pk_fp8_f32(clampf(in[i],-448.f,448.f),clampf(in[i+1],-448.f,448.f),0,false));
 uint b=uint(__builtin_amdgcn_cvt_pk_fp8_f32(clampf(in[i+2],-448.f,448.f),clampf(in[i+3],-448.f,448.f),0,false));
 out[vit_tile_offset(row,k,1024)/4]=(a&65535u)|((b&65535u)<<16);
}
// One wave produces16x64, reusing A across four output-column fragments.
DEV i2 vit_weight_fragment_native(const float*w,uint row,uint channels,uint k){i2 b;__builtin_memcpy(&b,reinterpret_cast<const unsigned char*>(w)+((row/16)*(channels/32)+k/32)*512+(((k%32)/16*2+gr())*16+row%16)*8,8);return b;}
template<bool Tiled=false,bool ByteInput=false,bool Frag=false,bool TiledInput=false>
DEV void vit_expand_blocked_body(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs){
 uint tiles=outputs/64,first=bid()/tiles*16,col=bid()%tiles*64;if(first>=tokens)return;
 const uint*pw=reinterpret_cast<const uint*>(w);f8 accum[4]{};
 for(uint k=0;k<inputs;k+=16){i2 a{};
  if constexpr(TiledInput){__builtin_memcpy(&a,reinterpret_cast<const unsigned char*>(in)+vit_tile_offset(first+rc(),k+gr()*8,inputs),8);}
  else if constexpr(ByteInput){const uint*p=reinterpret_cast<const uint*>(in);uint off=((first+rc())*inputs+k+gr()*8)/4;a={int(p[off]),int(p[off+1])};}
  else DF_PACK8_LOOP(a,in[(first+rc())*inputs+k+gr()*8+e]);
  for(uint n=0;n<4;n++){uint row=col+n*16+rc();i2 b;if constexpr(Frag)b=vit_weight_fragment_native(w,row,inputs,k);else b=vit_weight_fragment<Tiled>(w,row,inputs,k);accum[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,accum[n]);}
 }
 #if VIT_EXPAND_TAIL_VEC
 // 16 tokens x 64 hidden bytes staged row-major in LDS (same byte_F per element), then lane = (token lane>>1, half lane&1)
 // stores 32 contiguous bytes (2 x b128 instead of 32 b8 per lane). Bytes and addresses unchanged.
 __attribute__((shared)) unsigned char t8[1024];
 for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){float v=accum[n][e],g=clampf(v,-4.f,4.f),poly=__builtin_fmaf(g,__builtin_fmaf(absf(g),-.055908203125f,.447265625f),.89453125f);t8[(gr()*8+e)*64+n*16+rc()]=byte_F(v*poly);}
 WG_FENCE(3);__builtin_amdgcn_wave_barrier();WG_FENCE(2);
 {uint l=__builtin_amdgcn_workitem_id_x()%32u,r=l>>1,h=l&1u;for(uint j=0;j<2;j++){int __attribute__((ext_vector_type(4))) q;__builtin_memcpy(&q,__builtin_assume_aligned(t8+r*64+h*32+j*16,16),16);__builtin_memcpy(__builtin_assume_aligned(reinterpret_cast<unsigned char*>(out)+size_t(first+r)*outputs+col+h*32+j*16,16),&q,16);}}
 #else
 for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){float v=accum[n][e],g=clampf(v,-4.f,4.f),poly=__builtin_fmaf(g,__builtin_fmaf(absf(g),-.055908203125f,.447265625f),.89453125f);reinterpret_cast<unsigned char*>(out)[(first+gr()*8+e)*outputs+col+n*16+rc()]=byte_F(v*poly);}
 #endif
}

// BM token tiles per wave (host option vit_expand_m4, HLSL BLOCK_M=4 shape): every weight fragment load serves BM
// token tiles. Each accumulator keeps its own K-ascending order with the same a/b operands as the single-tile body, so
// the bytes are identical; token tiles past the end are neither loaded nor stored (wave-uniform branch).
template<bool Tiled,bool ByteInput,uint BM,bool Frag=false>
DEV void vit_expand_blocked_body_m(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs){
 uint tiles=outputs/64,first=bid()/tiles*16*BM,col=bid()%tiles*64;if(first>=tokens)return;
 f8 accum[BM][4]{};
 for(uint k=0;k<inputs;k+=16){i2 a[BM];
  #pragma unroll
  for(uint m=0;m<BM;m++){a[m]={};uint row=first+m*16;if(row<tokens){if constexpr(ByteInput){const uint*p=reinterpret_cast<const uint*>(in);uint off=((row+rc())*inputs+k+gr()*8)/4;a[m]={int(p[off]),int(p[off+1])};}else DF_PACK8_LOOP(a[m],in[(row+rc())*inputs+k+gr()*8+e]);}}
  i2 b[4];
  #pragma unroll
  for(uint n=0;n<4;n++){if constexpr(Frag)b[n]=vit_weight_fragment_native(w,col+n*16+rc(),inputs,k);else b[n]=vit_weight_fragment<Tiled>(w,col+n*16+rc(),inputs,k);}
  #pragma unroll
  for(uint m=0;m<BM;m++){if(first+m*16<tokens){
   #pragma unroll
   for(uint n=0;n<4;n++)accum[m][n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a[m],b[n],accum[m][n]);}}
 }
 #pragma unroll
 for(uint m=0;m<BM;m++){uint row=first+m*16;if(row<tokens){
  #pragma unroll
  for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){float v=accum[m][n][e],g=clampf(v,-4.f,4.f),poly=__builtin_fmaf(g,__builtin_fmaf(absf(g),-.055908203125f,.447265625f),.89453125f);reinterpret_cast<unsigned char*>(out)[(row+gr()*8+e)*outputs+col+n*16+rc()]=byte_F(v*poly);}}}
}
WAVE void vit_expand_blocked_fp8_tiled_bytein_m4(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_expand_blocked_body_m<true,true,4>(in,w,out,tokens,inputs,outputs);}
WAVE void vit_expand_blocked_fp8_frag_bytein_m2(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_expand_blocked_body_m<true,true,2,true>(in,w,out,tokens,inputs,outputs);}
WAVE void vit_expand_blocked_fp8_frag_bytein_m4(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_expand_blocked_body_m<true,true,4,true>(in,w,out,tokens,inputs,outputs);}

// ByteStream: the block input arrives as the E4M3 bytes the previous vit_project_bytein_bout stored (or vit_pack_input
// produced), the residual skip is decoded from them, and the F(H(total)) result is stored as its byte (byte_F: exact on
// the F() lattice). Same MMA operands, accumulation order and rounding as the f32 body.
template<bool Partial,bool Tiled=false,bool ByteStream=false,bool HalfOut=false,bool Frag=false>
DEV void vit_contract_blocked_body(const float*in,const float*w,const float*skip,float*out,uint tokens){
#if VIT_CONTRACT_OCC_LDS
 __attribute__((shared)) volatile uint occ_pad[VIT_CONTRACT_OCC_LDS/4]; /* occupancy cap only (small-wins O); branch never taken for a real token count */
 if(tokens==0xffffffffu){uint lane=__builtin_amdgcn_workitem_id_x();uint i=(bid()*131u+lane)%(VIT_CONTRACT_OCC_LDS/4);occ_pad[i]=i;__builtin_amdgcn_s_barrier();if(occ_pad[(i+1)%(VIT_CONTRACT_OCC_LDS/4)]==5u)out[0]=0.f;}
#endif

 uint blocks=tokens,part0=Partial?bid()/blocks:0,local=Partial?bid()%blocks:bid(),first=local/16*16,col=local%16*64;
 if(first>=tokens||part0>=4)return;const uint*ip=reinterpret_cast<const uint*>(in),*wp=reinterpret_cast<const uint*>(w);f8 total[4]{};
 if constexpr(!Partial)for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint c=col+n*16+rc();float s;if constexpr(ByteStream)s=__builtin_amdgcn_cvt_f32_fp8(int(reinterpret_cast<const unsigned char*>(skip)[(first+gr()*8+e)*1024+c]),0);else s=skip[(first+gr()*8+e)*1024+c];total[n][e]=H(s*w[4194304+c]);}
 for(uint part=part0;part<(Partial?part0+1:4);part++){f8 acc[4]{};
  for(uint k=part*1024;k<(part+1)*1024;k+=16){uint off=((first+rc())*4096+k+gr()*8)/4;i2 a={int(ip[off]),int(ip[off+1])};
   for(uint n=0;n<4;n++){uint row=col+n*16+rc();i2 b;if constexpr(Frag)b=vit_weight_fragment_native(w,row,4096,k);else b=vit_weight_fragment<Tiled>(w,row,4096,k);acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,acc[n]);}
  }
  for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){if constexpr(Partial)out[(part*tokens+first+gr()*8+e)*1024+col+n*16+rc()]=acc[n][e];else total[n][e]+=acc[n][e];}
 }
 if constexpr(!Partial)for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint i=(first+gr()*8+e)*1024+col+n*16+rc();if constexpr(HalfOut)reinterpret_cast<_Float16*>(out)[i]=(_Float16)F(H(total[n][e]));else if constexpr(ByteStream)reinterpret_cast<unsigned char*>(out)[i]=byte_F(H(total[n][e]));else out[i]=F(H(total[n][e]));}
}
// Fragment-weight twin (host option vit_contract_frag): B from the FragmentPackedMatrix tiles (one contiguous 8-byte load) as the frag expand does.
WAVE void vit_contract_blocked_fp8_frag(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;if(inputs==4096&&outputs==1024)vit_contract_blocked_body<false,false,false,false,true>(in,w,skip,out,tokens);}
WAVE void vit_contract_blocked_fp8_bstream(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;if(inputs==4096&&outputs==1024)vit_contract_blocked_body<false,false,true>(in,w,skip,out,tokens);}
// Half-stream contract: byte skip in, F(H(total)) stored as F16 (exact on the E4M3 lattice) so the QKV projection loads
// its A fragments directly (vit_qkv_project_normalize_fused_f16compact_fp8_h16in*), the way the HLSL fused QKV does.
WAVE void vit_contract_blocked_fp8_hstream(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;if(inputs==4096&&outputs==1024)vit_contract_blocked_body<false,false,true,true>(in,w,skip,out,tokens);}
WAVE void vit_contract_blocked_fp8_tiled_hstream(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;if(inputs==4096&&outputs==1024)vit_contract_blocked_body<false,true,true,true>(in,w,skip,out,tokens);}
WAVE void vit_contract_blocked_fp8_tiled_bstream(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;if(inputs==4096&&outputs==1024)vit_contract_blocked_body<false,true,true>(in,w,skip,out,tokens);}
// Fused ViT FFN (host option vit_ffn_fused): 16 waves own 16 tokens; hidden is produced in four 1024-column quarters
// (each wave 64 hidden columns, full K=1024 expand chain, activation + byte_F as vit_expand_blocked_body) into LDS,
// and each quarter's contract partial (each wave 64 output columns, K=1024 chain in ascending order) is added to the
// running total exactly as vit_contract_blocked_body adds its four parts (total starts at H(skip*scale)). Two launches
// become one and the 4096-byte hidden rows never leave the CU. Expand weights fragment-native, contract weights row-major.
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512)))
void vit_ffn_fused(const float*in,const float*we,const float*wc,const float*skip,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint tid=__builtin_amdgcn_workitem_id_x(),wave=tid/32,first=bid()*16;if(first>=tokens)return;
 __attribute__((shared)) unsigned char hid[16*1040];
 const uint*ip=reinterpret_cast<const uint*>(in),*cp=reinterpret_cast<const uint*>(wc);
 f8 total[4];for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint c=wave*64+n*16+rc();total[n][e]=H(skip[(first+gr()*8+e)*1024+c]*wc[4194304+c]);}
 for(uint q=0;q<4;q++){
  f8 accum[4]{};
  for(uint k=0;k<1024;k+=16){uint off=((first+rc())*1024+k+gr()*8)/4;i2 a={int(ip[off]),int(ip[off+1])};
   for(uint n=0;n<4;n++){uint row=q*1024+wave*64+n*16+rc();i2 b=vit_weight_fragment_native(we,row,1024,k);accum[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,accum[n]);}
  }
  if(q)__builtin_amdgcn_s_barrier();
  for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){float v=accum[n][e],g=clampf(v,-4.f,4.f),poly=__builtin_fmaf(g,__builtin_fmaf(absf(g),-.055908203125f,.447265625f),.89453125f);hid[(gr()*8+e)*1040+wave*64+n*16+rc()]=byte_F(v*poly);}
  __builtin_amdgcn_s_barrier();
  f8 acc[4]{};
  for(uint k=0;k<1024;k+=16){i2 a;__builtin_memcpy(&a,hid+rc()*1040+k+gr()*8,8);
   for(uint n=0;n<4;n++){uint row=wave*64+n*16+rc(),wo=(row*4096+q*1024+k+gr()*8)/4;i2 b={int(cp[wo]),int(cp[wo+1])};acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,acc[n]);}
  }
  for(uint n=0;n<4;n++)for(uint e=0;e<8;e++)total[n][e]+=acc[n][e];
 }
 for(uint n=0;n<4;n++)for(uint e=0;e<8;e++)out[(first+gr()*8+e)*1024+wave*64+n*16+rc()]=F(H(total[n][e]));
}
WAVE void vit_contract_blocked_fp8(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;if(inputs==4096&&outputs==1024)vit_contract_blocked_body<false>(in,w,skip,out,tokens);}
WAVE void vit_contract_parts_blocked_fp8(const float*in,const float*w,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_contract_blocked_body<true>(in,w,nullptr,out,tokens);}

WAVE void vit_expand_blocked_fp8(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_expand_blocked_body<false,false>(in,w,out,tokens,inputs,outputs);}

WAVE void vit_expand_blocked_fp8_tiled(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_expand_blocked_body<true,false>(in,w,out,tokens,inputs,outputs);}

WAVE void vit_expand_blocked_fp8_bytein(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_expand_blocked_body<false,true>(in,w,out,tokens,inputs,outputs);}

WAVE void vit_expand_blocked_fp8_tiled_bytein(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_expand_blocked_body<true,true>(in,w,out,tokens,inputs,outputs);}
// Fragment-native weight layout (FragmentPackedMatrix): one 8-byte load per B fragment instead of eight byte gathers.
// Fixed model dimensions expose the K-loop schedule; retain the generic ABI fallback.
WAVE void vit_expand_blocked_fp8_frag_bytein(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;if(inputs==1024&&outputs==4096)vit_expand_blocked_body<true,true,true>(in,w,out,tokens,1024,4096);else vit_expand_blocked_body<true,true,true>(in,w,out,tokens,inputs,outputs);}
// Tile-layout input (vit_pack_input_tiled) + fragment-native weights: both operands are contiguous 8-byte lane loads.
WAVE void vit_expand_blocked_fp8_frag_tiledin(const float*in,const float*w,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_expand_blocked_body<true,true,true,true>(in,w,out,tokens,inputs,outputs);}

WAVE void vit_contract_blocked_fp8_tiled(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;if(inputs==4096&&outputs==1024)vit_contract_blocked_body<false,true>(in,w,skip,out,tokens);}
WAVE void vit_contract_parts_blocked_fp8_tiled(const float*in,const float*w,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_contract_blocked_body<true,true>(in,w,nullptr,out,tokens);}

KERNEL __attribute__((amdgpu_flat_work_group_size(128,128)))
void split_ffn_fused(const float*in,const float*w,float*out,uint tokens){
 uint block=bid(),first=(block/8)*16,g=block%8,tid=__builtin_amdgcn_workitem_id_x(),wave=tid/32;
 if(first>=tokens)return;
 __attribute__((shared)) unsigned char hidden[16*260];
 f8 expanded[4]{};
 for(uint j=0;j<4;j++)expanded[j]=dot16(in,w,first,wave*64+j*16,512,64,g*64,262144+g*16384,expanded[j]);
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++){float v=Hrtz(expanded[j][e]),gate=clampf(v,-4.f,4.f),poly=__builtin_fmaf(gate,__builtin_fmaf(absf(gate),-.055908203125f,.447265625f),.89453125f);hidden[(gr()*8+e)*260+wave*64+j*16+rc()]=byte_F(v*poly);}
 __builtin_amdgcn_s_barrier();
 f8 result{};
 for(uint k=0;k<256;k+=16){h8 a{},b{};for(uint e=0;e<8;e++){uint kk=k+gr()*8+e;a[e]=(_Float16)__builtin_amdgcn_cvt_f32_fp8(int(hidden[rc()*260+kk]),0);b[e]=(_Float16)w[393216+g*16384+(wave*16+rc())*256+kk];}result=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,result);}
 for(uint e=0;e<8;e++)out[(first+gr()*8+e)*512+g*64+wave*16+rc()]=F(Hrtz(result[e]));
}

WAVE void split_mix_blocked(const float*in,const float*w,float*out,uint tokens){
 uint first=bid()/8*16,row=bid()%8*64;if(first>=tokens)return;f8 acc[4]{};
 for(uint k=0;k<512;k+=16){h8 a{};for(uint e=0;e<8;e++)a[e]=(_Float16)in[(first+rc())*512+k+gr()*8+e];for(uint j=0;j<4;j++){h8 b{};for(uint e=0;e<8;e++)b[e]=(_Float16)w[(row+j*16+rc())*512+k+gr()*8+e];acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,acc[j]);}}
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++)out[(first+gr()*8+e)*512+row+j*16+rc()]=F(Hrtz(acc[j][e]));
}

// split_mix_blocked with the mix weights prepacked as RNE half (PackedSplitFfnWeightMixHalf): one 16-byte load per B
// fragment instead of eight f32 loads and casts; identical operands, so identical bits.
// split_ffn_fused_fp8 with a second E4M3 tiled store of F(Hrtz(result)) for split_projection_frag (host option c512_proj_tiles).
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128)))
void split_ffn_fused_fp8_t8(const float*in,const float*w,float*out,unsigned char*out8,uint tokens){
 uint block=bid(),first=(block/8)*16,g=block%8,tid=__builtin_amdgcn_workitem_id_x(),wave=tid/32;
 if(first>=tokens)return;
 __attribute__((shared)) unsigned char hidden[16*260];
 f8 expanded[4]{};
 for(uint j=0;j<4;j++)for(uint k=0;k<64;k+=16){h8 a{},b{};for(uint e=0;e<8;e++)a[e]=(_Float16)in[(first+rc())*512+g*64+k+gr()*8+e];__builtin_memcpy(&b,reinterpret_cast<const _Float16*>(w+262144)+g*16384+(wave*64+j*16+rc())*64+k+gr()*8,16);expanded[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,expanded[j]);}
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++){float v=Hrtz(expanded[j][e]),gate=clampf(v,-4.f,4.f),poly=__builtin_fmaf(gate,__builtin_fmaf(absf(gate),-.055908203125f,.447265625f),.89453125f);hidden[(gr()*8+e)*260+wave*64+j*16+rc()]=byte_F(v*poly);}
 __builtin_amdgcn_s_barrier();
 f8 result{};
 const uint*pw=reinterpret_cast<const uint*>(w);
 for(uint k=0;k<256;k+=16){i2 a{};__builtin_memcpy(&a,hidden+rc()*260+k+gr()*8,8);uint off=393216+g*4096+((wave*16+rc())*256+k+gr()*8)/4;i2 b={int(pw[off]),int(pw[off+1])};result=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,result);}
#if C512_T8_TAIL_VEC
 // The workgroup's 16 tokens x 64 channels of out8 are two consecutive 512-byte tiles: stage them in LDS in the same
 // layout, then thread t stores bytes 8t..8t+7 (one b64 instead of 8 b8 per lane). Same bytes.
 __attribute__((shared)) unsigned char t8[1024];
 for(uint e=0;e<8;e++){uint r=first+gr()*8+e,c=g*64+wave*16+rc();float v=F(c512_hq(result[e]));if(!C512_T8_NO_F32)out[r*512+c]=v;uint cl=wave*16+rc();t8[(cl/32)*512+(r%16)*32+cl%32]=static_cast<unsigned char>(__builtin_amdgcn_cvt_pk_fp8_f32(v,0.f,0,false));}
 WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);
 {i2 q;__builtin_memcpy(&q,__builtin_assume_aligned(t8+tid*8,8),8);__builtin_memcpy(__builtin_assume_aligned(out8+size_t(first+g*2)*512+tid*8,8),&q,8);}
#else
 for(uint e=0;e<8;e++){uint r=first+gr()*8+e,c=g*64+wave*16+rc();float v=F(c512_hq(result[e]));if(!C512_T8_NO_F32)out[r*512+c]=v;out8[((r/16)*16+c/32)*512+(r%16)*32+c%32]=static_cast<unsigned char>(__builtin_amdgcn_cvt_pk_fp8_f32(v,0.f,0,false));}
#endif
}
// split_projection_blocked in the QKV-frag shape: A from the contract's E4M3 tiles, B from fragment-tiled E4M3 projection
// weights (PackedSplitProjectionFrag), no LDS; residual H(skip*scale) and F(H(acc)) as split_projection_blocked. Also
// stores the tiled E4M3 copy for mh_qkv_normalize_frag_c512 (replaces split_projection_blocked_t8).
WAVE void split_projection_frag(const unsigned char*in8,const float*w,const float*skip,float*out,unsigned char*out8,uint tokens){
 uint first=bid()/8*16,row=bid()%8*64;if(first>=tokens)return;f8 acc[4]{};
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++)acc[j][e]=H(skip[(first+gr()*8+e)*512+row+j*16+rc()]*w[262144+row+j*16+rc()]);
 const unsigned char*wb=reinterpret_cast<const unsigned char*>(w);const unsigned char*arow=in8+((first/16)*16)*512+rc()*32+gr()*8;
 for(uint gs=0;gs<16;gs++)for(uint h=0;h<2;h++){i2 a;__builtin_memcpy(&a,arow+gs*512+h*16,8);
  for(uint j=0;j<4;j++){uint n0=row+j*16+rc();i2 b;__builtin_memcpy(&b,wb+((n0/16)*16+gs)*512+((h*2+gr())*16+n0%16)*8,8);acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,acc[j]);}}
#if C512_T8_TAIL_VEC
 // 16 tokens x 64 channels = two consecutive 512-byte out8 tiles; staged in LDS, lane stores 2 x 16 contiguous bytes.
 __attribute__((shared)) unsigned char t8[1024];
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++){uint r=first+gr()*8+e,c=row+j*16+rc();float v=F(H(acc[j][e]));out[r*512+c]=v;uint cl=j*16+rc();t8[(cl/32)*512+(r%16)*32+cl%32]=static_cast<unsigned char>(__builtin_amdgcn_cvt_pk_fp8_f32(v,0.f,0,false));}
 WG_FENCE(3);__builtin_amdgcn_wave_barrier();WG_FENCE(2);
 {uint l=__builtin_amdgcn_workitem_id_x();for(uint h=0;h<2;h++){int __attribute__((ext_vector_type(4))) q;__builtin_memcpy(&q,__builtin_assume_aligned(t8+h*512+l*16,16),16);__builtin_memcpy(__builtin_assume_aligned(out8+size_t(first+row/32)*512+h*512+l*16,16),&q,16);}}
#else
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++){uint r=first+gr()*8+e,c=row+j*16+rc();float v=F(H(acc[j][e]));out[r*512+c]=v;out8[((r/16)*16+c/32)*512+(r%16)*32+c%32]=static_cast<unsigned char>(__builtin_amdgcn_cvt_pk_fp8_f32(v,0.f,0,false));}
#endif
}
WAVE void split_mix_blocked_h16w(const float*in,const float*w,float*out,uint tokens){
 uint first=bid()/8*16,row=bid()%8*64;if(first>=tokens)return;f8 acc[4]{};const _Float16*w16=reinterpret_cast<const _Float16*>(w);
 for(uint k=0;k<512;k+=16){h8 a{};for(uint e=0;e<8;e++)a[e]=(_Float16)in[(first+rc())*512+k+gr()*8+e];for(uint j=0;j<4;j++){h8 b;__builtin_memcpy(&b,w16+(row+j*16+rc())*512+k+gr()*8,16);acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,acc[j]);}}
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++)out[(first+gr()*8+e)*512+row+j*16+rc()]=F(Hrtz(acc[j][e]));
}
WAVE void split_projection_blocked(const float*in,const float*w,const float*skip,float*out,uint tokens){
 uint first=bid()/8*16,row=bid()%8*64;if(first>=tokens)return;f8 acc[4]{};
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++)acc[j][e]=H(skip[(first+gr()*8+e)*512+row+j*16+rc()]*w[262144+row+j*16+rc()]);
 for(uint k=0;k<512;k+=16){i2 a{};DF_PACK8_LOOP(a,in[(first+rc())*512+k+gr()*8+e]);for(uint j=0;j<4;j++){i2 b{};
#ifdef HIP_PREPACKED_WEIGHTS
  const uint*pw=reinterpret_cast<const uint*>(w);uint off=((row+j*16+rc())*512+k+gr()*8)/4;b={int(pw[off]),int(pw[off+1])};
#else
  DF_PACK8_LOOP(b,w[(row+j*16+rc())*512+k+gr()*8+e]);
#endif
  acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,acc[j]);}}
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++)out[(first+gr()*8+e)*512+row+j*16+rc()]=F(H(acc[j][e]));
}

// split_projection_blocked with a second, E4M3 tiled store (host option c512_qkv_frag): the F(H(acc)) values are also
// written as 16-token x 32-channel row-major 512-byte tiles (HLSL stream8 layout) for mh_qkv_normalize_frag_c512.
WAVE void split_projection_blocked_t8(const float*in,const float*w,const float*skip,float*out,unsigned char*out8,uint tokens){
 uint first=bid()/8*16,row=bid()%8*64;if(first>=tokens)return;f8 acc[4]{};
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++)acc[j][e]=H(skip[(first+gr()*8+e)*512+row+j*16+rc()]*w[262144+row+j*16+rc()]);
 for(uint k=0;k<512;k+=16){i2 a{};DF_PACK8_LOOP(a,in[(first+rc())*512+k+gr()*8+e]);for(uint j=0;j<4;j++){i2 b{};
#ifdef HIP_PREPACKED_WEIGHTS
  const uint*pw=reinterpret_cast<const uint*>(w);uint off=((row+j*16+rc())*512+k+gr()*8)/4;b={int(pw[off]),int(pw[off+1])};
#else
  DF_PACK8_LOOP(b,w[(row+j*16+rc())*512+k+gr()*8+e]);
#endif
  acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,acc[j]);}}
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++){uint r=first+gr()*8+e,c=row+j*16+rc();float v=F(H(acc[j][e]));out[r*512+c]=v;out8[((r/16)*16+c/32)*512+(r%16)*32+c%32]=static_cast<unsigned char>(__builtin_amdgcn_cvt_pk_fp8_f32(v,0.f,0,false));}
}
WAVE void vit_qkv_project_blocked(const float*in,const float*w,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint perpart=(tokens/16)*32,part=bid()/perpart,local=bid()%perpart,first=local/32*16,row=local%32*32;if(part>=3)return;f8 acc[2]{};
 for(uint k=0;k<1024;k+=16){h8 a{};for(uint e=0;e<8;e++)a[e]=(_Float16)in[(first+rc())*1024+k+gr()*8+e];for(uint j=0;j<2;j++){h8 b{};for(uint e=0;e<8;e++)b[e]=(_Float16)w[part*1048576+(row+j*16+rc())*1024+k+gr()*8+e];acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,acc[j]);}}
 for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=acc[j][e];
}

WAVE void vit_qkv_project_normalize_fused(const float*in,const float*w,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint perpart=(tokens/16)*32,part=bid()/perpart,local=bid()%perpart,first=local/32*16,row=local%32*32;if(part>=3)return;f8 acc[2]{};
 for(uint k=0;k<1024;k+=16){h8 a{};for(uint e=0;e<8;e++)a[e]=(_Float16)in[(first+rc())*1024+k+gr()*8+e];for(uint j=0;j<2;j++){h8 b{};for(uint e=0;e<8;e++)b[e]=(_Float16)w[part*1048576+(row+j*16+rc())*1024+k+gr()*8+e];acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,acc[j]);}}
 __attribute__((shared)) float raw[16*33];
 f8 sum{};
 if(part<2){
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)raw[(gr()*8+e)*33+j*16+rc()]=acc[j][e];
  __builtin_amdgcn_s_barrier();
  for(uint k=0;k<32;k+=16){h8 x{},ones{};for(uint e=0;e<8;e++){float v=raw[rc()*33+k+gr()*8+e];x[e]=(_Float16)(v*v);ones[e]=(_Float16)1.f;}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,sum);}
 }
 for(uint j=0;j<2;j++)for(uint e=0;e<8;e++){float v=acc[j][e];if(part<2){float scale=part==0?5.65625f*w[3145728+row/32]:1.f;v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*scale;}out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=F(v);}
}

KERNEL __attribute__((amdgpu_flat_work_group_size(128,128)))
// Keep expansion F16: the FP8 expansion probe differs on block46 wide-range input.
void split_ffn_fused_fp8(const float*in,const float*w,float*out,uint tokens){
 uint block=bid(),first=(block/8)*16,g=block%8,tid=__builtin_amdgcn_workitem_id_x(),wave=tid/32;
 if(first>=tokens)return;
 __attribute__((shared)) unsigned char hidden[16*260];
 f8 expanded[4]{};
 for(uint j=0;j<4;j++)for(uint k=0;k<64;k+=16){h8 a{},b{};for(uint e=0;e<8;e++)a[e]=(_Float16)in[(first+rc())*512+g*64+k+gr()*8+e];__builtin_memcpy(&b,reinterpret_cast<const _Float16*>(w+262144)+g*16384+(wave*64+j*16+rc())*64+k+gr()*8,16);expanded[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,expanded[j]);}
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++){float v=Hrtz(expanded[j][e]),gate=clampf(v,-4.f,4.f),poly=__builtin_fmaf(gate,__builtin_fmaf(absf(gate),-.055908203125f,.447265625f),.89453125f);hidden[(gr()*8+e)*260+wave*64+j*16+rc()]=byte_F(v*poly);}
 __builtin_amdgcn_s_barrier();
 f8 result{};
 const uint*pw=reinterpret_cast<const uint*>(w);
 for(uint k=0;k<256;k+=16){i2 a{};__builtin_memcpy(&a,hidden+rc()*260+k+gr()*8,8);uint off=393216+g*4096+((wave*16+rc())*256+k+gr()*8)/4;i2 b={int(pw[off]),int(pw[off+1])};result=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,result);}
 for(uint e=0;e<8;e++)out[(first+gr()*8+e)*512+g*64+wave*16+rc()]=F(Hrtz(result[e]));
}

// Mix fused into the C512 FFN (host option split_mix_fused, HLSL ffwd_parallel shape): the four waves of a group each
// compute 16 of the group's 64 mixed columns over K=512 (same F16 MMA chain and F(Hrtz()) as split_mix_blocked), park
// them as exact halves in LDS, and the expand reads its A fragments from there. One launch instead of two; the mixed
// tensor never leaves the CU. Weight buffer = PackedSplitFfnWeight (mix f32 at 0, expand F16 at 262144, contract E4M3 at 393216).
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128)))
void split_ffn_fused_fp8_mix(const float*in,const float*w,float*out,uint tokens){
 uint block=bid(),first=(block/8)*16,g=block%8,tid=__builtin_amdgcn_workitem_id_x(),wave=tid/32;
 if(first>=tokens)return;
 __attribute__((shared)) unsigned char hidden[16*260];
 __attribute__((shared)) _Float16 mixed16[16*72];
 {f8 acc{};uint row=g*64+wave*16;
  for(uint k=0;k<512;k+=16){h8 a{},b{};for(uint e=0;e<8;e++){a[e]=(_Float16)in[(first+rc())*512+k+gr()*8+e];b[e]=(_Float16)w[(row+rc())*512+k+gr()*8+e];}acc=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,acc);}
  for(uint e=0;e<8;e++)mixed16[(gr()*8+e)*72+wave*16+rc()]=(_Float16)F(Hrtz(acc[e]));
 }
 __builtin_amdgcn_s_barrier();
 f8 expanded[4]{};
 for(uint j=0;j<4;j++)for(uint k=0;k<64;k+=16){h8 a{},b{};for(uint e=0;e<8;e++)a[e]=mixed16[rc()*72+k+gr()*8+e];__builtin_memcpy(&b,reinterpret_cast<const _Float16*>(w+262144)+g*16384+(wave*64+j*16+rc())*64+k+gr()*8,16);expanded[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,expanded[j]);}
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++){float v=Hrtz(expanded[j][e]),gate=clampf(v,-4.f,4.f),poly=__builtin_fmaf(gate,__builtin_fmaf(absf(gate),-.055908203125f,.447265625f),.89453125f);hidden[(gr()*8+e)*260+wave*64+j*16+rc()]=byte_F(v*poly);}
 __builtin_amdgcn_s_barrier();
 f8 result{};
 const uint*pw=reinterpret_cast<const uint*>(w);
 for(uint k=0;k<256;k+=16){i2 a{};__builtin_memcpy(&a,hidden+rc()*260+k+gr()*8,8);uint off=393216+g*4096+((wave*16+rc())*256+k+gr()*8)/4;i2 b={int(pw[off]),int(pw[off+1])};result=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,result);}
 for(uint e=0;e<8;e++)out[(first+gr()*8+e)*512+g*64+wave*16+rc()]=F(Hrtz(result[e]));
}

WAVE void vit_qkv_project_normalize_fused_f16weight(const float*in,const float*w,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint perpart=(tokens/16)*32,part=bid()/perpart,local=bid()%perpart,first=local/32*16,row=local%32*32;if(part>=3)return;f8 acc[2]{};
 for(uint k=0;k<1024;k+=16){h8 a{};for(uint e=0;e<8;e++)a[e]=(_Float16)in[(first+rc())*1024+k+gr()*8+e];for(uint j=0;j<2;j++){h8 b{};__builtin_memcpy(&b,reinterpret_cast<const _Float16*>(w)+part*1048576+(row+j*16+rc())*1024+k+gr()*8,16);acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,acc[j]);}}
 __attribute__((shared)) float raw[16*33];
 f8 sum{};
 if(part<2){
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)raw[(gr()*8+e)*33+j*16+rc()]=acc[j][e];
  __builtin_amdgcn_s_barrier();
  for(uint k=0;k<32;k+=16){h8 x{},ones{};for(uint e=0;e<8;e++){float v=raw[rc()*33+k+gr()*8+e];x[e]=(_Float16)(v*v);ones[e]=(_Float16)1.f;}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,sum);}
 }
 for(uint j=0;j<2;j++)for(uint e=0;e<8;e++){float v=acc[j][e];if(part<2){float scale=part==0?5.65625f*w[3145728+row/32]:1.f;v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*scale;}out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=F(v);}
}

// Byte-output twin of vit_qkv_project_normalize_fused_f16compact: identical projection/normalize, the F() result stored
// as its E4M3 byte (byte_F: exact on the F() lattice, same byte pack() would produce from the f32 value).
WAVE void vit_qkv_project_normalize_fused_f16compact_fp8(const float*in,const float*w,unsigned char*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint perpart=(tokens/16)*32,part=bid()/perpart,local=bid()%perpart,first=local/32*16,row=local%32*32;if(part>=3)return;f8 acc[2]{};
 for(uint k=0;k<1024;k+=16){h8 a{};for(uint e=0;e<8;e++)a[e]=(_Float16)in[(first+rc())*1024+k+gr()*8+e];for(uint j=0;j<2;j++){h8 b{};__builtin_memcpy(&b,reinterpret_cast<const _Float16*>(w)+part*1048576+(row+j*16+rc())*1024+k+gr()*8,16);acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,acc[j]);}}
 __attribute__((shared)) float raw[16*33];
 f8 sum{};
 if(part<2){
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)raw[(gr()*8+e)*33+j*16+rc()]=acc[j][e];
  __builtin_amdgcn_s_barrier();
  for(uint k=0;k<32;k+=16){h8 x{},ones{};for(uint e=0;e<8;e++){float v=raw[rc()*33+k+gr()*8+e];x[e]=(_Float16)(v*v);ones[e]=(_Float16)1.f;}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,sum);}
 }
 for(uint j=0;j<2;j++)for(uint e=0;e<8;e++){float v=acc[j][e];if(part<2){float scale=part==0?5.65625f*w[1572864+row/32]:1.f;v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*scale;}out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=byte_F(v);}
}
// Fragment-weight twins (host options vit_proj_frag / vit_qkv_frag): identical operands and accumulation order to
// vit_project / vit_qkv_project_normalize_fused_f16compact_fp8, but every B fragment is one contiguous load from the
// prepacked tile layout (FragmentPackedMatrix for the E4M3 projection, 16x16 f16 tiles for the QKV halves) instead of
// 16 lanes touching 16 different weight rows.
DEV f8 dot8_frag(const float*in,const unsigned char*wf,uint first,uint row,uint channels,uint begin,uint end,f8 a){
 for(uint k=begin;k<end;k+=16){i2 x{},y{};
  DF_PACK8_LOOP(x,in[(first+rc())*channels+k+gr()*8+e]);
  __builtin_memcpy(&y,wf+((row/16)*(channels/32)+k/32)*512+((((k%32)/16)*2+gr())*16+rc())*8,8);
  a=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(x,y,a);
 }return a;
}
DEV void vit_project_frag_fixed_body(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;uint first=bid()/(outputs/16)*16,row=bid()%(outputs/16)*16;if(first>=tokens)return;f8 total{};
 const unsigned char*wf=reinterpret_cast<const unsigned char*>(w);
#if HIP_VIT_HOIST_SCALE
 const float rs=w[inputs*outputs+row+rc()]; // residual scale once per lane (was re-read per element: 7 identical loads in the ISA)
 if(inputs==4096){for(uint e=0;e<8;e++)total[e]=H(skip[(first+gr()*8+e)*outputs+row+rc()]*rs);for(uint p=0;p<4;p++){f8 a{};a=dot8_frag(in,wf,first,row,inputs,p*(inputs/4),(p+1)*(inputs/4),a);for(uint e=0;e<8;e++)total[e]+=a[e];}}
 else{for(uint p=0;p<4;p++){f8 a{};if(!p)for(uint e=0;e<8;e++)a[e]=H(skip[(first+gr()*8+e)*outputs+row+rc()]*rs);a=dot8_frag(in,wf,first,row,inputs,p*(inputs/4),(p+1)*(inputs/4),a);for(uint e=0;e<8;e++)total[e]=p?total[e]+a[e]:a[e];}}
#else
 if(inputs==4096){for(uint e=0;e<8;e++)total[e]=H(skip[(first+gr()*8+e)*outputs+row+rc()]*w[inputs*outputs+row+rc()]);for(uint p=0;p<4;p++){f8 a{};a=dot8_frag(in,wf,first,row,inputs,p*(inputs/4),(p+1)*(inputs/4),a);for(uint e=0;e<8;e++)total[e]+=a[e];}}
 else{for(uint p=0;p<4;p++){f8 a{};if(!p)for(uint e=0;e<8;e++)a[e]=H(skip[(first+gr()*8+e)*outputs+row+rc()]*w[inputs*outputs+row+rc()]);a=dot8_frag(in,wf,first,row,inputs,p*(inputs/4),(p+1)*(inputs/4),a);for(uint e=0;e<8;e++)total[e]=p?total[e]+a[e]:a[e];}}
#endif
 for(uint e=0;e<8;e++)out[(first+gr()*8+e)*outputs+row+rc()]=F(H(total[e]));
}
WAVE void vit_project_frag(const float*in,const float*w,const float*skip,float*out,uint tokens,uint inputs,uint outputs,const uint*reuse_gate){if(inputs==1024&&outputs==1024)vit_project_frag_fixed_body(in,w,skip,out,tokens,1024,1024,reuse_gate);else vit_project_frag_fixed_body(in,w,skip,out,tokens,inputs,outputs,reuse_gate);}
WAVE void vit_qkv_project_normalize_fused_f16compact_fp8_frag(const float*in,const float*w,unsigned char*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint perpart=(tokens/16)*32,part=bid()/perpart,local=bid()%perpart,first=local/32*16,row=local%32*32;if(part>=3)return;f8 acc[2]{};
 const _Float16*w16=reinterpret_cast<const _Float16*>(w)+part*1048576;
 for(uint k=0;k<1024;k+=16){h8 a{};for(uint e=0;e<8;e++)a[e]=(_Float16)in[(first+rc())*1024+k+gr()*8+e];for(uint j=0;j<2;j++){h8 b{};__builtin_memcpy(&b,w16+(((row+j*16)/16)*64+k/16)*256+(gr()*16+rc())*8,16);acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,acc[j]);}}
 __attribute__((shared)) float raw[16*33];
 f8 sum{};
 if(part<2){
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)raw[(gr()*8+e)*33+j*16+rc()]=acc[j][e];
  __builtin_amdgcn_s_barrier();
  for(uint k=0;k<32;k+=16){h8 x{},ones{};for(uint e=0;e<8;e++){float v=raw[rc()*33+k+gr()*8+e];x[e]=(_Float16)(v*v);ones[e]=(_Float16)1.f;}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,sum);}
 }
#if HIP_VIT_HOIST_SCALE
 // The per-row scale read once (uniform); inside the loop it was re-read after every out[] store (aliasing), 16 global_load + wait pairs.
 const float scale=part==0?5.65625f*w[1572864+row/32]:1.f;
 for(uint j=0;j<2;j++)for(uint e=0;e<8;e++){float v=acc[j][e];if(part<2){v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*scale;}out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=byte_F(v);}
#else
 for(uint j=0;j<2;j++)for(uint e=0;e<8;e++){float v=acc[j][e];if(part<2){float scale=part==0?5.65625f*w[1572864+row/32]:1.f;v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*scale;}out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=byte_F(v);}
#endif
}
// Byte-input twin: the contract output arrives as E4M3 bytes (vit_contract_blocked_fp8*_bstream); each A fragment is two
// dword loads decoded to the same half values (E4M3 is exact in F16) the f32 kernel converts from.
WAVE void vit_qkv_project_normalize_fused_f16compact_fp8_bytein(const float*in,const float*w,unsigned char*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint perpart=(tokens/16)*32,part=bid()/perpart,local=bid()%perpart,first=local/32*16,row=local%32*32;if(part>=3)return;f8 acc[2]{};
 const uint*ip=reinterpret_cast<const uint*>(in);
 for(uint k=0;k<1024;k+=16){h8 a{};uint off=((first+rc())*1024+k+gr()*8)/4,w0=ip[off],w1=ip[off+1];
  a[0]=(_Float16)__builtin_amdgcn_cvt_f32_fp8(int(w0),0);a[1]=(_Float16)__builtin_amdgcn_cvt_f32_fp8(int(w0),1);a[2]=(_Float16)__builtin_amdgcn_cvt_f32_fp8(int(w0),2);a[3]=(_Float16)__builtin_amdgcn_cvt_f32_fp8(int(w0),3);
  a[4]=(_Float16)__builtin_amdgcn_cvt_f32_fp8(int(w1),0);a[5]=(_Float16)__builtin_amdgcn_cvt_f32_fp8(int(w1),1);a[6]=(_Float16)__builtin_amdgcn_cvt_f32_fp8(int(w1),2);a[7]=(_Float16)__builtin_amdgcn_cvt_f32_fp8(int(w1),3);
  for(uint j=0;j<2;j++){h8 b{};__builtin_memcpy(&b,reinterpret_cast<const _Float16*>(w)+part*1048576+(row+j*16+rc())*1024+k+gr()*8,16);acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,acc[j]);}}
 __attribute__((shared)) float raw[16*33];
 f8 sum{};
 if(part<2){
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)raw[(gr()*8+e)*33+j*16+rc()]=acc[j][e];
  __builtin_amdgcn_s_barrier();
  for(uint k=0;k<32;k+=16){h8 x{},ones{};for(uint e=0;e<8;e++){float v=raw[rc()*33+k+gr()*8+e];x[e]=(_Float16)(v*v);ones[e]=(_Float16)1.f;}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,sum);}
 }
 for(uint j=0;j<2;j++)for(uint e=0;e<8;e++){float v=acc[j][e];if(part<2){float scale=part==0?5.65625f*w[1572864+row/32]:1.f;v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*scale;}out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=byte_F(v);}
}
// Half-input QKV: the contract output arrives as F16 (vit_contract_blocked_fp8*_hstream); one 16-byte load per A
// fragment, no conversion. N column fragments per wave (N=4: 64 columns = two heads, two square-sum MMAs). Same MMA
// operands, accumulation order, normalization and byte store as the f32-input kernel.
template<uint N>
DEV void vit_qkv_h16in_body(const float*in,const float*w,unsigned char*out,uint tokens){
 const uint cols=N*16,groups=1024/cols;uint perpart=(tokens/16)*groups,part=bid()/perpart,local=bid()%perpart,first=local/groups*16,row=local%groups*cols;if(part>=3)return;f8 acc[N]{};
 for(uint k=0;k<1024;k+=16){h8 a{};__builtin_memcpy(&a,reinterpret_cast<const _Float16*>(in)+(first+rc())*1024+k+gr()*8,16);for(uint j=0;j<N;j++){h8 b{};__builtin_memcpy(&b,reinterpret_cast<const _Float16*>(w)+part*1048576+(row+j*16+rc())*1024+k+gr()*8,16);acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,acc[j]);}}
 __attribute__((shared)) float raw[16*(N*16+1)];
 f8 sum[N/2]{};
 if(part<2){
  for(uint j=0;j<N;j++)for(uint e=0;e<8;e++)raw[(gr()*8+e)*(N*16+1)+j*16+rc()]=acc[j][e];
  __builtin_amdgcn_s_barrier();
  for(uint h=0;h<N/2;h++)for(uint k=0;k<32;k+=16){h8 x{},ones{};for(uint e=0;e<8;e++){float v=raw[rc()*(N*16+1)+h*32+k+gr()*8+e];x[e]=(_Float16)(v*v);ones[e]=(_Float16)1.f;}sum[h]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,sum[h]);}
 }
 for(uint j=0;j<N;j++)for(uint e=0;e<8;e++){float v=acc[j][e];if(part<2){uint h=j/2;float scale=part==0?5.65625f*w[1572864+row/32+h]:1.f;v*=__builtin_amdgcn_rsqf(maxf(sum[h][e],6.198883056640625e-5f))*scale;}out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=byte_F(v);}
}
WAVE void vit_qkv_project_normalize_fused_f16compact_fp8_h16in(const float*in,const float*w,unsigned char*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_qkv_h16in_body<2>(in,w,out,tokens);}
WAVE void vit_qkv_project_normalize_fused_f16compact_fp8_h16in_n4(const float*in,const float*w,unsigned char*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;vit_qkv_h16in_body<4>(in,w,out,tokens);}
WAVE void vit_qkv_project_normalize_fused_f16compact(const float*in,const float*w,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint perpart=(tokens/16)*32,part=bid()/perpart,local=bid()%perpart,first=local/32*16,row=local%32*32;if(part>=3)return;f8 acc[2]{};
 for(uint k=0;k<1024;k+=16){h8 a{};for(uint e=0;e<8;e++)a[e]=(_Float16)in[(first+rc())*1024+k+gr()*8+e];for(uint j=0;j<2;j++){h8 b{};__builtin_memcpy(&b,reinterpret_cast<const _Float16*>(w)+part*1048576+(row+j*16+rc())*1024+k+gr()*8,16);acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,acc[j]);}}
 __attribute__((shared)) float raw[16*33];
 f8 sum{};
 if(part<2){
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)raw[(gr()*8+e)*33+j*16+rc()]=acc[j][e];
  __builtin_amdgcn_s_barrier();
  for(uint k=0;k<32;k+=16){h8 x{},ones{};for(uint e=0;e<8;e++){float v=raw[rc()*33+k+gr()*8+e];x[e]=(_Float16)(v*v);ones[e]=(_Float16)1.f;}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(x,ones,sum);}
 }
 for(uint j=0;j<2;j++)for(uint e=0;e<8;e++){float v=acc[j][e];if(part<2){float scale=part==0?5.65625f*w[1572864+row/32]:1.f;v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*scale;}out[(part*tokens+first+gr()*8+e)*1024+row+j*16+rc()]=F(v);}
}

// GPU-only decision: no per-frame CPU readback. One wave per logical token.
WAVE void reuse_token_stats(const float*x,const float*anchor,const uint*state,float*stats,uint n,uint rw,uint rh,uint stride){
 uint t=bid(),lane=__builtin_amdgcn_workitem_id_x();if(t>=n)return;
 uint r=(t&~15u)|((t&1u)<<3)|((t&14u)>>1);
 float d=0,a=0,bad=0;uint changed=0;
 if(state[2]){bool valid=r/stride<rh&&r%stride<rw;for(uint c=lane;c<1024;c+=32){float v=x[t*1024+c],old=anchor[t*1024+c];changed|=bits(v)^bits(old);
  if(!__builtin_isfinite(v)||!__builtin_isfinite(old))bad=1;else if(valid){d+=absf(v-old);a+=absf(old);}}}
 for(uint offset=16;offset;offset>>=1){d+=value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(d)));a+=value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(a)));bad=maxf(bad,value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(bad))));changed|=__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,changed);}
 if(!lane){stats[t*4]=d;stats[t*4+1]=a;stats[t*4+2]=bad?1.e30f:d/maxf(a,16.f);stats[t*4+3]=changed?1.f:0.f;}
}
WAVE void reuse_decide(const float*stats,uint*state,uint n,uint period,float global_limit,float local_limit,uint mode,const float*image_delta,uint image_tiles,uint image_cols,float image_limit){
 uint lane=__builtin_amdgcn_workitem_id_x();float d=0,a=0,peak=0,image_peak=0,changed=0;
 // Require a coherent 2x2 tile change: isolated near-black edge ratios must not veto every pan.
 for(uint t=lane;t<image_tiles;t+=32){if(image_delta[t]>=1.e29f)image_peak=1.e30f;
  if(t%image_cols+1<image_cols&&t+image_cols+1<image_tiles){float patch=minf(minf(image_delta[t],image_delta[t+1]),minf(image_delta[t+image_cols],image_delta[t+image_cols+1]));image_peak=maxf(image_peak,patch);}}
 for(uint t=lane;t<n;t+=32){d+=stats[t*4];a+=stats[t*4+1];peak=maxf(peak,stats[t*4+2]);changed=maxf(changed,stats[t*4+3]);}
 for(uint offset=16;offset;offset>>=1){d+=value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(d)));a+=value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(a)));peak=maxf(peak,value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(peak))));image_peak=maxf(image_peak,value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(image_peak))));changed=maxf(changed,value(__builtin_amdgcn_ds_bpermute(((lane+offset)&31)*4,bits(changed))));}
 if(!lane){float rel=d/maxf(a,16.f);uint reason=!state[2]?1:mode==2?2:(state[1]+1>=period&&!(mode==1&&period>1&&changed==0))?3:(!__builtin_isfinite(rel)||peak>=1.e29f||image_peak>=1.e29f)?4:mode==3?0:rel>global_limit?5:peak>local_limit?6:image_peak>image_limit?7:0;
 if(reason==0&&mode==1&&changed==0&&state[1]+1>=period)reason=8;
 state[0]=reason==0||reason==8;state[1]=state[0]?(state[1]<period?state[1]+1:period):0;state[2]=1;state[3]++;state[4]=bits(rel);state[5]=bits(peak);state[6]=reason;state[7]=bits(image_peak);}
}
WAVE void reuse_finish(const float*x,const float*full,float*anchor_in,float*anchor_out,const float*gain,const uint*state,float*out,uint count,const float*image_signature,float*image_anchor,uint image_count){
 uint i=bid()*256+__builtin_amdgcn_workitem_id_x();bool reuse=state[0]!=0;
 if(!reuse){uint s=i;for(uint j=0;j<8&&s<image_count;j++,s+=32)image_anchor[s]=image_signature[s];}
 for(uint j=0;j<8;j++,i+=32)if(i<count){float v=x[i];if(reuse){float a=anchor_in[i],b=anchor_out[i];out[i]=v==a?b:F(H(__builtin_fmaf(gain[i%1024],v-a,b)));}else{float y=full[i];out[i]=y;anchor_in[i]=v;anchor_out[i]=y;}}
}
// Exact 32x32 RGB tile means; sparse sampling aliases moving edges.
WAVE void reuse_image_stats(const float*rgba,const float*anchor,const uint*state,float*signature,float*delta,uint width,uint height){
 uint tile=bid(),cols=(width+31)/32,rows=(height+31)/32,lane=__builtin_amdgcn_workitem_id_x();if(tile>=cols*rows)return;
 float r=0,g=0,b=0,bad=0;
 for(uint p=lane;p<1024;p+=32){uint x=tile%cols*32+p%32,y=tile/cols*32+p/32;x=x<width?x:width-1;y=y<height?y:height-1;
  float vr=rgba[(y*width+x)*4],vg=rgba[(y*width+x)*4+1],vb=rgba[(y*width+x)*4+2];if(!__builtin_isfinite(vr)||!__builtin_isfinite(vg)||!__builtin_isfinite(vb))bad=1;else{r+=vr;g+=vg;b+=vb;}}
 for(uint o=16;o;o>>=1){r+=value(__builtin_amdgcn_ds_bpermute(((lane+o)&31)*4,bits(r)));g+=value(__builtin_amdgcn_ds_bpermute(((lane+o)&31)*4,bits(g)));b+=value(__builtin_amdgcn_ds_bpermute(((lane+o)&31)*4,bits(b)));bad=maxf(bad,value(__builtin_amdgcn_ds_bpermute(((lane+o)&31)*4,bits(bad))));}
 if(!lane){r/=1024;g/=1024;b/=1024;signature[tile*3]=r;signature[tile*3+1]=g;signature[tile*3+2]=b;float rel=0;if(state[2]){float ar=anchor[tile*3],ag=anchor[tile*3+1],ab=anchor[tile*3+2];rel=(absf(r-ar)+absf(g-ag)+absf(b-ab))/maxf(absf(ar)+absf(ag)+absf(ab),1.e-6f);}delta[tile]=bad?1.e30f:rel;}
}
WAVE void reuse_image_commit(const float*signature,float*anchor,const uint*state,uint count){
 if(state[0])return;uint i=bid()*256+__builtin_amdgcn_workitem_id_x();for(uint j=0;j<8;j++,i+=32)if(i<count)anchor[i]=signature[i];
}
