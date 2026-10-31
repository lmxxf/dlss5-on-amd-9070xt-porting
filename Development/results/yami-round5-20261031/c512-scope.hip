#define HIP_ISA_HALF 1
#define C512_PAIR_PACK 1
#define C512_COMPACT_QKV_ATTN 1
#define C512_FUSED_QKV_ATTN 1
#define HIP_PREPACKED_WEIGHTS 1
#define HIP_FFN_HOIST_RES 2
#define HIP_PDL_KERNELS 0
#define HIP_FMED3_CLAMP 1
#define C512_COMPACT_NOF 1
#define C512_COMPACT_RCP 1
#define C512_COMPACT_FUSEQKV 1
#define C512_COMPACT_QKV_DEEP 4
#define C512_COMPACT_QKV_SCHED 1
#ifndef HIP_MH_SCALAR_DIAGONAL
#define HIP_MH_SCALAR_DIAGONAL 1
#endif
#ifndef HIP_FFN_INPUT_PACK4
#define HIP_FFN_INPUT_PACK4 1
#endif
#ifndef HIP_QKV_SPECIALIZE
#define HIP_QKV_SPECIALIZE 1
#endif
#ifndef HIP_QKV_ROW_SUM
#define HIP_QKV_ROW_SUM 1
#endif
#ifndef HIP_ISA_HALF
#define HIP_ISA_HALF 1
#endif
// Native gfx1201 FP8 WMMA multihead kernels. Original externalfloat32 ABI.
// Mirrors native_c64.hlsl non-wave math, native_c64_shift.hlsl, native_c64_ds.hlsl.
// Host validates geometry/channel contracts; all tensors/weights externally float32.
// Every exported kernel uses one-dimensional blocks of exactly512 threads.
#pragma clang fp contract(off)
#pragma clang fp reassociate(off)
typedef unsigned uint;
#define DEV __attribute__((device)) __attribute__((always_inline))
#ifndef HIP_KERNEL_WPE
#define HIP_KERNEL_WPE 0 /* 2026-10-01 compiler sweep (diagnostic, default 0 = no attribute): amdgpu_waves_per_eu lower bound applied to every kernel of the module */
#endif
#ifndef HIP_KERNEL_ATTR
#if HIP_KERNEL_WPE
#define HIP_KERNEL_ATTR __attribute__((amdgpu_waves_per_eu(HIP_KERNEL_WPE)))
#else
#define HIP_KERNEL_ATTR
#endif
#endif
#define KERNEL extern "C" __attribute__((global)) HIP_KERNEL_ATTR
#ifndef HIP_FFN_WAVE_NORM
#define HIP_FFN_WAVE_NORM 0 /* 2026-09-23 (NOT bit-exact, default off): parts 0/1 of the QKV are computed one head (32 columns) per
 wave so the normalisation's sum of squares is 16 in-lane terms + one ds_bpermute add, with rsqrt/scale/norm bytes in registers -- no
 storage.raw round trip, no two barriers, no 128-thread serial loop. Re-associates the 32-term sum (two 16-term partial sums) before
 rsqrt; everything else unchanged. -0.18/-0.14 ms on top of HIP_FFN_TRANSPOSED_TAIL. Enable only after image-quality judgment. */
#endif
#ifndef HIP_FFN_LINE_STORES
#define HIP_FFN_LINE_STORES 0 /* 2026-09-24 experiment (needs TRANSPOSED_TAIL, non-BatchNorm, ByteFeature out): the FFN+QKV tail's global
 stores go out as full 128-byte lines. clock-ledger-20260924: the kernel family's power is in its narrow global stores (removing norm
 stores +4% clock, norm+out +7%), and merging 24 b8 into 3 b64 per lane did not help because each store instruction still touches 16
 lines (lane = row). Here the out bytes are written from the qfeature LDS copy after the existing barrier (one wave = one row = 256
 contiguous bytes at C=256), and the three norm parts are staged in LDS (parts 0/1 share a buffer, part 2 its own) and written row-wise:
 part 0 flushed after part 1's first barrier, parts 1/2 after one extra barrier at the end. Same bytes, same addresses: bit-exact. */
#endif
#ifndef MH_PAD_ZERO_VEC
#define MH_PAD_ZERO_VEC 0 /* 1: mapped-padding zero tiles of the FFN+QKV kernels stored as 8-byte words instead of bytes; same zeros (results/deep-tail2-20260930) */
#endif
#ifndef HIP_FFN_TRANSPOSED_TAIL
#define HIP_FFN_TRANSPOSED_TAIL 1 /* 2026-09-23: projection and QKV WMMAs take swapped operands so each lane holds one token row and
 8 consecutive columns (A/B fragment register layouts are identical). Residual input, out/qfeature bytes, storage.raw rows and norm
 bytes then go 8-consecutive per lane; the compiler merges the byte stores into b64 (c256 kernel: global stores 38->10, -212
 instructions). The serial 32-term sum of squares keeps its order: bit-exact. -0.28/-0.21 ms (1080/900), three replications. */
#endif
#ifndef HIP_LDS_FENCE
#define HIP_LDS_FENCE 1 /* 2026-09-22 fence scope: workgroup fences here only publish LDS; the default "workgroup" scope made gfx12 emit global_inv (vector L0 invalidate) and wait for all outstanding global loads at every barrier. Restricting the fence to the LDS address space removes both; bit-exact, see Development/results/fence-scope-all-20260922 */
#endif
#if HIP_LDS_FENCE
#define WG_FENCE(k) __builtin_amdgcn_fence(k,"workgroup","local")
#else
#define WG_FENCE(k) __builtin_amdgcn_fence(k,"workgroup")
#endif
#ifndef HIP_BARRIER_FENCE
#define HIP_BARRIER_FENCE 0 /* 1: every bare s_barrier in this file gets the WG_FENCE release/acquire pair, so LLVM cannot move LDS accesses across it (LLVM23 bit-exactness probe, results/llvm23-vit-20261002) */
#endif
#if HIP_BARRIER_FENCE
#define __builtin_amdgcn_s_barrier() (WG_FENCE(3),__builtin_amdgcn_s_barrier(),WG_FENCE(2))
#endif
DEV uint bits(float x){union{float f;uint u;}v;v.f=x;return v.u;}
DEV float value(uint x){union{float f;uint u;}v;v.u=x;return v.f;}
DEV uint lane(){return __builtin_amdgcn_workgroup_id_x()*256u+__builtin_amdgcn_workitem_id_x();}
DEV float absf(float x){return value(bits(x)&0x7fffffffu);}
DEV float maxf(float a,float b){return a>b?a:b;}
DEV float minf(float a,float b){return a<b?a:b;}
// HIP_FMED3_CLAMP (experiment fmed3-clamp, default 0): clamps as one v_med3_num_f32 via __builtin_amdgcn_fmed3f. The fminf/
// fmaxf forms also become med3, but with a v_max_num(x,x) canonicalisation in front of each (IEEE mode quiets sNaN); the
// target intrinsic has no such requirement. Same result for every non-NaN input; a NaN input gives lo in both forms.
#ifndef HIP_FMED3_CLAMP
#define HIP_FMED3_CLAMP 0
#endif
#if HIP_FMED3_CLAMP
DEV float clampf(float a,float lo,float hi){return __builtin_amdgcn_fmed3f(a,lo,hi);}
#else
DEV float clampf(float a,float lo,float hi){return minf(maxf(a,lo),hi);}
#endif
#define HIP_FP8_CLAMP(x) __builtin_amdgcn_fmed3f((x),-448.f,448.f)
DEV uint shift_rne(uint x,uint n){if(n>31)return 0;uint q=x>>n,mask=(1u<<n)-1u,r=x&mask,mid=1u<<(n-1);return q+uint(r>mid||(r==mid&&(q&1u)));}
DEV uint half_bits(float x){uint b=bits(x),a=b&0x7fffffffu,s=(b>>16)&0x8000u;int e=int(a>>23)-127;
 if(a>=0x7f800000u)return s|0x7c00u|((a&0x7fffffu)?0x200u:0u);
 if(e<-25)return s;
 if(e<-14)return s|shift_rne((a&0x7fffffu)|0x800000u,uint(-e-1));
 if(e>15)return s|0x7c00u;
 uint h=((a+0xfffu+((a>>13)&1u))>>13)-0x1c000u;return s|(h>=0x7c00u?0x7c00u:h);
}
DEV float from_half(uint h){uint s=(h&0x8000u)<<16,e=(h>>10)&31u,m=h&1023u;if(e==31)return value(s|0x7f800000u|(m<<13));if(!e){if(!m)return value(s);float f=float(m)*5.9604644775390625e-8f;return s?-f:f;}return value(s|((e+112)<<23)|(m<<13));}
// FAST TIER (lossy, 2026-10-01 fast-tier): W2_FAST_NUM bit0 = H()/Hrtz() are the identity (f32 activation/normalisation
// path, no half round trip between f32 ops; explicit half stores still convert), bit1 = w2_inverse is a bare rcp.
#ifndef W2_FAST_NUM
#define W2_FAST_NUM 0
#endif
#if W2_FAST_NUM&1
DEV float H(float x){return x;}
#elif defined(HIP_ISA_HALF)
// Keep f32 expression evaluation and both rounding boundaries opaque to fusion.
DEV float H(float x){uint h;float result;asm("v_cvt_f16_f32 %0, %1" : "=v"(h) : "v"(x));asm("v_cvt_f32_f16 %0, %1" : "=v"(result) : "v"(h));return result;}
#else
DEV float H(float x){if((bits(x)&0x7fffffffu)>=0x7f800000u)return x;return from_half(half_bits(x));}
#endif
#ifndef HIP_NATIVE_FP8_F
#define HIP_NATIVE_FP8_F 1
#endif
#ifndef HIP_BRANCHLESS_F
#define HIP_BRANCHLESS_F 1 /* 2026-09-17: with deep_fast and the fused attention file -0.12ms, bit-exact. F() as clamp+cvt+cndmask instead of a divergent branch pair per call (same values for every non-NaN input; NaN gives -448 instead of +-448) */
#endif
#if HIP_NATIVE_FP8_F && HIP_BRANCHLESS_F
DEV float F(float x){float c=HIP_FP8_CLAMP(x+0.f);return __builtin_amdgcn_cvt_f32_fp8(__builtin_amdgcn_cvt_pk_fp8_f32(c,0.f,0,false),0);}
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
DEV float half_square_pair(float lower,float upper){float a=lower*lower,b=H(upper*upper),sum=a+b;
 if((bits(sum)&0x7fffffffu)>=0x7f800000u)return H(sum);
 float large=maxf(a,b),small=minf(a,b),recovered=sum-large,error=small-recovered;
 if(sum==65520.f)return error<0?65504.f:H(sum);
 float rounded=H(sum);if(error==0||(bits(rounded)&0x7fffffffu)>=0x7f800000u||rounded==sum)return rounded;
 uint hb=half_bits(rounded),lo_bits=sum<rounded?hb-1:hb;float lo=from_half(lo_bits),hi=from_half(lo_bits+1),mid=(lo+hi)*.5f;
 return sum==mid?(error>0?hi:lo):rounded;
}
DEV float exp_approx(float x){float a=clampf(H(x*.044921875f+1.30078125f),1.03125f,1.5693359375f);return from_half(((half_bits(a)<<5)+0x8000u)&65535u);}
DEV bool valid_c(uint c){return c==64u||c==128u||c==256u;}
DEV bool valid_attn(uint c){return valid_c(c)||c==512u;}
DEV bool valid_spatial(uint c){return c==32u||valid_attn(c);}
DEV uint pixel_in_window(uint window,uint token,uint width){return ((window/(width/8))*8+token/8)*width+(window%(width/8))*8+token%8;}
DEV uint window_of_pixel(uint p,uint width){return (p/width/8)*(width/8)+(p%width)/8;}
DEV uint token_of_pixel(uint p,uint width){return ((p/width)%8)*8+(p%width)%8;}

#if !__has_builtin(__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12)
#error "gfx12 FP8 WMMA compiler required; no scalar fallback"
#endif
using i2=int __attribute__((ext_vector_type(2)));
using f8=float __attribute__((ext_vector_type(8)));
#define TILE KERNEL __attribute__((amdgpu_flat_work_group_size(512,512)))
DEV uint q8(float x){return uint(__builtin_amdgcn_cvt_pk_fp8_f32(x,x,0,false))&255u;}
DEV void pack(i2&v,uint e,float x){v[e/4]=int(uint(v[e/4])|(q8(x)<<(8*(e%4))));}
// Diagnostic production HLSL f32tof16 path: explicit RTZ, distinct from
// Cast<F16> / ISA_HALF RNE. Keep both exported contracts until measured.
#if W2_FAST_NUM&1
DEV float Hrtz(float x){return x;}
#else
DEV float Hrtz(float x){uint h;float out;asm("v_cvt_pkrtz_f16_f32 %0, %1, %1" : "=v"(h) : "v"(x));asm("v_cvt_f32_f16 %0, %1" : "=v"(out) : "v"(h));return out;}
#endif
//64x64 output tile,16 waves. Each thread stages one packed4-byte A row word
// and one B column word; each packed operand is reused by four waves.
DEV uint pack4(float a,float b,float c,float d){uint lo=uint(__builtin_amdgcn_cvt_pk_fp8_f32(a,b,0,false));uint hi=uint(__builtin_amdgcn_cvt_pk_fp8_f32(c,d,0,false));return (lo&65535u)|((hi&65535u)<<16);}
template<bool ByteInput=false,bool ByteOutput=false,bool Normalize=false,bool Crop=false>
DEV void fast_dense(const float* input,const float* feature,const float* weights,float* output,
 uint M,uint N,uint K,uint matrix_offset,uint skip_offset,uint epilogue,uint width=0,uint valid_width=0,uint valid_height=0,uint matrix_residual=0,uint cropw=0,uint croph=0,uint workw=0,uint sx=0,uint sy=0){
 __attribute__((shared)) uint sa[576];
 __attribute__((shared)) uint sb[576];
 uint block=__builtin_amdgcn_workgroup_id_x(),nt=(N+63)/64;
 if(!nt||block>=((M+63)/64)*nt)return;
 uint mr=block/nt*64,nc=block%nt*64,t=__builtin_amdgcn_workitem_id_x(),wave=t/32,l=t%32,rc=l&15,group=l>>4;
 uint wm=wave/4,wn=wave%4,first=mr+wm*16,col=nc+wn*16;
 f8 accum{};
 #pragma unroll
 for(uint e=0;e<8;e++){uint p=first+group*8+e,n=col+rc;if(p<M&&n<N&&skip_offset!=~0u&&!matrix_residual)accum[e]=Hrtz(feature[p*N+n]*weights[skip_offset+n]);}
 if(matrix_residual){
  uint n=col+rc;float remain=n<N?weights[skip_offset+n]:0.f;
  for(uint part=0;part<3;part++){
   float component;
   if(absf(remain)<.015625f){float x=absf(remain)*512.f;uint q=uint(x);float r=x-float(q);q+=uint(r>.5f||(r==.5f&&(q&1)));q=q>7?7:q;component=(remain<0?-1.f:1.f)*float(q)/512.f;}
   else component=F(remain);
   remain-=component;
#if HIP_MH_SCALAR_DIAGONAL
   for(uint e=0;e<8;e++){uint p=first+group*8+e;if(p<M&&n<N){float x=__builtin_amdgcn_cvt_f32_fp8(int(q8(feature[p*N+n])),0);for(uint half=0;half<2;half++)accum[e]=__builtin_fmaf(x,half==(n%32)/16?component:0.f,accum[e]);}}
#else
   #pragma unroll
   for(uint half=0;half<2;half++){i2 a{},b{};
    #pragma unroll
    for(uint e=0;e<8;e++){uint k=half*16+group*8+e,p=first+rc,g=col/32*32;float x=(p<M&&g+k<N)?feature[p*N+g+k]:0.f;pack(a,e,x);pack(b,e,(n<N&&k==n%32)?component:0.f);}
    accum=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,accum);
   }
#endif
  }
 }
 for(uint chunk=0;chunk<K/32;chunk++){
  uint row=t/8,word=t%8,k=chunk*32+word*4,p=mr+row,n=nc+row;
  float a0=0,a1=0,a2=0,a3=0,b0=0,b1=0,b2=0,b3=0;
  if constexpr(!ByteInput){if(p<M&&(!valid_width||(p%width<valid_width&&p/width<valid_height))){uint off=p*K+k;a0=input[off];a1=input[off+1];a2=input[off+2];a3=input[off+3];}}
  #ifndef HIP_PREPACKED_WEIGHTS
  if(n<N){uint off=matrix_offset+n*K+k;b0=weights[off];b1=weights[off+1];b2=weights[off+2];b3=weights[off+3];}
#endif
  if constexpr(ByteInput){sa[row*9+word]=(p<M&&(!valid_width||(p%width<valid_width&&p/width<valid_height)))?reinterpret_cast<const uint*>(input)[(p*K+k)/4]:0u;}else sa[row*9+word]=pack4(a0,a1,a2,a3);
#ifdef HIP_PREPACKED_WEIGHTS
  // Matrix bytes start at the original float-region offset; nonmatrix fields stay f32.
  const uint* packed_weights=reinterpret_cast<const uint*>(weights);
  sb[row*9+word]=n<N?packed_weights[matrix_offset+(n*K+k)/4]:0u;
#else
  sb[row*9+word]=pack4(b0,b1,b2,b3);
#endif
  __builtin_amdgcn_s_barrier();
  #pragma unroll
  for(uint half=0;half<2;half++){
   uint word0=half*4+group*2;
   i2 a={int(sa[(wm*16+rc)*9+word0]),int(sa[(wm*16+rc)*9+word0+1])};
   i2 b={int(sb[(wn*16+rc)*9+word0]),int(sb[(wn*16+rc)*9+word0+1])};
   accum=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,accum);
  }
  // All sixteen waves finish reading before any writer reuses this K32 tile.
  __builtin_amdgcn_s_barrier();
  // Production fast path: keep accumulator live across every K16/K32.
 }
#if HIP_QKV_ROW_SUM
 if constexpr(Normalize){
  __attribute__((shared)) float tile_qkv[64*65],norm_inv[128];
  for(uint e=0;e<8;e++)tile_qkv[(wm*16+group*8+e)*65+wn*16+rc]=accum[e];
  __builtin_amdgcn_s_barrier();
  uint channels=N/3,heads=channels/32,scale=4*channels*channels+heads*4096;
  if(t<128){uint row=t%64,h=t/64,n=nc+h*32,part=n/channels,head=(n%channels)/32;float inv=1.f;
   if(mr+row<M&&n<N&&part<2){float ss=0.f;
    for(uint j=0;j<32;j++){float v=tile_qkv[row*65+h*32+j];ss+=v*v;}
    inv=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?weights[scale+head]:1.f);
   }norm_inv[h*64+row]=inv;
  }
  __builtin_amdgcn_s_barrier();
  for(uint e=0;e<8;e++){uint row=wm*16+group*8+e,p=mr+row,n=col+rc;if(p<M&&n<N)reinterpret_cast<unsigned char*>(output)[p*N+n]=static_cast<unsigned char>(q8(F(accum[e]*norm_inv[(wn/2)*64+row])));}
  return;
 }
#else
 if constexpr(Normalize){
  __attribute__((shared)) float tile_qkv[64*64];
  for(uint e=0;e<8;e++)tile_qkv[(wm*16+group*8+e)*64+wn*16+rc]=accum[e];
  __builtin_amdgcn_s_barrier();
  uint channels=N/3,heads=channels/32,scale=4*channels*channels+heads*4096;
  for(uint row0=wave/2;row0<64;row0+=8){
   uint p=mr+row0,n=nc+(wave%2)*32+l;
   if(p>=M||n>=N)continue;
   uint part=n/channels,head=(n%channels)/32;
   float v=tile_qkv[row0*64+(wave%2)*32+l],inv=1.f;
   if(part<2){float sq=v*v,ss=0.f;
    #pragma unroll
    for(uint j=0;j<32;j++)ss+=value(__builtin_amdgcn_readlane(bits(sq),j));
    inv=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?weights[scale+head]:1.f);
   }
   reinterpret_cast<unsigned char*>(output)[p*N+n]=static_cast<unsigned char>(q8(F(v*inv)));
  }
  return;
 }
#endif
 #pragma unroll
 for(uint e=0;e<8;e++){uint p=first+group*8+e,n=col+rc;if(p>=M||n>=N)continue;float a=accum[e],v;
  if(epilogue==1){float gate=clampf(a,-4.f,4.f),q=__builtin_fmaf(absf(gate),-.055908203125f,.447265625f),poly=__builtin_fmaf(gate,q,.89453125f);v=F(a*poly);}
  else v=epilogue==2?a:epilogue==3?Hrtz(a):epilogue==4?F(a):epilogue==5?F(Hrtz(a)):F(H(a));
  v=(valid_width&&(p%width>=valid_width||p/width>=valid_height))?0.f:v;
  uint dest=p*N+n;
  if constexpr(Crop){uint x=p%workw,y=p/workw;if(x<sx||y<sy||x>=sx+cropw||y>=sy+croph)continue;dest=((y-sy)*cropw+x-sx)*N+n;}
  if constexpr(ByteOutput)reinterpret_cast<unsigned char*>(output)[dest]=static_cast<unsigned char>(q8(v));else output[dest]=v;
 }
}
// Final build contract: fused FP8act overwritten without HW_QUANTIZE:
// expand=F(polynomial(FP32 acc)), contract=F(H(FP32 acc)).
TILE void mh_ffn_expand_fast(const float* input,const float* weights,float* hidden,uint tokens,uint channels){if(valid_c(channels))fast_dense(input,input,weights,hidden,tokens,4*channels,channels,0,~0u,1);}
TILE void mh_ffn_contract_fast(const float* hidden,const float* weights,float* middle,uint tokens,uint channels){if(valid_c(channels))fast_dense(hidden,hidden,weights,middle,tokens,channels,4*channels,4*channels*channels,~0u,5);}
// Production MAP_FEATURE+DIRECT_CAST variant. Not MATRIX_RESIDUAL.
TILE void mh_ffn_project_fast(const float* middle,const float* feature,const float* weights,float* output,uint tokens,uint channels){if(valid_c(channels))fast_dense(middle,feature,weights,output,tokens,channels,channels,8*channels*channels,9*channels*channels,4);}
// Raw FP32 QKV; do NOT feed to legacy H-reduction normalization.
TILE void mh_qkv_fast(const float* input,const float* weights,float* qkv,uint tokens,uint channels){if(valid_attn(channels))fast_dense(input,input,weights,qkv,tokens,3*channels,channels,0,~0u,2);}
// Explicit scalar-residual diagnostic variant. Does NOT implement production
// NATIVE_MATRIX_RESIDUAL (three FP8 diagonal MMAs). post:0 F(H),3 H,4 directF.
TILE void mh_attention_project_fast_scalar(const float* av,const float* feature,const float* weights,float* output,uint tokens,uint channels,uint post){if(valid_attn(channels)&&(post==0||post==3||post==4))fast_dense(av,feature,weights,output,tokens,channels,channels,3*channels*channels,4*channels*channels+(channels/32)*4096+channels/32,post==0?5:post);}

// Same input/weight/output ABI as contract_fast, only the last epilogue differs.
TILE void mh_ffn_contract_fast_pre(const float* hidden,const float* weights,float* middle,uint tokens,uint channels){if(valid_c(channels))fast_dense(hidden,hidden,weights,middle,tokens,channels,4*channels,4*channels*channels,~0u,2);}
TILE void mh_ffn_contract_fast_rtz(const float* hidden,const float* weights,float* middle,uint tokens,uint channels){if(valid_c(channels))fast_dense(hidden,hidden,weights,middle,tokens,channels,4*channels,4*channels*channels,~0u,5);}

TILE void mh_ffn_contract_fast_rne(const float* hidden,const float* weights,float* middle,uint tokens,uint channels){if(valid_c(channels))fast_dense(hidden,hidden,weights,middle,tokens,channels,4*channels,4*channels*channels,~0u,0);}
// Production NATIVE_QKV_FAST2=0: rawFP32 squares, sequentialFP32 sum,
// unrounded rsqrt*scale and direct saturated FP8 cast. Not legacy normalize.
// Input/output HWC with three parts[p][Q,K,V][C]. Block256, one thread per head/token.
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256)))
void mh_qkv_normalize_fast(const float* qkv,const float* weights,float* normalized,uint tokens,uint channels){
 uint i=lane(),heads=channels/32;if(!valid_attn(channels)||i>=tokens*heads)return;uint p=i/heads,head=i%heads,scale=4*channels*channels+heads*4096;
 for(uint part=0;part<3;part++){uint base=(p*3+part)*channels+head*32;float inv=1.f;
  if(part<2){float ss=0.f;for(uint c=0;c<32;c++){float v=qkv[base+c];float square=v*v;ss+=square;}inv=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?weights[scale+head]:1.f);}
  for(uint c=0;c<32;c++)normalized[base+c]=F(qkv[base+c]*inv);
 }
}

// Matrix-residual projection: three original FP8 diagonal terms, no seed H.
// post0 F(H_RTZ),3 H_RTZ,4 directFP8; feature must be exact finiteFP8.
TILE void mh_attention_project_fast_matrix(const float* av,const float* feature,const float* weights,float* output,uint tokens,uint channels,uint post){if(valid_attn(channels)&&(post==0||post==3||post==4))fast_dense(av,feature,weights,output,tokens,channels,channels,3*channels*channels,4*channels*channels+(channels/32)*4096+channels/32,post==0?5:post,0,0,0,1);}
#define FASTWAVE KERNEL __attribute__((amdgpu_flat_work_group_size(32,32)))
using h8=_Float16 __attribute__((ext_vector_type(8)));
// normalized is raster[p][Q,K,V][C], ex/prob are[window][head][q64][k64].
FASTWAVE void mh_scores_exp_fast(const float* normalized,const float* weights,float* ex,uint width,uint height,uint channels){
 uint tile=__builtin_amdgcn_workgroup_id_x(),heads=channels/32,windows=width/8*(height/8);if(!valid_attn(channels)||width%8||height%8||tile>=windows*heads*16)return;
 uint wh=tile/16,win=wh/heads,head=wh%heads,q=tile%16/4*16,col=tile%4*16,l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4;f8 sum{};
 for(uint half=0;half<2;half++){i2 a{},b{};for(uint e=0;e<8;e++){uint c=half*16+g*8+e,qp=pixel_in_window(win,q+rc,width),kp=pixel_in_window(win,col+rc,width);pack(a,e,normalized[(qp*3)*channels+head*32+c]);pack(b,e,normalized[(kp*3+1)*channels+head*32+c]);}sum=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,sum);}
 for(uint e=0;e<8;e++){uint qr=q+g*8+e,k=col+rc;float score=sum[e]+weights[4*channels*channels+head*4096+qr*64+k];float a=clampf(score*.044921875f+1.30078125f,1.03125f,1.5693359375f);uint h;asm("v_cvt_pkrtz_f16_f32 %0, %1, %1" : "=v"(h) : "v"(a));ex[(wh*64+qr)*64+k]=from_half((((h&65535u)<<5)+0x8000u)&65535u);}
}
// Each wave handles16 queries, computes the two production32-key partials
// with F16 WMMA against ones, then forms probabilities with unrounded inverse.
FASTWAVE void mh_probabilities_fast(const float* ex,float* prob,uint windows,uint channels){
 uint tile=__builtin_amdgcn_workgroup_id_x(),heads=channels/32;if(!valid_attn(channels)||tile>=windows*heads*4)return;uint wh=tile/4,q=tile%4*16,l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4;f8 totals[2];
 for(uint side=0;side<2;side++){f8 sum{};for(uint half=0;half<2;half++){h8 a{},b{};for(uint e=0;e<8;e++){uint k=side*16+half*32+g*8+e;a[e]=(_Float16)ex[(wh*64+q+rc)*64+k];b[e]=(_Float16)1.f;}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,sum);}totals[side]=sum;}
 for(uint e=0;e<8;e++){uint qr=q+g*8+e;float inv=1.f/(totals[0][e]+totals[1][e]);for(uint k=rc;k<64;k+=16)prob[(wh*64+qr)*64+k]=F(ex[(wh*64+qr)*64+k]*inv);}
}
FASTWAVE void mh_attention_av_fast(const float* prob,const float* normalized,float* av,uint width,uint height,uint channels){
 uint tile=__builtin_amdgcn_workgroup_id_x(),heads=channels/32,windows=width/8*(height/8);if(!valid_attn(channels)||width%8||height%8||tile>=windows*heads*8)return;
 uint wh=tile/8,win=wh/heads,head=wh%heads,q=tile%8/2*16,col=tile%2*16,l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4;f8 sum{};
 for(uint half=0;half<4;half++){i2 a{},b{};for(uint e=0;e<8;e++){uint k=half*16+g*8+e,kp=pixel_in_window(win,k,width);pack(a,e,prob[(wh*64+q+rc)*64+k]);pack(b,e,normalized[(kp*3+2)*channels+head*32+col+rc]);}sum=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,sum);}
 for(uint e=0;e<8;e++){uint p=pixel_in_window(win,q+g*8+e,width);av[p*channels+head*32+col+rc]=F(sum[e]);}
}
// Production pool projection is NOT the dense fast accumulation schedule.
// C>=64: F16 operands, independent K32 sum then software-H/RNE each group.
// C32: FP8 operands, a single K32 dot, explicit HLSL-half/RTZ epilogue.
FASTWAVE void mh_pool_project_production(const float* pooled,const float* weights,float* output,uint width,uint height,uint valid_width,uint valid_height,uint channels){
 uint M=width*height,N=2*channels,tile=__builtin_amdgcn_workgroup_id_x(),nt=N/16;
 if(!valid_spatial(channels)||tile>=((M+15)/16)*nt)return;uint first=tile/nt*16,col=tile%nt*16,l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4;f8 acc{};
 for(uint chunk=0;chunk<channels/32;chunk++){f8 sum{};
  for(uint half=0;half<2;half++){
   if(channels==32){i2 a{},b{};for(uint e=0;e<8;e++){uint k=half*16+g*8+e,p=first+rc;pack(a,e,p<M?pooled[p*channels+k]:0.f);pack(b,e,weights[(col+rc)*channels+k]);}sum=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,sum);}
   else{h8 a{},b{};for(uint e=0;e<8;e++){uint k=chunk*32+half*16+g*8+e,p=first+rc;a[e]=(_Float16)(p<M?pooled[p*channels+k]:0.f);b[e]=(_Float16)weights[(col+rc)*channels+k];}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,sum);}
  }
  for(uint e=0;e<8;e++)acc[e]=channels==32?Hrtz(acc[e]+sum[e]):H(acc[e]+sum[e]);
 }
 for(uint e=0;e<8;e++){uint p=first+g*8+e;if(p<M)output[p*N+col+rc]=(valid_width&&(p%width>=valid_width||p/width>=valid_height))?0.f:F(acc[e]);}
}
// Prepacked-weight twin (host option pool_project_h16w): weights arrive as RNE half rows (c>=64) or E4M3 bytes (c==32)
// exactly as the (_Float16)/pack() casts of the f32 kernel would produce them; one fragment load replaces eight casts.
FASTWAVE void mh_pool_project_production_h16w(const float* pooled,const float* weights,float* output,uint width,uint height,uint valid_width,uint valid_height,uint channels){
 uint M=width*height,N=2*channels,tile=__builtin_amdgcn_workgroup_id_x(),nt=N/16;
 if(!valid_spatial(channels)||tile>=((M+15)/16)*nt)return;uint first=tile/nt*16,col=tile%nt*16,l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4;f8 acc{};
 for(uint chunk=0;chunk<channels/32;chunk++){f8 sum{};
  for(uint half=0;half<2;half++){
   if(channels==32){i2 a{},b;for(uint e=0;e<8;e++){uint k=half*16+g*8+e,p=first+rc;pack(a,e,p<M?pooled[p*channels+k]:0.f);}__builtin_memcpy(&b,reinterpret_cast<const unsigned char*>(weights)+(col+rc)*32+half*16+g*8,8);sum=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,sum);}
   else{h8 a{},b;for(uint e=0;e<8;e++){uint k=chunk*32+half*16+g*8+e,p=first+rc;a[e]=(_Float16)(p<M?pooled[p*channels+k]:0.f);}__builtin_memcpy(&b,reinterpret_cast<const _Float16*>(weights)+(col+rc)*channels+chunk*32+half*16+g*8,16);sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,sum);}
  }
  for(uint e=0;e<8;e++)acc[e]=channels==32?Hrtz(acc[e]+sum[e]):H(acc[e]+sum[e]);
 }
 for(uint e=0;e<8;e++){uint p=first+g*8+e;if(p<M)output[p*N+col+rc]=(valid_width&&(p%width>=valid_width||p/width>=valid_height))?0.f:F(acc[e]);}
}

#ifndef HIP_POOL32_B8
#define HIP_POOL32_B8 0
#endif
#if HIP_POOL32_B8
// C32 twin of the h16w kernel whose pooled input is already E4M3 bytes (c32_wave1_finish_dcrop_b8d stores fp8(F(v))).
// F(v) is E4M3-exact and never -0, so the byte equals pack()'s q8 of the f32 value: identical A fragment, 1/4 of the reads.
FASTWAVE void mh_pool_project_c32_b8(const unsigned char* pooled,const float* weights,float* output,uint width,uint height,uint valid_width,uint valid_height,uint channels){
 uint M=width*height,N=64,tile=__builtin_amdgcn_workgroup_id_x(),nt=4;
 if(channels!=32||tile>=((M+15)/16)*nt)return;uint first=tile/nt*16,col=tile%nt*16,l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4;f8 acc{},sum{};
 for(uint half=0;half<2;half++){i2 a{},b;uint p=first+rc;if(p<M)__builtin_memcpy(&a,pooled+p*32+half*16+g*8,8);__builtin_memcpy(&b,reinterpret_cast<const unsigned char*>(weights)+(col+rc)*32+half*16+g*8,8);sum=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,sum);}
 for(uint e=0;e<8;e++)acc[e]=Hrtz(acc[e]+sum[e]);
 for(uint e=0;e<8;e++){uint p=first+g*8+e;if(p<M)output[p*N+col+rc]=(valid_width&&(p%width>=valid_width||p/width>=valid_height))?0.f:F(acc[e]);}
}

// Paired typed edge for block4 pool -> block5. Keep F and RTZ exactly;
// encode its result, including negative underflow zero, without changing the reduction.
FASTWAVE void mh_pool_project_c32_b8_out8(const unsigned char* pooled,const float* weights,unsigned char* output,uint width,uint height,uint valid_width,uint valid_height,uint channels){
 uint M=width*height,N=64,tile=__builtin_amdgcn_workgroup_id_x(),nt=4;
 if(channels!=32||tile>=((M+15)/16)*nt)return;uint first=tile/nt*16,col=tile%nt*16,l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4;f8 acc{},sum{};
 for(uint half=0;half<2;half++){i2 a{},b;uint p=first+rc;if(p<M)__builtin_memcpy(&a,pooled+p*32+half*16+g*8,8);__builtin_memcpy(&b,reinterpret_cast<const unsigned char*>(weights)+(col+rc)*32+half*16+g*8,8);sum=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,sum);}
 for(uint e=0;e<8;e++)acc[e]=Hrtz(acc[e]+sum[e]);
 for(uint e=0;e<8;e++){uint p=first+g*8+e;if(p<M)output[p*N+col+rc]=(valid_width&&(p%width>=valid_width||p/width>=valid_height))?0u:static_cast<unsigned char>(q8(F(acc[e])));}
}
#endif

// Fused 2x2 pool + channel-doubling projection (host option pool_project_fused, C=64/128/256): one wave produces 16
// pooled tokens x 64 output columns; the pooled A fragments are computed once from raw (same H/F chain as mh_pool) and
// reused across the four column tiles, weights arrive prepacked as F16 rows (one 16-byte load per B fragment). The
// per-32-chunk f32 sum, H(acc+sum) chaining and F(acc) store follow mh_pool_project_production exactly.
template<uint C>
DEV void mh_pool_project_fused_body(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){
 const uint M=width*height,N=2*C,ncol=N/64;uint tile=__builtin_amdgcn_workgroup_id_x();if(tile>=((M+15)/16)*ncol)return;
 uint first=tile/ncol*16,colg=tile%ncol*64,l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4;
 const _Float16*w16=reinterpret_cast<const _Float16*>(w);
 h8 a[C/16];uint p=first+rc;bool live=p<M&&!(valid_width&&(p%width>=valid_width||p/width>=valid_height));uint base=live?((p/width)*2*source_width+(p%width)*2)*C:0;
 for(uint sidx=0;sidx<C/16;sidx++)for(uint e=0;e<8;e++){uint c=sidx*16+g*8+e;float v=0.f;if(live){float top=H(raw[base+c]+raw[base+C+c]),bottom=H(raw[base+source_width*C+c]+raw[base+(source_width+1)*C+c]);v=F(H(H(top+bottom)*.25f));}a[sidx][e]=(_Float16)v;}
 f8 acc[4]{};
 for(uint chunk=0;chunk<C/32;chunk++){f8 sum[4]{};
  for(uint half=0;half<2;half++){uint sidx=chunk*2+half;for(uint j=0;j<4;j++){h8 b;__builtin_memcpy(&b,w16+(colg+j*16+rc)*C+sidx*16+g*8,16);sum[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a[sidx],b,sum[j]);}}
  for(uint j=0;j<4;j++)for(uint e=0;e<8;e++)acc[j][e]=H(acc[j][e]+sum[j][e]);
 }
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++){uint q=first+g*8+e;if(q<M)output[q*N+colg+j*16+rc]=(valid_width&&(q%width>=valid_width||q/width>=valid_height))?0.f:F(acc[j][e]);}
}
// Group-shared pool + projection (host option pool_project_group, C=64/128/256): one workgroup of 2C/64 waves owns 16
// pooled tokens; the pooling (same H/F chain as mh_pool) is done once per group into LDS f16 rows (exact: values are
// F()-quantized), then each wave produces its 64 output columns with A fragments from LDS and B fragments from the
// prepacked 16x16 f16 tile layout (PackedDsWeightFrag: one contiguous 512-byte load per fragment). Same per-32-chunk
// sum, H(acc+sum) chaining and F(acc) store as mh_pool_project_production.
// Gout (head Down C512 only, results/gap-fusion-20260930): store straight into the ViT gather order (host Gather(...,false):
// out[t][c] = in[raster(t)][ch(c)]), so the separate vit_gather launch disappears; values unchanged, only addresses.
DEV uint pg_tinv(uint r){return (r&~15u)|((r&7u)<<1)|((r>>3)&1u);}
DEV uint pg_cinv(uint k){return (k&~31u)|((k>>1)&1u)|((k&1u)<<1)|((k>>4)&1u)<<2|((k>>2)&1u)<<3|((k>>3)&1u)<<4;}
template<uint C,bool Gout=false,bool HalfIn=false,bool ByteOut=false,bool F8W16=false>
DEV void mh_pool_project_group_body(const float*raw,const float*wf,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){
 const uint M=width*height,N=2*C,S=C+8;uint tile=__builtin_amdgcn_workgroup_id_x();if(tile>=(M+15)/16)return;
 uint first=tile*16,t=__builtin_amdgcn_workitem_id_x(),wave=t/32,l=t%32,rc=l&15,g=l>>4,colg=wave*64;
 __attribute__((shared)) unsigned char pooled8[F8W16?16*S:1];
 __attribute__((shared)) _Float16 pooled[F8W16?1:16*S];
 for(uint k=0;k<16;k++){uint p=first+k,c=t;float v=0.f;if(p<M&&!(valid_width&&(p%width>=valid_width||p/width>=valid_height))){uint base=((p/width)*2*source_width+(p%width)*2)*C;auto rd=[&](uint i)->float{if constexpr(HalfIn)return float(reinterpret_cast<const _Float16*>(raw)[i]);else return raw[i];};float top=H(rd(base+c)+rd(base+C+c)),bottom=H(rd(base+source_width*C+c)+rd(base+(source_width+1)*C+c));v=F(H(H(top+bottom)*.25f));}if constexpr(F8W16)pooled8[k*S+c]=static_cast<unsigned char>(__builtin_amdgcn_cvt_pk_fp8_f32(v,0.f,0,false));else pooled[k*S+c]=(_Float16)v;}
 WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);
 const _Float16*w16=reinterpret_cast<const _Float16*>(wf);
 f8 acc[4]{};
 for(uint chunk=0;chunk<C/32;chunk++){f8 sum[4]{};
  if constexpr(F8W16){
   using pool_i4=int __attribute__((ext_vector_type(4)));
   i2 a0,a1;__builtin_memcpy(&a0,pooled8+rc*S+chunk*32+g*8,8);__builtin_memcpy(&a1,pooled8+rc*S+chunk*32+16+g*8,8);
   for(uint j=0;j<4;j++){pool_i4 b;__builtin_memcpy(&b,reinterpret_cast<const unsigned char*>(wf)+(((colg/16+j)*(C/32)+chunk)*512)+(g*16+rc)*16,16);
    sum[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a0,i2{b[0],b[1]},sum[j]);
    sum[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a1,i2{b[2],b[3]},sum[j]);
   }
  }else{
  for(uint half=0;half<2;half++){uint sidx=chunk*2+half;h8 a;__builtin_memcpy(&a,pooled+rc*S+sidx*16+g*8,16);
   for(uint j=0;j<4;j++){h8 b;uint nt=colg/16+j;__builtin_memcpy(&b,w16+(nt*(C/16)+sidx)*256+(g*16+rc)*8,16);sum[j]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a,b,sum[j]);}}
  }
  for(uint j=0;j<4;j++)for(uint e=0;e<8;e++)acc[j][e]=H(acc[j][e]+sum[j][e]);
 }
 _Pragma("unroll") for(uint e=0;e<8;e++){uint q=first+g*8+e;if(q<M){bool inv=valid_width&&(q%width>=valid_width||q/width>=valid_height);_Pragma("unroll") for(uint j=0;j<4;j++){float a=acc[j][e],ac=__builtin_amdgcn_fmed3f(a+0.f,-448.f,448.f);int b8=__builtin_amdgcn_cvt_pk_fp8_f32(ac,0.f,0,false);uint idx=Gout?pg_tinv(q)*N+pg_cinv(colg+j*16+rc):q*N+colg+j*16+rc;if constexpr(ByteOut)reinterpret_cast<unsigned char*>(output)[idx]=inv?0u:static_cast<unsigned char>(b8);else output[idx]=inv?0.f:__builtin_amdgcn_cvt_f32_fp8(b8,0);}}}
}
KERNEL __attribute__((amdgpu_flat_work_group_size(64,64))) void mh_pool_project_group_c64(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<64>(raw,w,output,width,height,source_width,valid_width,valid_height);}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_pool_project_group_c128(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<128>(raw,w,output,width,height,source_width,valid_width,valid_height);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_pool_project_group_c256(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<256>(raw,w,output,width,height,source_width,valid_width,valid_height);}
KERNEL __attribute__((amdgpu_flat_work_group_size(64,64))) void mh_pool_project_group_c64_out8(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<64,false,false,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_pool_project_group_c128_out8(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<128,false,false,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_pool_project_group_c256_out8(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<256,false,false,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}
/* MH_POOL_HALF_IN (2026-10-01, swin-body-gap): the encoder tail stored as f16 (Hrtz, f16-exact); read back as f16 -> same float. New exports only. */
#ifndef MH_POOL_HALF_IN
#define MH_POOL_HALF_IN 0
#endif
#if MH_POOL_HALF_IN
KERNEL __attribute__((amdgpu_flat_work_group_size(64,64))) void mh_pool_project_group_c64_hin(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<64,false,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_pool_project_group_c128_hin(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<128,false,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}
KERNEL __attribute__((amdgpu_flat_work_group_size(64,64))) void mh_pool_project_group_c64_hin_out8(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<64,false,true,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_pool_project_group_c128_hin_out8(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<128,false,true,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}
#endif
FASTWAVE void mh_pool_project_fused_c64(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_fused_body<64>(raw,w,output,width,height,source_width,valid_width,valid_height);}
FASTWAVE void mh_pool_project_fused_c128(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_fused_body<128>(raw,w,output,width,height,source_width,valid_width,valid_height);}
FASTWAVE void mh_pool_project_fused_c256(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_fused_body<256>(raw,w,output,width,height,source_width,valid_width,valid_height);}

// C512 QKV+normalize without the 64x64 LDS-staged tile (host option qkv_norm_wave_c512): a 128-thread group packs the
// 16-token A rows (pack4, as fast_dense) into LDS once, then each of its four waves owns 64 output columns (two heads)
// and runs the K=512 FP8 MMA chain straight from LDS/A and the prepacked [N][K] weight bytes/B. Row sums stay the
// sequential j=0..31 order over a per-wave LDS tile; the store is q8(F(acc*inv)) exactly as fast_dense<Normalize>.
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128)))
void mh_qkv_normalize_wave_c512(const float*in,const float*w,float*out,uint tokens){
 const uint C=512,N=1536,K=512;uint t=__builtin_amdgcn_workitem_id_x(),wave=t/32,l=t%32,rc=l&15,g=l>>4;
 uint block=__builtin_amdgcn_workgroup_id_x(),first=block/6*16,colbase=block%6*256;if(first>=tokens)return;
 union Shared{uint a[16*130];float raw[4*17*65];};__attribute__((shared)) Shared sh;
 for(uint i=t;i<16*128;i+=128){uint row=i/128,word=i%128,p=first+row,k=word*4;uint v=0;if(p<tokens){uint off=p*K+k;v=pack4(in[off],in[off+1],in[off+2],in[off+3]);}sh.a[row*130+word]=v;}
 __builtin_amdgcn_s_barrier();
 const uint*pw=reinterpret_cast<const uint*>(w);uint col=colbase+wave*64;f8 acc[4]{};
 for(uint k=0;k<K;k+=16){uint word0=(k+g*8)/4;i2 a={int(sh.a[rc*130+word0]),int(sh.a[rc*130+word0+1])};
  for(uint j=0;j<4;j++){uint n=col+j*16+rc,off=(n*K+k+g*8)/4;i2 b={int(pw[off]),int(pw[off+1])};acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,acc[j]);}
 }
 __builtin_amdgcn_s_barrier();
 float*raw=sh.raw+wave*17*65;
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++)raw[(g*8+e)*65+j*16+rc]=acc[j][e];
 __builtin_amdgcn_s_barrier();
 uint part=col/C,head0=(col%C)/32,scale=4*C*C+(C/32)*4096;
 float inv=1.f;{uint row=l%16,h=l/16;if(first+row<tokens&&part<2){float ss=0.f;for(uint j=0;j<32;j++){float v=raw[row*65+h*32+j];ss+=v*v;}inv=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?w[scale+head0+h]:1.f);}}
 // inv lives in lane (h*16+row); the store needs inv[row=g*8+e][h=j/2]: read it back through the same LDS tile row 16..
 __builtin_amdgcn_s_barrier();
 raw[16*65+l]=inv;
 __builtin_amdgcn_s_barrier();
 for(uint j=0;j<4;j++)for(uint e=0;e<8;e++){uint row=g*8+e,p=first+row,n=col+j*16+rc;if(p<tokens)reinterpret_cast<unsigned char*>(out)[p*N+n]=static_cast<unsigned char>(q8(F(acc[j][e]*raw[16*65+(j/2)*16+row])));}
}

// C512 QKV+normalize in the HLSL native_wave_qkv_normalize shape (host option c512_qkv_frag): one wave = 16 tokens x
// 64 output columns (two heads of one part); A comes straight from the projection's 512-byte [16 token][32 k] E4M3
// tiles, B from fragment-tiled E4M3 QKV weights (FragmentPackedMatrix: 512-byte tile per (column tile, k32), 8
// contiguous bytes per lane) -- no LDS and no barrier in the K loop. Row sums keep the sequential j=0..31 order in a
// wave-private LDS tile; the store is q8(F(acc*inv)) exactly as fast_dense<Normalize>.
FASTWAVE void mh_qkv_normalize_frag_c512(const unsigned char*in8,const float*w,unsigned char*out,uint tokens){
 const uint C=512,N=1536;uint l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4,block=__builtin_amdgcn_workgroup_id_x(),first=block/24*16,col=block%24*64;if(first>=tokens)return;
 const unsigned char*wb=reinterpret_cast<const unsigned char*>(w);f8 acc[4]{};
 for(uint gs=0;gs<16;gs++){const unsigned char*atile=in8+((first/16)*16+gs)*512+rc*32+g*8;
  for(uint h=0;h<2;h++){i2 a;__builtin_memcpy(&a,atile+h*16,8);
   for(uint n=0;n<4;n++){uint n0=col+n*16+rc;i2 b;__builtin_memcpy(&b,wb+((n0/16)*16+gs)*512+((h*2+g)*16+n0%16)*8,8);acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,acc[n]);}}}
 __attribute__((shared)) float raw[17*65];
 for(uint n=0;n<4;n++)for(uint e=0;e<8;e++)raw[(g*8+e)*65+n*16+rc]=acc[n][e];
 WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);
 uint part=col/C,head0=(col%C)/32,scale=4*C*C+(C/32)*4096;
 {uint row=l%16,h=l/16;float inv=1.f;if(first+row<tokens&&part<2){float ss=0.f;for(uint j=0;j<32;j++){float v=raw[row*65+h*32+j];ss+=v*v;}inv=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?w[scale+head0+h]:1.f);}raw[16*65+l]=inv;}
 WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);
 for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint row=g*8+e,p=first+row,c=col+n*16+rc;if(p<tokens)out[p*N+c]=static_cast<unsigned char>(q8(F(acc[n][e]*raw[16*65+(n/2)*16+row])));}
}
#ifndef C512_PROJ_DEEP
#define C512_PROJ_DEEP 0 /* 2026-10-01 gap-map-evening: mh_attention_project_frag_c512 K loop unrolled with a D-deep operand ring over the 32 (k32,half) steps, D=2..8; 0 = loop */
#endif
#ifndef C512_PROJ_ABLATE
#define C512_PROJ_ABLATE 0 /* 2026-10-01 night (diagnostic only, output WRONG when !=0): mh_attention_project_frag_c512 default path, bit 1 = residual init 0
   (no feature/scale reads), bit 2 = no K loop (no AV/weight loads, no WMMA), bit 4 = output store only when post==99 (never). results/night-20261001 */
#endif

// C512 attention projection in the same shape (host option c512_proj_frag): one wave = 16 tokens x 64 output columns,
// A = the attention's E4M3 AV bytes (8 contiguous bytes per lane per K16), B = fragment-tiled E4M3 projection weights
// (PackedMhWeightQkvFrag, region after the QKV rows), no LDS. Residual, epilogue (post 0 -> F(Hrtz), 3 -> Hrtz, 4 -> F)
// and the crop mapping follow fast_dense<ByteInput,Crop> with matrix_residual=0 exactly.
FASTWAVE void mh_attention_project_frag_c512(const unsigned char*av8,const float*feature,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){
#if C512_PROJ_M32
 // M32 weight sharing (default 0): same host grid (tokens/16*8 waves) and ABI; wave b handles token tiles 2*(b/8) and
 // 2*(b/8)+1, so the upper half of the grid returns at once. Each K16 weight fragment feeds both tiles' WMMAs; every
 // accumulator keeps the old init (Hrtz(feature*scale), scale read once per column), K order, operands and epilogue.
 const uint C=512;uint l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4,block=__builtin_amdgcn_workgroup_id_x(),first=block/8*32,col=block%8*64;if(first>=tokens)return;
 const unsigned char*wb=reinterpret_cast<const unsigned char*>(w)+size_t(3)*C*C*4;const uint skip=4*C*C+(C/32)*4096+C/32;
 f8 acc[2][4];
 for(uint n=0;n<4;n++){uint c=col+n*16+rc;float scale=w[skip+c];
  for(uint m=0;m<2;m++)for(uint e=0;e<8;e++){uint p=first+m*16+g*8+e;acc[m][n][e]=p<tokens?Hrtz(feature[p*C+c]*scale):0.f;}}
 bool live0=first+rc<tokens,live1=first+16+rc<tokens;const unsigned char*arow0=av8+(first+rc)*C+g*8,*arow1=arow0+16*C;
 for(uint k=0;k<C;k+=32){uint gs=k/32;
  for(uint h=0;h<2;h++){i2 a0{},a1{};if(live0)__builtin_memcpy(&a0,arow0+k+h*16,8);if(live1)__builtin_memcpy(&a1,arow1+k+h*16,8);
   for(uint n=0;n<4;n++){uint n0=col+n*16+rc;i2 b;__builtin_memcpy(&b,wb+((n0/16)*16+gs)*512+((h*2+g)*16+n0%16)*8,8);
    acc[0][n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a0,b,acc[0][n]);acc[1][n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a1,b,acc[1][n]);}}}
 for(uint m=0;m<2;m++)for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint p=first+m*16+g*8+e,c=col+n*16+rc;if(p>=tokens)continue;float a=acc[m][n][e],v=post==3?Hrtz(a):post==4?F(a):F(Hrtz(a));uint dest=p*C+c;
  if(cropw){uint x=p%workw,y=p/workw;if(x<sx||y<sy||x>=sx+cropw||y>=sy+croph)continue;dest=((y-sy)*cropw+x-sx)*C+c;}out[dest]=v;}
#else
 const uint C=512;uint l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4,block=__builtin_amdgcn_workgroup_id_x(),first=block/8*16,col=block%8*64;if(first>=tokens)return;
 const unsigned char*wb=reinterpret_cast<const unsigned char*>(w)+size_t(3)*C*C*4;const uint skip=4*C*C+(C/32)*4096+C/32;
 f8 acc[4];
#if C512_PROJ_ABLATE&1
 for(uint n=0;n<4;n++)for(uint e=0;e<8;e++)acc[n][e]=0.f;
#elif HIP_C512_HOIST_RES
 // Residual init with the loads outside the per-element condition: the 4 column scales once per lane, the 32 feature
 // values with a clamped row (row `first` is always < tokens), then the select. ISA of the conditional form: every
 // element re-read w[skip+c] and waited (32 global_load + s_wait_loadcnt 0 pairs per wave).
 {float ws[4];for(uint n=0;n<4;n++)ws[n]=w[skip+col+n*16+rc];
  float fv[4][8];for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint p=first+g*8+e;uint pp=p<tokens?p:first;fv[n][e]=feature[pp*C+col+n*16+rc];}
  for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint p=first+g*8+e;acc[n][e]=p<tokens?Hrtz(fv[n][e]*ws[n]):0.f;}}
#else
 for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint p=first+g*8+e,c=col+n*16+rc;acc[n][e]=p<tokens?Hrtz(feature[p*C+c]*w[skip+c]):0.f;}
#endif
 bool live=first+rc<tokens;
#if C512_PROJ_DEEP
 // C512_PROJ_DEEP=D (gap-map-evening-20261001), same operands and per-accumulator order as the loop. Mirrors C512_COMPACT_QKV_DEEP + SCHED: 32 (gs,h) steps unrolled, a C512_PROJ_DEEP-deep register ring, every
 // address = one of 1+4 base pointers + an immediate, dead rows read row `first` and are zeroed at the consuming step,
 // sched_barrier around each step's WMMAs. NOT TAKEN: LLVM still hoists the B loads and chains acc[0] over all 32 steps,
 // 255 VGPR + scratch spills; first ring form (d4) 19 SAME but +0.22ms both tiers (results/gap-map-evening-20261001).
 {constexpr uint D=C512_PROJ_DEEP;const unsigned char*ap=av8+(live?first+rc:first)*C+g*8;const unsigned char*bp[4];
  _Pragma("unroll") for(uint n=0;n<4;n++){uint n0=col+n*16+rc;bp[n]=wb+(n0/16)*16*512+(g*16+n0%16)*8;}
  i2 ra[D],rb[D][4];
  auto ld=[&](uint st){const uint gs=st/2,h=st%2,k=st%D;__builtin_memcpy(&ra[k],ap+gs*32+h*16,8);
   _Pragma("unroll") for(uint n=0;n<4;n++)__builtin_memcpy(&rb[k][n],bp[n]+gs*512+h*256,8);};
  _Pragma("unroll") for(uint st=0;st<D;st++)ld(st);
  _Pragma("unroll") for(uint st=0;st<32;st++){
   const uint k=st%D;i2 ca=live?ra[k]:i2{},cb[4];
   _Pragma("unroll") for(uint n=0;n<4;n++)cb[n]=rb[k][n];
   if(st+D<32)ld(st+D);
   __asm__ volatile("" ::: "memory");__builtin_amdgcn_sched_barrier(0);
   _Pragma("unroll") for(uint n=0;n<4;n++)acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(ca,cb[n],acc[n]);
   __builtin_amdgcn_sched_barrier(0);
  }}
#elif C512_PROJ_ABLATE&2
#else
 const unsigned char*arow=av8+(first+rc)*C+g*8;
 for(uint k=0;k<C;k+=32){uint gs=k/32;
  for(uint h=0;h<2;h++){i2 a{};if(live)__builtin_memcpy(&a,arow+k+h*16,8);
   for(uint n=0;n<4;n++){uint n0=col+n*16+rc;i2 b;__builtin_memcpy(&b,wb+((n0/16)*16+gs)*512+((h*2+g)*16+n0%16)*8,8);acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,acc[n]);}}}
#endif
 for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint p=first+g*8+e,c=col+n*16+rc;if(p>=tokens)continue;float a=acc[n][e],v=post==3?Hrtz(a):post==4?F(a):F(Hrtz(a));uint dest=p*C+c;
  if(cropw){uint x=p%workw,y=p/workw;if(x<sx||y<sy||x>=sx+cropw||y>=sy+croph)continue;dest=((y-sy)*cropw+x-sx)*C+c;}
#if C512_PROJ_ABLATE&4
  if(post==99)
#endif
  out[dest]=v;}
#endif
}

template<uint Unroll,bool ByteOut=false,bool FB8=false>
DEV void mh_attention_project_frag_c512_w16_body(const unsigned char*av8,const float*feature,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){
 const uint C=512;uint l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4,block=__builtin_amdgcn_workgroup_id_x(),first=block/8*16,col=block%8*64;if(first>=tokens)return;
 const unsigned char*wb=reinterpret_cast<const unsigned char*>(w)+size_t(3)*C*C*4;const uint skip=4*C*C+(C/32)*4096+C/32;
 typedef int w16_i4 __attribute__((ext_vector_type(4)));
 f8 acc[4];
 {float ws[4];for(uint n=0;n<4;n++)ws[n]=w[skip+col+n*16+rc];
  float fv[4][8];
  if constexpr(FB8){
   const unsigned char*feat8=reinterpret_cast<const unsigned char*>(feature)+((first/16)*16+col/32)*512+rc;
   uint roff=g*256;
   for(uint e=0;e<8;e++){uint p=first+g*8+e;uint re=p<tokens?(roff+e*32):0u;
    for(uint n=0;n<4;n++)fv[n][e]=__builtin_amdgcn_cvt_f32_fp8(int(feat8[re+(n>>1)*512+(n&1)*16]),0);}
  }else{
   for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint p=first+g*8+e;uint pp=p<tokens?p:first;fv[n][e]=feature[pp*C+col+n*16+rc];}
  }
  for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint p=first+g*8+e;float h=Hrtz(fv[n][e]*ws[n]);__asm__ volatile("":"+v"(h));acc[n][e]=p<tokens?h:0.f;}}
 const bool live=first+rc<tokens;
 const unsigned char*arow=av8+(live?first+rc:first)*C+g*8;
 if constexpr(Unroll==4){
  _Pragma("unroll 4") for(uint k=0;k<C;k+=32){uint gs=k/32;
   i2 a0,a1;__builtin_memcpy(&a0,arow+k,8);__builtin_memcpy(&a1,arow+k+16,8);
   a0=live?a0:i2{};a1=live?a1:i2{};
   _Pragma("unroll") for(uint n=0;n<4;n++){uint n0=col+n*16+rc;w16_i4 bw;__builtin_memcpy(&bw,wb+((n0/16)*16+gs)*512+(g*16+n0%16)*16,16);
    i2 b0={bw[0],bw[1]},b1={bw[2],bw[3]};
    acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a0,b0,acc[n]);
    acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a1,b1,acc[n]);}}
 }else{
  _Pragma("unroll 2") for(uint k=0;k<C;k+=32){uint gs=k/32;
   i2 a0,a1;__builtin_memcpy(&a0,arow+k,8);__builtin_memcpy(&a1,arow+k+16,8);
   a0=live?a0:i2{};a1=live?a1:i2{};
   _Pragma("unroll") for(uint n=0;n<4;n++){uint n0=col+n*16+rc;w16_i4 bw;__builtin_memcpy(&bw,wb+((n0/16)*16+gs)*512+(g*16+n0%16)*16,16);
    i2 b0={bw[0],bw[1]},b1={bw[2],bw[3]};
    acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a0,b0,acc[n]);
    acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a1,b1,acc[n]);}}
 }
 uint l_tail=__builtin_amdgcn_workitem_id_x();__asm__ volatile("":"+v"(l_tail));
 uint rc_t=l_tail&15u,g_t=l_tail>>4;
 const uint valid_tokens=(cropw&&!sx&&!sy&&cropw==workw)?croph*workw:tokens;
 _Pragma("unroll") for(uint e=0;e<8;e++){uint p=first+g_t*8+e;if(p<valid_tokens){
  _Pragma("unroll") for(uint n=0;n<4;n++){uint c=col+n*16+rc_t;float a=acc[n][e],h=Hrtz(a),hi=post==4?a:h,hc=__builtin_amdgcn_fmed3f(hi+0.f,-448.f,448.f);int b8=__builtin_amdgcn_cvt_pk_fp8_f32(hc,0.f,0,false);if constexpr(ByteOut)reinterpret_cast<unsigned char*>(out)[p*C+c]=static_cast<unsigned char>(b8);else{float f=__builtin_amdgcn_cvt_f32_fp8(b8,0),v=post==3?h:f;out[p*C+c]=v;}}}}
}
FASTWAVE void mh_attention_project_frag_c512_w16(const unsigned char*av8,const float*feature,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){mh_attention_project_frag_c512_w16_body<2>(av8,feature,w,out,tokens,post,cropw,croph,workw,sx,sy);}
FASTWAVE void mh_attention_project_frag_c512_w16_u4(const unsigned char*av8,const float*feature,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){mh_attention_project_frag_c512_w16_body<4>(av8,feature,w,out,tokens,post,cropw,croph,workw,sx,sy);}
FASTWAVE void mh_attention_project_frag_c512_w16_bout(const unsigned char*av8,const float*feature,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){mh_attention_project_frag_c512_w16_body<2,true>(av8,feature,w,out,tokens,post,cropw,croph,workw,sx,sy);}
FASTWAVE void mh_attention_project_frag_c512_w16_u4_bout(const unsigned char*av8,const float*feature,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){mh_attention_project_frag_c512_w16_body<4,true>(av8,feature,w,out,tokens,post,cropw,croph,workw,sx,sy);}
FASTWAVE void mh_attention_project_frag_c512_w16_u4_fb8(const unsigned char*av8,const float*feature,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){mh_attention_project_frag_c512_w16_body<4,false,true>(av8,feature,w,out,tokens,post,cropw,croph,workw,sx,sy);}
FASTWAVE void mh_attention_project_frag_c512_w16_u4_fb8_bout(const unsigned char*av8,const float*feature,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){mh_attention_project_frag_c512_w16_body<4,true,true>(av8,feature,w,out,tokens,post,cropw,croph,workw,sx,sy);}

// C512_PROJ_WN4 (2026-10-01, results/gap-map-evening-20261001): mh_attention_project_frag_c512 reorganised like mochizuki's
// Proj (WN=4): a 128-thread workgroup = the same 16 tokens x 64 columns, each wave one 16-column slice (one accumulator,
// 32 WMMA in the same (k32, half) order). Same grid as the 1-wave form, so 4x the waves and a 4x shorter serial chain; the
// four waves read the same A bytes (L0-shared) and disjoint weight fragments. Init, operands, K order and epilogue per
// output element are those of the HIP_C512_HOIST_RES path. 0 = no export (host falls back to the 1-wave kernel).
#ifndef C512_PROJ_WN4
#define C512_PROJ_WN4 0
#endif
#if C512_PROJ_WN4
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_attention_project_frag_c512_wn4(const unsigned char*av8,const float*feature,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){
 const uint C=512;uint t=__builtin_amdgcn_workitem_id_x(),l=t&31,n=t>>5,rc=l&15,g=l>>4,block=__builtin_amdgcn_workgroup_id_x(),first=block/8*16,col=block%8*64+n*16;if(first>=tokens)return;
 const unsigned char*wb=reinterpret_cast<const unsigned char*>(w)+size_t(3)*C*C*4;const uint skip=4*C*C+(C/32)*4096+C/32;
 f8 acc;
 {float ws=w[skip+col+rc];float fv[8];for(uint e=0;e<8;e++){uint p=first+g*8+e;uint pp=p<tokens?p:first;fv[e]=feature[pp*C+col+rc];}
  for(uint e=0;e<8;e++){uint p=first+g*8+e;acc[e]=p<tokens?Hrtz(fv[e]*ws):0.f;}}
 const bool live=first+rc<tokens;const unsigned char*ap=av8+(live?first+rc:first)*C+g*8;
 const uint n0=col+rc;const unsigned char*bp=wb+(n0/16)*16*512+(g*16+n0%16)*8;
#if C512_PROJ_WN4>=2
 _Pragma("unroll")
#else
 _Pragma("unroll 4")
#endif
 for(uint st=0;st<32;st++){const uint gs=st/2,h=st%2;i2 a,b;__builtin_memcpy(&a,ap+gs*32+h*16,8);__builtin_memcpy(&b,bp+gs*512+h*256,8);if(!live)a=i2{};acc=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,acc);}
 for(uint e=0;e<8;e++){uint p=first+g*8+e,c=col+rc;if(p>=tokens)continue;float a=acc[e],v=post==3?Hrtz(a):post==4?F(a):F(Hrtz(a));uint dest=p*C+c;
  if(cropw){uint x=p%workw,y=p/workw;if(x<sx||y<sy||x>=sx+cropw||y>=sy+croph)continue;dest=((y-sy)*cropw+x-sx)*C+c;}out[dest]=v;}
}
#endif

// C512_PROJ_FB8 (2026-10-01, results/mochizuki-gap-20261001): mh_attention_project_frag_c512 (HIP_C512_HOIST_RES form)
// with the residual feature read from split_projection_frag's E4M3 tiles instead of its f32 copy.
// C512_PROJ_FB8 2 (results/c512-qkv-pipeline-20261001): residual scale and output as 16-byte vector accesses. That f32 value is
// F(H(acc)), an E4M3 value (+-0 included), so decode(byte) == the float bit for bit. 0 = no export.
#ifndef C512_PROJ_FB8
#define C512_PROJ_FB8 0
#endif
#if C512_PROJ_FB8
FASTWAVE void mh_attention_project_frag_c512_fb8(const unsigned char*av8,const unsigned char*feat8,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){
 // Transposed WMMA (weights as A): lane = token, the 8 accumulator elements = 8 consecutive columns, so the residual is
 // one 8-byte load from the E4M3 tile and the result 8 consecutive floats. Same products and K order per output.
 const uint C=512;uint l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4,block=__builtin_amdgcn_workgroup_id_x(),first=block/8*16,col=block%8*64;if(first>=tokens)return;
 const unsigned char*wb=reinterpret_cast<const unsigned char*>(w)+size_t(3)*C*C*4;const uint skip=4*C*C+(C/32)*4096+C/32;
 const uint p=first+rc;const bool live=p<tokens;const uint pp=live?p:first;
 f8 acc[4];
 for(uint n=0;n<4;n++){uint c0=col+n*16+g*8;i2 fb;__builtin_memcpy(&fb,feat8+((pp/16)*16+c0/32)*512+(pp%16)*32+c0%32,8);
#if C512_PROJ_FB8>=2
  float wv[8];__builtin_memcpy(wv,__builtin_assume_aligned(w+skip+c0,16),32);
  for(uint e=0;e<8;e++){float fv=__builtin_amdgcn_cvt_f32_fp8(int((uint(fb[e/4])>>(8*(e%4)))&255u),0);acc[n][e]=live?Hrtz(fv*wv[e]):0.f;}}
#else
  for(uint e=0;e<8;e++){float fv=__builtin_amdgcn_cvt_f32_fp8(int((uint(fb[e/4])>>(8*(e%4)))&255u),0);acc[n][e]=live?Hrtz(fv*w[skip+c0+e]):0.f;}}
#endif
 const unsigned char*arow=av8+(first+rc)*C+g*8;
 for(uint k=0;k<C;k+=32){uint gs=k/32;
  for(uint h=0;h<2;h++){i2 a{};if(live)__builtin_memcpy(&a,arow+k+h*16,8);
   for(uint n=0;n<4;n++){uint n0=col+n*16+rc;i2 b;__builtin_memcpy(&b,wb+((n0/16)*16+gs)*512+((h*2+g)*16+n0%16)*8,8);acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,acc[n]);}}}
 if(!live)return;
 for(uint n=0;n<4;n++){uint c0=col+n*16+g*8;uint dest=p*C+c0;
  if(cropw){uint x=p%workw,y=p/workw;if(x<sx||y<sy||x>=sx+cropw||y>=sy+croph)continue;dest=((y-sy)*cropw+x-sx)*C+c0;}
#if C512_PROJ_FB8>=2
  // v3: 8 consecutive floats as two 16-byte stores (dest is a multiple of 8 floats) instead of 8 dword stores.
  float v[8];for(uint e=0;e<8;e++){float a=acc[n][e];v[e]=post==3?Hrtz(a):post==4?F(a):F(Hrtz(a));}
  __builtin_memcpy(__builtin_assume_aligned(out+dest,16),v,32);}
#else
  for(uint e=0;e<8;e++){float a=acc[n][e];out[dest+e]=post==3?Hrtz(a):post==4?F(a):F(Hrtz(a));}}
#endif
}
#endif

#ifndef C512_SPLIT_N
#define C512_SPLIT_N 0 /* 1: add exports mh_attention_project_frag_c512_n2 (block 64, same grid) and _n2g (block 32, grid x2): each wave 16 tokens x 32 columns, per-output init/K order/epilogue as the default path. results/fill-cu-20260930. */
#endif

// C512_FAST_PROJ (2026-10-03, fast-vit-c512): mh_attention_project_frag_c512_f16res = the production projection (the
// HIP_C512_HOIST_RES 1-wave form: 16 tokens x 64 columns, same operands/K order/epilogue) with the residual read as RNE
// halves from split_projection_frag_f16res instead of the f32 tensor. split_projection_frag's f32 value is F(H(acc)),
// an E4M3 value, so the half is exact: the init products are bit-identical; only the residual traffic halves. Lossless.
#ifndef C512_FAST_PROJ
#define C512_FAST_PROJ 0
#endif
#if C512_FAST_PROJ
FASTWAVE void mh_attention_project_frag_c512_f16res(const unsigned char*av8,const _Float16*feature16,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){
 const uint C=512;uint l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4,block=__builtin_amdgcn_workgroup_id_x(),first=block/8*16,col=block%8*64;if(first>=tokens)return;
 const unsigned char*wb=reinterpret_cast<const unsigned char*>(w)+size_t(3)*C*C*4;const uint skip=4*C*C+(C/32)*4096+C/32;
 f8 acc[4];
 {float ws[4];for(uint n=0;n<4;n++)ws[n]=w[skip+col+n*16+rc];
  float fv[4][8];for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint p=first+g*8+e;uint pp=p<tokens?p:first;fv[n][e]=feature16[pp*C+col+n*16+rc];}
  for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint p=first+g*8+e;acc[n][e]=p<tokens?Hrtz(fv[n][e]*ws[n]):0.f;}}
 bool live=first+rc<tokens;
 const unsigned char*arow=av8+(first+rc)*C+g*8;
 for(uint k=0;k<C;k+=32){uint gs=k/32;
  for(uint h=0;h<2;h++){i2 a{};if(live)__builtin_memcpy(&a,arow+k+h*16,8);
   for(uint n=0;n<4;n++){uint n0=col+n*16+rc;i2 b;__builtin_memcpy(&b,wb+((n0/16)*16+gs)*512+((h*2+g)*16+n0%16)*8,8);acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,acc[n]);}}}
 for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint p=first+g*8+e,c=col+n*16+rc;if(p>=tokens)continue;float a=acc[n][e],v=post==3?Hrtz(a):post==4?F(a):F(Hrtz(a));uint dest=p*C+c;
  if(cropw){uint x=p%workw,y=p/workw;if(x<sx||y<sy||x>=sx+cropw||y>=sy+croph)continue;dest=((y-sy)*cropw+x-sx)*C+c;}
  out[dest]=v;}
}
#endif

#if C512_SPLIT_N
template<bool WG64> DEV void c512_proj_n2_body(const unsigned char*av8,const float*feature,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){
 const uint C=512;uint t=__builtin_amdgcn_workitem_id_x(),l=t&31,rc=l&15,g=l>>4,half=WG64?(t>>5):(__builtin_amdgcn_workgroup_id_x()&1u),block=WG64?__builtin_amdgcn_workgroup_id_x():(__builtin_amdgcn_workgroup_id_x()>>1),first=block/8*16,col=block%8*64+half*32;if(first>=tokens)return;
 const unsigned char*wb=reinterpret_cast<const unsigned char*>(w)+size_t(3)*C*C*4;const uint skip=4*C*C+(C/32)*4096+C/32;
 f8 acc[2];
 for(uint n=0;n<2;n++)for(uint e=0;e<8;e++){uint p=first+g*8+e,c=col+n*16+rc;acc[n][e]=p<tokens?Hrtz(feature[p*C+c]*w[skip+c]):0.f;}
 bool live=first+rc<tokens;const unsigned char*arow=av8+(first+rc)*C+g*8;
 for(uint k=0;k<C;k+=32){uint gs=k/32;
  for(uint h=0;h<2;h++){i2 a{};if(live)__builtin_memcpy(&a,arow+k+h*16,8);
   for(uint n=0;n<2;n++){uint n0=col+n*16+rc;i2 b;__builtin_memcpy(&b,wb+((n0/16)*16+gs)*512+((h*2+g)*16+n0%16)*8,8);acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,acc[n]);}}}
 for(uint n=0;n<2;n++)for(uint e=0;e<8;e++){uint p=first+g*8+e,c=col+n*16+rc;if(p>=tokens)continue;float a=acc[n][e],v=post==3?Hrtz(a):post==4?F(a):F(Hrtz(a));uint dest=p*C+c;
  if(cropw){uint x=p%workw,y=p/workw;if(x<sx||y<sy||x>=sx+cropw||y>=sy+croph)continue;dest=((y-sy)*cropw+x-sx)*C+c;}out[dest]=v;}
}
KERNEL __attribute__((amdgpu_flat_work_group_size(64,64))) void mh_attention_project_frag_c512_n2(const unsigned char*av8,const float*feature,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){c512_proj_n2_body<true>(av8,feature,w,out,tokens,post,cropw,croph,workw,sx,sy);}
FASTWAVE void mh_attention_project_frag_c512_n2g(const unsigned char*av8,const float*feature,const float*w,float*out,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy){c512_proj_n2_body<false>(av8,feature,w,out,tokens,post,cropw,croph,workw,sx,sy);}
#endif

// One wave per pixel/head. Read every channel coalesced, but preserve the
// production sequential FP32 addition order with scalar-lane extraction.
// ABI matches normalize_fast; block256, grid ceil(tokens*heads/8).
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256)))
void mh_qkv_normalize_fast_wave(const float* qkv,const float* weights,float* normalized,uint tokens,uint channels){
 uint t=__builtin_amdgcn_workitem_id_x(),row=__builtin_amdgcn_workgroup_id_x()*8+t/32,c=t%32,heads=channels/32;
 if(!valid_attn(channels)||row>=tokens*heads)return;
 uint p=row/heads,head=row%heads,scale=4*channels*channels+heads*4096;
 for(uint part=0;part<3;part++){
  uint offset=(p*3+part)*channels+head*32+c;float v=qkv[offset],inv=1.f;
  if(part<2){float sq=v*v,ss=0.f;
   #pragma unroll
   for(uint j=0;j<32;j++)ss+=value(__builtin_amdgcn_readlane(bits(sq),j));
   inv=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?weights[scale+head]:1.f);
  }
  normalized[offset]=F(v*inv);
 }
}

// Byte-output ABI: same arithmetic, one byte per normalized element.
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256)))
void mh_qkv_normalize_fast_wave_fp8(const float* qkv,const float* weights,unsigned char* normalized,uint tokens,uint channels){
 uint t=__builtin_amdgcn_workitem_id_x(),row=__builtin_amdgcn_workgroup_id_x()*8+t/32,c=t%32,heads=channels/32;
 if(!valid_attn(channels)||row>=tokens*heads)return;
 uint p=row/heads,head=row%heads,scale=4*channels*channels+heads*4096;
 for(uint part=0;part<3;part++){
  uint offset=(p*3+part)*channels+head*32+c;float v=qkv[offset],inv=1.f;
  if(part<2){float sq=v*v,ss=0.f;
   #pragma unroll
   for(uint j=0;j<32;j++)ss+=value(__builtin_amdgcn_readlane(bits(sq),j));
   inv=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?weights[scale+head]:1.f);
  }
  normalized[offset]=static_cast<unsigned char>(q8(F(v*inv)));
 }
}

TILE void mh_ffn_expand_fast_fp8(const float* input,const float* weights,float* hidden,uint tokens,uint channels){if(valid_c(channels))fast_dense<false,true>(input,input,weights,hidden,tokens,4*channels,channels,0,~0u,1);}

TILE void mh_ffn_contract_fast_fp8(const float* hidden,const float* weights,float* middle,uint tokens,uint channels){if(valid_c(channels))fast_dense<true,false>(hidden,hidden,weights,middle,tokens,channels,4*channels,4*channels*channels,~0u,5);}

TILE void mh_attention_project_fast_scalar_fp8(const float* av,const float* feature,const float* weights,float* output,uint tokens,uint channels,uint post){if(valid_attn(channels)&&(post==0||post==3||post==4))fast_dense<true,false>(av,feature,weights,output,tokens,channels,channels,3*channels*channels,4*channels*channels+(channels/32)*4096+channels/32,post==0?5:post);}

TILE void mh_attention_project_fast_matrix_fp8(const float* av,const float* feature,const float* weights,float* output,uint tokens,uint channels,uint post){if(valid_attn(channels)&&(post==0||post==3||post==4))fast_dense<true,false>(av,feature,weights,output,tokens,channels,channels,3*channels*channels,4*channels*channels+(channels/32)*4096+channels/32,post==0?5:post,0,0,0,1);}

TILE void mh_ffn_contract_fast_fp8_out(const float* hidden,const float* weights,float* middle,uint tokens,uint channels){if(valid_c(channels))fast_dense<true,true>(hidden,hidden,weights,middle,tokens,channels,4*channels,4*channels*channels,~0u,5);}

TILE void mh_ffn_project_fast_fp8(const float* middle,const float* feature,const float* weights,float* output,uint tokens,uint channels){if(valid_c(channels))fast_dense<true,false>(middle,feature,weights,output,tokens,channels,channels,8*channels*channels,9*channels*channels,4);}

// Full FP32 QKV accumulator is normalized on chip, preserving sequential sums.
TILE void mh_qkv_normalize_fused(const float*in,const float*w,float*out,uint tokens,uint channels){
#if HIP_QKV_SPECIALIZE
 switch(channels){
 case 64:fast_dense<false,false,true>(in,in,w,out,tokens,192,64,0,~0u,2);break;
 case 128:fast_dense<false,false,true>(in,in,w,out,tokens,384,128,0,~0u,2);break;
 case 256:fast_dense<false,false,true>(in,in,w,out,tokens,768,256,0,~0u,2);break;
 case 512:fast_dense<false,false,true>(in,in,w,out,tokens,1536,512,0,~0u,2);break;
 }
#else
 if(valid_attn(channels))fast_dense<false,false,true>(in,in,w,out,tokens,3*channels,channels,0,~0u,2);
#endif
}

#ifndef HIP_FFN_COOP_INPUT
#define HIP_FFN_COOP_INPUT 1
#endif
// Fused MH FFN:16 tokens/group, C/16 waves, hidden FP8 stays in LDS.
// External input is f32 carrying FP8 values; weights use existing packed regions.
template<uint C,bool Mapped> DEV float ffn_input(const float*in,uint p,uint c,uint width,uint height,uint workw,uint sx,uint sy){if constexpr(Mapped){int x=int(p%workw)-int(sx),y=int(p/workw)-int(sy);return x>=0&&y>=0&&x<int(width)&&y<int(height)?in[(uint(y)*width+uint(x))*C+c]:0.f;}return in[p*C+c];}
#ifndef HIP_FFN_PK_ACT
#define HIP_FFN_PK_ACT 0 /* expand activation epilogue: 1 exact byte pairing per cvt_pk, 2 packed-f16 polynomial (relaxed, PSNR-gated) */
#endif
using h2=_Float16 __attribute__((ext_vector_type(2)));using f2=float __attribute__((ext_vector_type(2)));
#ifndef HIP_FFN_ABLATE_ACT
#define HIP_FFN_ABLATE_ACT 0 /* timing-only: skip the expand activation polynomial */
#endif
#ifndef HIP_FFN_HOIST_RES
#define HIP_FFN_HOIST_RES 2 /* 2026-09-17: level 1 -0.03, level 2 -0.14, bit-exact. 1: the FFN+QKV kernels' 8 residual input reads are issued branch-free (clamped index, select after) and the scale once; ISA of the conditional form: 8 x (global_load -> s_wait_loadcnt 0) per lane */
#endif
// Branch-free twins: the load is unconditional at a clamped (in-window) index, the window test selects the value.
template<uint C,bool Mapped> DEV float ffn_input_nb(const float*in,uint p,uint c,uint width,uint height,uint workw,uint sx,uint sy){if constexpr(Mapped){int x=int(p%workw)-int(sx),y=int(p/workw)-int(sy);bool ok=x>=0&&y>=0&&x<int(width)&&y<int(height);uint xi=ok?uint(x):0u,yi=ok?uint(y):0u;float v=in[(yi*width+xi)*C+c];return ok?v:0.f;}return in[p*C+c];}
template<uint C,bool Mapped> DEV float ffn_input8_nb(const unsigned char*in,uint p,uint c,uint width,uint height,uint workw,uint sx,uint sy){if constexpr(Mapped){int x=int(p%workw)-int(sx),y=int(p/workw)-int(sy);bool ok=x>=0&&y>=0&&x<int(width)&&y<int(height);uint xi=ok?uint(x):0u,yi=ok?uint(y):0u;float v=__builtin_amdgcn_cvt_f32_fp8(int(in[(yi*width+xi)*C+c]),0);return ok?v:0.f;}return __builtin_amdgcn_cvt_f32_fp8(int(in[p*C+c]),0);}
template<uint C,bool Tiled=false,bool Project=false,bool Mapped=false,bool Grouped=false>
DEV void mh_ffn_fused_body(const float*in,const float*w,unsigned char*out,uint tokens,uint width=0,uint height=0,uint workw=0,uint sx=0,uint sy=0){
 uint tid=__builtin_amdgcn_workitem_id_x(),wave=tid/32,l=tid%32,rc=l&15,group=l>>4;
 uint first=__builtin_amdgcn_workgroup_id_x()*16;
 if(first>=tokens)return;
 __attribute__((shared)) unsigned char hidden[16*(4*C+4)];
 const uint*pw=reinterpret_cast<const uint*>(w);
#if HIP_FFN_COOP_INPUT
#if HIP_FFN_INPUT_PACK4
 for(uint i=tid;i<16*(C/4+1);i+=2*C){uint row=i/(C/4+1),k=(i%(C/4+1))*4,word=0;if(k<C)word=pack4(ffn_input<C,Mapped>(in,first+row,k,width,height,workw,sx,sy),ffn_input<C,Mapped>(in,first+row,k+1,width,height,workw,sx,sy),ffn_input<C,Mapped>(in,first+row,k+2,width,height,workw,sx,sy),ffn_input<C,Mapped>(in,first+row,k+3,width,height,workw,sx,sy));__builtin_memcpy(hidden+i*4,&word,4);}
#else
 for(uint i=tid;i<16*(C+4);i+=2*C){uint row=i/(C+4),k=i%(C+4);hidden[i]=k<C?static_cast<unsigned char>(q8(ffn_input<C,Mapped>(in,first+row,k,width,height,workw,sx,sy))):0;}
#endif
 __builtin_amdgcn_s_barrier();
#endif
 f8 expand[4]{};
 for(uint k=0;k<C;k+=16){i2 a{};
#if HIP_FFN_COOP_INPUT
  __builtin_memcpy(&a,hidden+rc*(C+4)+k+group*8,8);
#else
  for(uint e=0;e<8;e++)pack(a,e,ffn_input<C,Mapped>(in,first+rc,k+group*8+e,width,height,workw,sx,sy));
#endif
  for(uint tile=0;tile<4;tile++){uint row=wave*64+tile*16+rc,offset=(row*C+k+group*8)/4;i2 b{};if constexpr(Tiled){const unsigned char*wb=reinterpret_cast<const unsigned char*>(w);uint off=((row/16)*(C/32)+k/32)*512+(k%32+group*8)*16+row%16;for(uint e=0;e<8;e++)b[e/4]=int(uint(b[e/4])|(uint(wb[off+e*16])<<(8*(e%4))));}else b={int(pw[offset]),int(pw[offset+1])};expand[tile]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,expand[tile]);}
 }
#if HIP_FFN_COOP_INPUT
 // Every wave must finish reading input before its storage becomes hidden.
 __builtin_amdgcn_s_barrier();
#endif
 for(uint tile=0;tile<4;tile++)for(uint e=0;e<8;e++){float a=expand[tile][e],g=clampf(a,-4.f,4.f),q=__builtin_fmaf(absf(g),-.055908203125f,.447265625f),poly=__builtin_fmaf(g,q,.89453125f);hidden[(group*8+e)*(4*C+4)+wave*64+tile*16+rc]=static_cast<unsigned char>(q8(F(a*poly)));}
 __builtin_amdgcn_s_barrier();
 f8 accum{};
 const uint first_k=Grouped?(wave/2)*128:0,last_k=Grouped?first_k+128:4*C;
 for(uint k=first_k;k<last_k;k+=16){i2 a;__builtin_memcpy(&a,hidden+rc*(4*C+4)+k+group*8,8);uint offset=4*C*C+((wave*16+rc)*(4*C)+k+group*8)/4;i2 b{};if constexpr(Tiled){const unsigned char*wb=reinterpret_cast<const unsigned char*>(w+4*C*C);uint row=wave*16+rc,off=((row/16)*(4*C/32)+k/32)*512+(k%32+group*8)*16+row%16;for(uint e=0;e<8;e++)b[e/4]=int(uint(b[e/4])|(uint(wb[off+e*16])<<(8*(e%4))));}else b={int(pw[offset]),int(pw[offset+1])};accum=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,accum);}
 if constexpr(Project){
  __builtin_amdgcn_s_barrier();
  for(uint e=0;e<8;e++)hidden[(group*8+e)*(C+4)+wave*16+rc]=static_cast<unsigned char>(q8(F(Hrtz(accum[e]))));
  __builtin_amdgcn_s_barrier();
  f8 result{};for(uint e=0;e<8;e++)result[e]=Hrtz(ffn_input<C,Mapped>(in,first+group*8+e,wave*16+rc,width,height,workw,sx,sy)*w[9*C*C+wave*16+rc]);
  for(uint k=0;k<C;k+=16){i2 a,b;__builtin_memcpy(&a,hidden+rc*(C+4)+k+group*8,8);uint off=8*C*C+((wave*16+rc)*C+k+group*8)/4;b={int(pw[off]),int(pw[off+1])};result=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,result);}
  for(uint e=0;e<8;e++)reinterpret_cast<float*>(out)[(first+group*8+e)*C+wave*16+rc]=F(result[e]);
 }else{
 for(uint e=0;e<8;e++)out[(first+group*8+e)*C+wave*16+rc]=static_cast<unsigned char>(q8(F(Hrtz(accum[e]))));
 }
}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64(const float*in,const float*w,unsigned char*out,uint tokens){mh_ffn_fused_body<64>(in,w,out,tokens);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128(const float*in,const float*w,unsigned char*out,uint tokens){mh_ffn_fused_body<128>(in,w,out,tokens);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256(const float*in,const float*w,unsigned char*out,uint tokens){mh_ffn_fused_body<256>(in,w,out,tokens);}

KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_tiled(const float*in,const float*w,unsigned char*out,uint tokens){mh_ffn_fused_body<64,true>(in,w,out,tokens);}

KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_tiled(const float*in,const float*w,unsigned char*out,uint tokens){mh_ffn_fused_body<128,true>(in,w,out,tokens);}

KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_tiled(const float*in,const float*w,unsigned char*out,uint tokens){mh_ffn_fused_body<256,true>(in,w,out,tokens);}

KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project(const float*in,const float*w,unsigned char*out,uint tokens){mh_ffn_fused_body<64,false,true>(in,w,out,tokens);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project(const float*in,const float*w,unsigned char*out,uint tokens){mh_ffn_fused_body<128,false,true>(in,w,out,tokens);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_tiled_project(const float*in,const float*w,unsigned char*out,uint tokens){mh_ffn_fused_body<256,true,true>(in,w,out,tokens);}

TILE void mh_attention_crop(const float*av,const float*feature,const float*weights,float*output,uint tokens,uint post,uint cropw,uint croph,uint workw,uint sx,uint sy,uint channels){if(valid_attn(channels)&&(post==0||post==3||post==4))fast_dense<true,false,false,true>(av,feature,weights,output,tokens,channels,channels,3*channels*channels,4*channels*channels+(channels/32)*4096+channels/32,post==0?5:post,0,0,0,channels==512?0:1,cropw,croph,workw,sx,sy);}

KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_mapped(const float*in,const float*w,unsigned char*out,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_fused_body<64,false,true,true>(in,w,out,tokens,width,height,workw,sx,sy);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_mapped(const float*in,const float*w,unsigned char*out,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_fused_body<128,false,true,true>(in,w,out,tokens,width,height,workw,sx,sy);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_tiled_project_mapped(const float*in,const float*w,unsigned char*out,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_fused_body<256,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy);}

KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_g128(const float*in,const float*w,unsigned char*out,uint tokens){mh_ffn_fused_body<64,false,true,false,true>(in,w,out,tokens);}

KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_mapped_g128(const float*in,const float*w,unsigned char*out,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_fused_body<64,false,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy);}

KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_g128(const float*in,const float*w,unsigned char*out,uint tokens){mh_ffn_fused_body<128,false,true,false,true>(in,w,out,tokens);}

KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_mapped_g128(const float*in,const float*w,unsigned char*out,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_fused_body<128,false,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy);}

KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_tiled_project_g128(const float*in,const float*w,unsigned char*out,uint tokens){mh_ffn_fused_body<256,true,true,false,true>(in,w,out,tokens);}

KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_tiled_project_mapped_g128(const float*in,const float*w,unsigned char*out,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_fused_body<256,true,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy);}

// Byte result of q8(F(x)), preserving canonical zero and signed saturation.
#ifndef C512_PROJ_M32
#define C512_PROJ_M32 0 /* 2026-09-29 candidate: mh_attention_project_frag_c512 two token tiles share each weight fragment (same grid/ABI) */
#endif
#ifndef HIP_C512_HOIST_RES
#define HIP_C512_HOIST_RES 0 /* 1: mh_attention_project_frag_c512 residual init loads hoisted out of the p<tokens condition */
#endif
#ifndef HIP_BRANCHLESS_Q8
#define HIP_BRANCHLESS_Q8 1 /* 2026-09-17: with HIP_MH_RTZ_ISA -0.16ms, bit-exact. q8_fused_round as clamp+cvt+select (ISA: every call was a divergent branch pair, 64 per FFN+QKV kernel); same byte for every non-NaN input */
#endif
#if HIP_BRANCHLESS_Q8
// f-sweep 2026-09-30: HIP_Q8_ADD0 1 -> q8(clamp(x+0)): the +0 add turns -0 into +0 exactly like the a==0 select;
// GPU exhaustive over all 2^32 x (results/f-sweep-20260930). The add stays apart from any producer mul.
#ifndef HIP_Q8_ADD0
#define HIP_Q8_ADD0 0
#endif
#if HIP_Q8_ADD0
DEV uint q8_fused_round(float x){
#pragma clang fp contract(off)
 return q8(HIP_FP8_CLAMP(x+0.f));}
#else
DEV uint q8_fused_round(float x){uint a=bits(x)&0x7fffffffu;uint q=q8(HIP_FP8_CLAMP(x));return a==0?0u:q;}
#endif
#else
DEV uint q8_fused_round(float x){uint b=bits(x),a=b&0x7fffffffu;if(!a)return 0;if(a>=0x43e00000u)return ((b>>24)&128u)|126u;return q8(x);}
#endif
// Byte residual stream (host option mh_byte_stream): the block input arrives as the E4M3 bytes the previous block's
// attention-project stored; every input value the f32 path would read is exactly cvt_f32_fp8(byte), so decoding here and
// feeding the unchanged f32 code is bit for bit the same computation. Padding outside the mapped window stays +0.
template<uint C,bool Mapped> DEV float ffn_input8(const unsigned char*in,uint p,uint c,uint width,uint height,uint workw,uint sx,uint sy){if constexpr(Mapped){int x=int(p%workw)-int(sx),y=int(p/workw)-int(sy);return x>=0&&y>=0&&x<int(width)&&y<int(height)?__builtin_amdgcn_cvt_f32_fp8(int(in[(uint(y)*width+uint(x))*C+c]),0):0.f;}return __builtin_amdgcn_cvt_f32_fp8(int(in[p*C+c]),0);}
// Preserve encoded E4M3 input (including signed zero) when staging four channels.
// Producers emit finite FP8 values; invalid mapped positions must still become positive zero.
template<uint C,bool Mapped> DEV uint ffn_input8_word(const unsigned char*in,uint p,uint c,uint width,uint height,uint workw,uint sx,uint sy){
 uint index=p*C+c;bool ok=true;
 if constexpr(Mapped){int x=int(p%workw)-int(sx),y=int(p/workw)-int(sy);ok=x>=0&&y>=0&&x<int(width)&&y<int(height);uint xi=ok?uint(x):0u,yi=ok?uint(y):0u;index=(yi*width+xi)*C+c;}
 uint word;__builtin_memcpy(&word,in+index,4);return ok?word:0u;
}
// Byte inputs can feed WMMA fragments directly; mapped padding remains positive zero.
template<uint C,bool Mapped> DEV i2 ffn_input8_fragment(const unsigned char*in,uint p,uint c,uint width,uint height,uint workw,uint sx,uint sy){
 uint index=p*C+c;bool ok=true;
 if constexpr(Mapped){int x=int(p%workw)-int(sx),y=int(p/workw)-int(sy);ok=x>=0&&y>=0&&x<int(width)&&y<int(height);uint xi=ok?uint(x):0u,yi=ok?uint(y):0u;index=(yi*width+xi)*C+c;}
 i2 v;__builtin_memcpy(&v,in+index,8);return ok?v:i2{};
}
// BatchNorm: the three QKV projections are computed first, both normalized parts' raw tiles go to LDS in one round and
// their row sums run on 2*16*(C/32) threads behind a single barrier (same sequential 32-term order per row, so the
// same bytes); two barriers and one LDS round per block fewer than the part-by-part loop. C64/C128 only (LDS budget).
// Frag: every B fragment is one contiguous 8-byte load from FragmentPackedMatrix tiles (PackedFusedMhWeightFrag for the
// FFN/project regions, PackedMhWeightQkvFragOnly for the QKV rows) instead of 16 lanes touching 16 weight rows.
DEV i2 frag_b(const unsigned char*base,uint row,uint K,uint k,uint group){i2 b;__builtin_memcpy(&b,base+((row/16)*(K/32)+k/32)*512+((((k%32)/16)*2+group)*16+row%16)*8,8);return b;}
template<uint C,bool Tiled=false,bool Project=false,bool Mapped=false,bool Grouped=false,bool ByteIn=false,bool ByteFeature=false,bool BatchNorm=false,bool Frag=false>
DEV void mh_ffn_qkv_body(const float*in,const float*w,unsigned char*out,uint tokens,uint width=0,uint height=0,uint workw=0,uint sx=0,uint sy=0,const float*aw=nullptr,unsigned char*norm=nullptr){
 const unsigned char*in8=reinterpret_cast<const unsigned char*>(in);
 auto load_in=[&](uint row,uint k)->float{if constexpr(ByteIn)return ffn_input8<C,Mapped>(in8,row,k,width,height,workw,sx,sy);else return ffn_input<C,Mapped>(in,row,k,width,height,workw,sx,sy);};
 auto load_in_nb=[&](uint row,uint k)->float{if constexpr(ByteIn)return ffn_input8_nb<C,Mapped>(in8,row,k,width,height,workw,sx,sy);else return ffn_input_nb<C,Mapped>(in,row,k,width,height,workw,sx,sy);};
 uint tid=__builtin_amdgcn_workitem_id_x(),wave=tid/32,l=tid%32,rc=l&15,group=l>>4;
 uint first=__builtin_amdgcn_workgroup_id_x()*16;
 if(first>=tokens)return;
 // A complete16-row tile in top/bottom mapped padding has exactly zero
 // bias-free FFN and normalized QKV outputs. The test is workgroup-uniform.
 if constexpr((C==128||C==256)&&Mapped){
  if(first+15<sy*workw||first>=(sy+height)*workw){
#if MH_PAD_ZERO_VEC
   if constexpr(ByteFeature){for(uint i=tid*8;i<16*C;i+=2*C*8){unsigned long long z=0;__builtin_memcpy(__builtin_assume_aligned(out+first*C+i,8),&z,8);}}
   else for(uint i=tid;i<16*C;i+=2*C)reinterpret_cast<float*>(out)[first*C+i]=0.f;
   for(uint i=tid*8;i<16*3*C;i+=2*C*8){unsigned long long z=0;__builtin_memcpy(__builtin_assume_aligned(norm+first*3*C+i,8),&z,8);}
#else
   for(uint i=tid;i<16*C;i+=2*C){if constexpr(ByteFeature)out[first*C+i]=0;else reinterpret_cast<float*>(out)[first*C+i]=0.f;}
   for(uint i=tid;i<16*3*C;i+=2*C)norm[first*3*C+i]=0;
#endif
   return;
  }
 }

 static_assert(Project&&Grouped,"FFN/QKV requires grouped project");
 union Storage {unsigned char bytes[16*(4*C+4)];float raw[(BatchNorm?2:1)*16*(C/32)*33];};
 __attribute__((shared)) Storage storage;unsigned char*hidden=storage.bytes;
 __attribute__((shared)) unsigned char qfeature[16*(C+4)];
 __attribute__((shared)) float inverse[(BatchNorm?2:1)*16*(C/32)];
#if HIP_FFN_LINE_STORES
 constexpr uint NS=C+8; /* row stride: 8-byte aligned, 66 dwords at C=256 so the 16 lanes' rows land in different banks */
 __attribute__((shared)) unsigned char nstage[2][16*NS];
 /* one 8-byte piece per thread (2C threads = 16 rows x C/8 pieces): wave w writes row w at C=256, contiguous C bytes per row */
 auto flush_norm=[&](const unsigned char*st,uint part){uint row=tid/(C/8),col=(tid%(C/8))*8;unsigned long long v;__builtin_memcpy(&v,st+row*NS+col,8);__builtin_memcpy(norm+((first+row)*3+part)*C+col,&v,8);};
#endif
 const uint*pw=reinterpret_cast<const uint*>(w);
#if HIP_FFN_COOP_INPUT
 if constexpr(!ByteIn){
#if HIP_FFN_INPUT_PACK4
#if HIP_FFN_HOIST_RES>=2
 // Level 2: the cooperative input staging also loads branch-free (4 window tests per word, loads overlap). Same bytes.
 for(uint i=tid;i<16*(C/4+1);i+=2*C){uint row=i/(C/4+1),k=(i%(C/4+1))*4,word=0;if(k<C){if constexpr(ByteIn)word=ffn_input8_word<C,Mapped>(in8,first+row,k,width,height,workw,sx,sy);else word=pack4(load_in_nb(first+row,k),load_in_nb(first+row,k+1),load_in_nb(first+row,k+2),load_in_nb(first+row,k+3));}__builtin_memcpy(hidden+i*4,&word,4);}
#else
 for(uint i=tid;i<16*(C/4+1);i+=2*C){uint row=i/(C/4+1),k=(i%(C/4+1))*4,word=0;if(k<C){if constexpr(ByteIn)word=ffn_input8_word<C,Mapped>(in8,first+row,k,width,height,workw,sx,sy);else word=pack4(load_in(first+row,k),load_in(first+row,k+1),load_in(first+row,k+2),load_in(first+row,k+3));}__builtin_memcpy(hidden+i*4,&word,4);}
#endif
#else
 for(uint i=tid;i<16*(C+4);i+=2*C){uint row=i/(C+4),k=i%(C+4);hidden[i]=k<C?static_cast<unsigned char>(q8(load_in(first+row,k))):0;}
#endif
 __builtin_amdgcn_s_barrier();
 }
#endif
 f8 expand[4]{};
 for(uint k=0;k<C;k+=16){i2 a{};
#if HIP_FFN_COOP_INPUT
  if constexpr(ByteIn)a=ffn_input8_fragment<C,Mapped>(in8,first+rc,k+group*8,width,height,workw,sx,sy);else __builtin_memcpy(&a,hidden+rc*(C+4)+k+group*8,8);
#else
  for(uint e=0;e<8;e++)pack(a,e,load_in(first+rc,k+group*8+e));
#endif
  for(uint tile=0;tile<4;tile++){uint row=wave*64+tile*16+rc,offset=(row*C+k+group*8)/4;i2 b{};if constexpr(Frag)b=frag_b(reinterpret_cast<const unsigned char*>(w),row,C,k,group);else if constexpr(Tiled){const unsigned char*wb=reinterpret_cast<const unsigned char*>(w);uint off=((row/16)*(C/32)+k/32)*512+(k%32+group*8)*16+row%16;for(uint e=0;e<8;e++)b[e/4]=int(uint(b[e/4])|(uint(wb[off+e*16])<<(8*(e%4))));}else b={int(pw[offset]),int(pw[offset+1])};expand[tile]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,expand[tile]);}
 }
#if HIP_FFN_COOP_INPUT
 // ByteIn reads global memory; only the f32 staging aliases the hidden buffer.
 if constexpr(!ByteIn)__builtin_amdgcn_s_barrier();
#endif
#if HIP_FFN_ABLATE_ACT
 for(uint tile=0;tile<4;tile++)for(uint e=0;e<8;e++)hidden[(group*8+e)*(4*C+4)+wave*64+tile*16+rc]=static_cast<unsigned char>(q8(expand[tile][e])); // timing only
#elif HIP_FFN_PK_ACT==1
 // Exact pairing: two activated values per v_cvt_pk_fp8_f32 (same clamp/RNE per element), the canonical-zero select on the bytes.
 for(uint tile=0;tile<4;tile++)for(uint e=0;e<8;e+=2){float x[2];for(uint j=0;j<2;j++){float a=expand[tile][e+j],g=clampf(a,-4.f,4.f),q=__builtin_fmaf(absf(g),-.055908203125f,.447265625f),poly=__builtin_fmaf(g,q,.89453125f);x[j]=a*poly;}
  uint two=uint(__builtin_amdgcn_cvt_pk_fp8_f32(HIP_FP8_CLAMP(x[0]),HIP_FP8_CLAMP(x[1]),0,false));
  for(uint j=0;j<2;j++)hidden[(group*8+e+j)*(4*C+4)+wave*64+tile*16+rc]=static_cast<unsigned char>((bits(x[j])&0x7fffffffu)?(two>>(8*j))&255u:0u);}
#elif HIP_FFN_PK_ACT==2
 // Relaxed (PSNR-gated): the activation polynomial in packed f16 (v_pk_*), two lanes of the 8-vector per instruction;
 // the product a*poly is formed in f16 before the E4M3 cast (f32 in the exact chain), so the byte can differ by one ulp.
 for(uint tile=0;tile<4;tile++)for(uint e=0;e<8;e+=2){h2 a=__builtin_convertvector(f2{expand[tile][e],expand[tile][e+1]},h2);h2 g=__builtin_elementwise_min(__builtin_elementwise_max(a,h2{-4.f,-4.f}),h2{4.f,4.f});h2 q=__builtin_elementwise_abs(g)*h2{-.055908203125f,-.055908203125f}+h2{.447265625f,.447265625f};h2 o=a*(g*q+h2{.89453125f,.89453125f});f2 x=__builtin_convertvector(o,f2);
  uint two=uint(__builtin_amdgcn_cvt_pk_fp8_f32(HIP_FP8_CLAMP(x[0]),HIP_FP8_CLAMP(x[1]),0,false));
  for(uint j=0;j<2;j++)hidden[(group*8+e+j)*(4*C+4)+wave*64+tile*16+rc]=static_cast<unsigned char>((bits(x[j])&0x7fffffffu)?(two>>(8*j))&255u:0u);}
#else
 for(uint tile=0;tile<4;tile++)for(uint e=0;e<8;e++){float a=expand[tile][e],g=clampf(a,-4.f,4.f),q=__builtin_fmaf(absf(g),-.055908203125f,.447265625f),poly=__builtin_fmaf(g,q,.89453125f);hidden[(group*8+e)*(4*C+4)+wave*64+tile*16+rc]=static_cast<unsigned char>(q8_fused_round(a*poly));}
#endif
 __builtin_amdgcn_s_barrier();
 f8 accum{};
 const uint first_k=Grouped?(wave/2)*128:0,last_k=Grouped?first_k+128:4*C;
 for(uint k=first_k;k<last_k;k+=16){i2 a;__builtin_memcpy(&a,hidden+rc*(4*C+4)+k+group*8,8);uint offset=4*C*C+((wave*16+rc)*(4*C)+k+group*8)/4;i2 b{};if constexpr(Frag)b=frag_b(reinterpret_cast<const unsigned char*>(w+4*C*C),wave*16+rc,4*C,k,group);else if constexpr(Tiled){const unsigned char*wb=reinterpret_cast<const unsigned char*>(w+4*C*C);uint row=wave*16+rc,off=((row/16)*(4*C/32)+k/32)*512+(k%32+group*8)*16+row%16;for(uint e=0;e<8;e++)b[e/4]=int(uint(b[e/4])|(uint(wb[off+e*16])<<(8*(e%4))));}else b={int(pw[offset]),int(pw[offset+1])};accum=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,accum);}
 if constexpr(Project){
  __builtin_amdgcn_s_barrier();
  for(uint e=0;e<8;e++)hidden[(group*8+e)*(C+4)+wave*16+rc]=static_cast<unsigned char>(q8_fused_round(Hrtz(accum[e])));
  __builtin_amdgcn_s_barrier();
#if HIP_FFN_HOIST_RES
#if HIP_FFN_TRANSPOSED_TAIL
  f8 result{};{float rv[8],rs[8];
   for(uint e=0;e<8;e++){uint c=wave*16+group*8+e;rs[e]=w[9*C*C+c];if constexpr(ByteIn)rv[e]=ffn_input8_nb<C,Mapped>(in8,first+rc,c,width,height,workw,sx,sy);else rv[e]=ffn_input_nb<C,Mapped>(in,first+rc,c,width,height,workw,sx,sy);}
   for(uint e=0;e<8;e++)result[e]=Hrtz(rv[e]*rs[e]);}
#else
  f8 result{};{const float rs=w[9*C*C+wave*16+rc];float rv[8];
   for(uint e=0;e<8;e++){if constexpr(ByteIn)rv[e]=ffn_input8_nb<C,Mapped>(in8,first+group*8+e,wave*16+rc,width,height,workw,sx,sy);else rv[e]=ffn_input_nb<C,Mapped>(in,first+group*8+e,wave*16+rc,width,height,workw,sx,sy);}
   for(uint e=0;e<8;e++)result[e]=Hrtz(rv[e]*rs);}
#endif
#else
#if HIP_FFN_TRANSPOSED_TAIL
  f8 result{};for(uint e=0;e<8;e++)result[e]=Hrtz(load_in(first+rc,wave*16+group*8+e)*w[9*C*C+wave*16+group*8+e]);
#else
  f8 result{};for(uint e=0;e<8;e++)result[e]=Hrtz(load_in(first+group*8+e,wave*16+rc)*w[9*C*C+wave*16+rc]);
#endif
#endif
#if HIP_FFN_TRANSPOSED_TAIL
  for(uint k=0;k<C;k+=16){i2 a,b;__builtin_memcpy(&a,hidden+rc*(C+4)+k+group*8,8);uint off=8*C*C+((wave*16+rc)*C+k+group*8)/4;if constexpr(Frag)b=frag_b(reinterpret_cast<const unsigned char*>(w+8*C*C),wave*16+rc,C,k,group);else b={int(pw[off]),int(pw[off+1])};result=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,result);}
#else
  for(uint k=0;k<C;k+=16){i2 a,b;__builtin_memcpy(&a,hidden+rc*(C+4)+k+group*8,8);uint off=8*C*C+((wave*16+rc)*C+k+group*8)/4;if constexpr(Frag)b=frag_b(reinterpret_cast<const unsigned char*>(w+8*C*C),wave*16+rc,C,k,group);else b={int(pw[off]),int(pw[off+1])};result=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,result);}
#endif
#if HIP_FFN_TRANSPOSED_TAIL
  for(uint e=0;e<8;e++){uint byte=q8_fused_round(result[e]);float v=__builtin_amdgcn_cvt_f32_fp8(int(byte),0);uint c=wave*16+group*8+e;if constexpr(ByteFeature){
#if !HIP_FFN_LINE_STORES
   out[(first+rc)*C+c]=static_cast<unsigned char>(byte);
#endif
   }else reinterpret_cast<float*>(out)[(first+rc)*C+c]=v;qfeature[rc*(C+4)+c]=static_cast<unsigned char>(byte);}
#else
  for(uint e=0;e<8;e++){uint byte=q8_fused_round(result[e]);float v=__builtin_amdgcn_cvt_f32_fp8(int(byte),0);if constexpr(ByteFeature)out[(first+group*8+e)*C+wave*16+rc]=static_cast<unsigned char>(byte);else reinterpret_cast<float*>(out)[(first+group*8+e)*C+wave*16+rc]=v;qfeature[(group*8+e)*(C+4)+wave*16+rc]=static_cast<unsigned char>(byte);}
#endif
  __builtin_amdgcn_s_barrier();
#if HIP_FFN_LINE_STORES
  if constexpr(ByteFeature){uint row=tid/(C/8),col=(tid%(C/8))*8;unsigned long long v;__builtin_memcpy(&v,qfeature+row*(C+4)+col,8);__builtin_memcpy(out+(first+row)*C+col,&v,8);}
#endif
  const uint*aq=reinterpret_cast<const uint*>(aw);
  if constexpr(BatchNorm){
   f8 q[3]{};
   for(uint part=0;part<3;part++)for(uint k=0;k<C;k+=16){i2 a,b;__builtin_memcpy(&a,qfeature+rc*(C+4)+k+group*8,8);uint off=((part*C+wave*16+rc)*C+k+group*8)/4;if constexpr(Frag)b=frag_b(reinterpret_cast<const unsigned char*>(aw),part*C+wave*16+rc,C,k,group);else b={int(aq[off]),int(aq[off+1])};q[part]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,q[part]);}
   for(uint part=0;part<2;part++)for(uint e=0;e<8;e++)storage.raw[part*16*(C/32)*33+((wave/2)*16+group*8+e)*33+(wave%2)*16+rc]=q[part][e];
   __builtin_amdgcn_s_barrier();
   if(tid<2*16*(C/32)){uint part=tid/(16*(C/32)),t=tid%(16*(C/32)),head=t/16,row=t%16;float ss=0;for(uint k=0;k<32;k++){float v=storage.raw[part*16*(C/32)*33+(head*16+row)*33+k];ss+=v*v;}inverse[tid]=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?aw[4*C*C+(C/32)*4096+head]:1.f);}
   __builtin_amdgcn_s_barrier();
   for(uint part=0;part<3;part++)for(uint e=0;e<8;e++){uint row=group*8+e;float inv=part<2?inverse[part*16*(C/32)+(wave/2)*16+row]:1.f;norm[((first+row)*3+part)*C+wave*16+rc]=static_cast<unsigned char>(q8_fused_round(q[part][e]*inv));}
  }else
#if HIP_FFN_WAVE_NORM
  {const uint heads=C/32;uint hp=wave/heads,head=wave%heads;f8 q[2]{};
   for(uint j=0;j<2;j++)for(uint k=0;k<C;k+=16){i2 a,b;__builtin_memcpy(&a,qfeature+rc*(C+4)+k+group*8,8);uint col=head*32+j*16+rc,off=((hp*C+col)*C+k+group*8)/4;if constexpr(Frag)b=frag_b(reinterpret_cast<const unsigned char*>(aw),hp*C+col,C,k,group);else b={int(aq[off]),int(aq[off+1])};q[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,q[j]);}
   float ss=0.f;for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)ss+=q[j][e]*q[j][e];
   ss+=__builtin_bit_cast(float,__builtin_amdgcn_ds_bpermute(int((l^16)*4),__builtin_bit_cast(int,ss)));
   float inv=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(hp==0?aw[4*C*C+(C/32)*4096+head]:1.f);
   for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)norm[((first+rc)*3+hp)*C+head*32+j*16+group*8+e]=static_cast<unsigned char>(q8_fused_round(q[j][e]*inv));}
  {f8 q{};
   for(uint k=0;k<C;k+=16){i2 a,b;__builtin_memcpy(&a,qfeature+rc*(C+4)+k+group*8,8);uint off=((2*C+wave*16+rc)*C+k+group*8)/4;if constexpr(Frag)b=frag_b(reinterpret_cast<const unsigned char*>(aw),2*C+wave*16+rc,C,k,group);else b={int(aq[off]),int(aq[off+1])};q=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,q);}
   for(uint e=0;e<8;e++)norm[((first+rc)*3+2)*C+wave*16+group*8+e]=static_cast<unsigned char>(q8_fused_round(q[e]*1.f));}
#else
  for(uint part=0;part<3;part++){f8 q{};
#if HIP_FFN_TRANSPOSED_TAIL
   for(uint k=0;k<C;k+=16){i2 a,b;__builtin_memcpy(&a,qfeature+rc*(C+4)+k+group*8,8);uint off=((part*C+wave*16+rc)*C+k+group*8)/4;if constexpr(Frag)b=frag_b(reinterpret_cast<const unsigned char*>(aw),part*C+wave*16+rc,C,k,group);else b={int(aq[off]),int(aq[off+1])};q=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,q);}
#else
   for(uint k=0;k<C;k+=16){i2 a,b;__builtin_memcpy(&a,qfeature+rc*(C+4)+k+group*8,8);uint off=((part*C+wave*16+rc)*C+k+group*8)/4;if constexpr(Frag)b=frag_b(reinterpret_cast<const unsigned char*>(aw),part*C+wave*16+rc,C,k,group);else b={int(aq[off]),int(aq[off+1])};q=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a,b,q);}
#endif
#if HIP_FFN_TRANSPOSED_TAIL
   if(part<2){for(uint e=0;e<8;e++)storage.raw[((wave/2)*16+rc)*33+(wave%2)*16+group*8+e]=q[e];
#else
   if(part<2){for(uint e=0;e<8;e++)storage.raw[((wave/2)*16+group*8+e)*33+(wave%2)*16+rc]=q[e];
#endif
    __builtin_amdgcn_s_barrier();
#if HIP_FFN_LINE_STORES
    if(part==1)flush_norm(nstage[0],0); /* part 0's rows are complete (staged before this barrier); nstage[0] is rewritten only after the next barrier */
#endif
    if(tid<16*(C/32)){uint head=tid/16,row=tid%16;float ss=0;for(uint k=0;k<32;k++){float v=storage.raw[(head*16+row)*33+k];ss+=v*v;}inverse[tid]=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?aw[4*C*C+(C/32)*4096+head]:1.f);}
    __builtin_amdgcn_s_barrier();
   }
#if HIP_FFN_TRANSPOSED_TAIL
#if HIP_FFN_LINE_STORES
   {float inv=part<2?inverse[(wave/2)*16+rc]:1.f;unsigned char*st=nstage[part==2?1:0];for(uint e=0;e<8;e++)st[rc*NS+wave*16+group*8+e]=static_cast<unsigned char>(q8_fused_round(q[e]*inv));}
#else
   {float inv=part<2?inverse[(wave/2)*16+rc]:1.f;for(uint e=0;e<8;e++)norm[((first+rc)*3+part)*C+wave*16+group*8+e]=static_cast<unsigned char>(q8_fused_round(q[e]*inv));}
#endif
#else
   for(uint e=0;e<8;e++){uint row=group*8+e;float inv=part<2?inverse[(wave/2)*16+row]:1.f;norm[((first+row)*3+part)*C+wave*16+rc]=static_cast<unsigned char>(q8_fused_round(q[e]*inv));}
#endif
  }
#if HIP_FFN_LINE_STORES
  WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2); /* parts 1 and 2 staged by every wave */
  flush_norm(nstage[0],1);flush_norm(nstage[1],2);
#endif
#endif
 }else{
 for(uint e=0;e<8;e++)out[(first+group*8+e)*C+wave*16+rc]=static_cast<unsigned char>(q8_fused_round(Hrtz(accum[e])));
 }
}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<64,false,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_mapped_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<64,false,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<128,false,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_mapped_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<128,false,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
// Fragment-weight twins for C64/C128 (host option mh_ffn_frag).
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_frag_project_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<64,false,true,false,true,false,false,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_frag_project_mapped_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<64,false,true,true,true,false,false,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_frag_project_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<128,false,true,false,true,false,false,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_frag_project_mapped_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<128,false,true,true,true,false,false,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
// Tiled-weight twins for C64/C128 (host option tiled_ffn_small): same body as the C256 production kernel.
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_tiled_project_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<64,true,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_tiled_project_mapped_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<64,true,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_tiled_project_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<128,true,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_tiled_project_mapped_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<128,true,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_tiled_project_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<256,true,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_tiled_project_mapped_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<256,true,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
// Batched QKV normalization (host option ffn_qkv_batched_norm), C64/C128.
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_g128_qkv_bn(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<64,false,true,false,true,false,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_mapped_g128_qkv_bn(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<64,false,true,true,true,false,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_g128_qkv_bn(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<128,false,true,false,true,false,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_mapped_g128_qkv_bn(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<128,false,true,true,true,false,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
// Byte residual stream variants: _fb = feature output stored as E4M3 bytes (attention-project reads them), _bytein_fb = block
// input also read as bytes. Same body, same arithmetic; only the storage format of lattice-exact values changes.
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_g128_qkv_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<64,false,true,false,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_mapped_g128_qkv_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<64,false,true,true,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_g128_qkv_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<128,false,true,false,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_mapped_g128_qkv_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<128,false,true,true,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_tiled_project_g128_qkv_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<256,true,true,false,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_tiled_project_mapped_g128_qkv_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<256,true,true,true,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_g128_qkv_bytein_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<64,false,true,false,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_mapped_g128_qkv_bytein_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<64,false,true,true,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_g128_qkv_bytein_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<128,false,true,false,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_mapped_g128_qkv_bytein_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<128,false,true,true,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_tiled_project_g128_qkv_bytein_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<256,true,true,false,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_tiled_project_mapped_g128_qkv_bytein_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<256,true,true,true,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}

// C256 contiguous weight fragments; paired with PackedFusedMhWeightFrag and PackedMhWeightQkvFragOnly.
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_frag_project_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<256,false,true,false,true,false,false,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_frag_project_mapped_g128_qkv(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<256,false,true,true,true,false,false,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_frag_project_g128_qkv_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<256,false,true,false,true,false,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_frag_project_mapped_g128_qkv_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<256,false,true,true,true,false,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_frag_project_g128_qkv_bytein_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<256,false,true,false,true,true,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_frag_project_mapped_g128_qkv_bytein_fb(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){mh_ffn_qkv_body<256,false,true,true,true,true,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);}

#ifndef HIP_PDL_KERNELS
#define HIP_PDL_KERNELS 1
#endif
#if HIP_PDL_KERNELS
// ---- Programmatic-dependent-launch emulation (2026-09-25, results/pdl-chain-20260925). Twins of the chain kernels used with
// hipExtAnyOrderLaunch (host option pdl): a group waits at entry for the producer tiles its tokens need, publishes its own
// tile at exit. Counters carry cumulative wave counts per (kernel kind, channels, lattice); the host tracks the targets.
// No per-group acquire: the dispatch packet's own acquire already invalidated L0/L1 for this launch and every line
// cached after that was loaded after its tile's flag. Math and layouts untouched; the original kernels are unchanged.
DEV void pdl_ffn_wait(const uint*pflags,uint pepoch,uint pww,uint psx,uint psy,uint tokens,uint width,uint height,uint workw,uint sx,uint sy){
 if(!pflags)return;
 uint tid=__builtin_amdgcn_workitem_id_x(),first=__builtin_amdgcn_workgroup_id_x()*16;
 if(tid<16){uint p=first+tid;if(p<tokens){int x=int(p%workw)-int(sx),y=int(p/workw)-int(sy);
  if(x>=0&&y>=0&&x<int(width)&&y<int(height)){uint idx=((uint(y)+psy)/8)*(pww/8)+(uint(x)+psx)/8;
   while(__hip_atomic_load(pflags+idx,__ATOMIC_RELAXED,__HIP_MEMORY_SCOPE_AGENT)<pepoch)__builtin_amdgcn_s_sleep(1);}}}
 __builtin_amdgcn_s_barrier();
}
DEV void pdl_publish(uint*flags,uint slot){
 __builtin_amdgcn_fence(__ATOMIC_RELEASE,"agent");
 if(__builtin_amdgcn_workitem_id_x()%32==0)__hip_atomic_fetch_add(flags+slot,1u,__ATOMIC_RELAXED,__HIP_MEMORY_SCOPE_AGENT);
}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_g128_qkv_bytein_fb_pdl(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy,const uint*pflags,uint pepoch,uint pww,uint psx,uint psy,uint*fflags,uint fepoch){pdl_ffn_wait(pflags,pepoch,pww,psx,psy,tokens,width,height,workw,sx,sy);mh_ffn_qkv_body<128,false,true,false,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);pdl_publish(fflags,__builtin_amdgcn_workgroup_id_x());}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_g128_qkv_fb_pdl(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy,const uint*pflags,uint pepoch,uint pww,uint psx,uint psy,uint*fflags,uint fepoch){pdl_ffn_wait(pflags,pepoch,pww,psx,psy,tokens,width,height,workw,sx,sy);mh_ffn_qkv_body<128,false,true,false,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);pdl_publish(fflags,__builtin_amdgcn_workgroup_id_x());}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_mapped_g128_qkv_bytein_fb_pdl(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy,const uint*pflags,uint pepoch,uint pww,uint psx,uint psy,uint*fflags,uint fepoch){pdl_ffn_wait(pflags,pepoch,pww,psx,psy,tokens,width,height,workw,sx,sy);mh_ffn_qkv_body<128,false,true,true,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);pdl_publish(fflags,__builtin_amdgcn_workgroup_id_x());}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_ffn_fused_c128_project_mapped_g128_qkv_fb_pdl(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy,const uint*pflags,uint pepoch,uint pww,uint psx,uint psy,uint*fflags,uint fepoch){pdl_ffn_wait(pflags,pepoch,pww,psx,psy,tokens,width,height,workw,sx,sy);mh_ffn_qkv_body<128,false,true,true,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);pdl_publish(fflags,__builtin_amdgcn_workgroup_id_x());}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_frag_project_g128_qkv_bytein_fb_pdl(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy,const uint*pflags,uint pepoch,uint pww,uint psx,uint psy,uint*fflags,uint fepoch){pdl_ffn_wait(pflags,pepoch,pww,psx,psy,tokens,width,height,workw,sx,sy);mh_ffn_qkv_body<256,false,true,false,true,true,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);pdl_publish(fflags,__builtin_amdgcn_workgroup_id_x());}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_frag_project_g128_qkv_fb_pdl(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy,const uint*pflags,uint pepoch,uint pww,uint psx,uint psy,uint*fflags,uint fepoch){pdl_ffn_wait(pflags,pepoch,pww,psx,psy,tokens,width,height,workw,sx,sy);mh_ffn_qkv_body<256,false,true,false,true,false,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);pdl_publish(fflags,__builtin_amdgcn_workgroup_id_x());}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_frag_project_mapped_g128_qkv_bytein_fb_pdl(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy,const uint*pflags,uint pepoch,uint pww,uint psx,uint psy,uint*fflags,uint fepoch){pdl_ffn_wait(pflags,pepoch,pww,psx,psy,tokens,width,height,workw,sx,sy);mh_ffn_qkv_body<256,false,true,true,true,true,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);pdl_publish(fflags,__builtin_amdgcn_workgroup_id_x());}
KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_ffn_fused_c256_frag_project_mapped_g128_qkv_fb_pdl(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy,const uint*pflags,uint pepoch,uint pww,uint psx,uint psy,uint*fflags,uint fepoch){pdl_ffn_wait(pflags,pepoch,pww,psx,psy,tokens,width,height,workw,sx,sy);mh_ffn_qkv_body<256,false,true,true,true,false,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);pdl_publish(fflags,__builtin_amdgcn_workgroup_id_x());}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_g128_qkv_bytein_fb_pdl(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy,const uint*pflags,uint pepoch,uint pww,uint psx,uint psy,uint*fflags,uint fepoch){pdl_ffn_wait(pflags,pepoch,pww,psx,psy,tokens,width,height,workw,sx,sy);mh_ffn_qkv_body<64,false,true,false,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);pdl_publish(fflags,__builtin_amdgcn_workgroup_id_x());}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_g128_qkv_fb_pdl(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy,const uint*pflags,uint pepoch,uint pww,uint psx,uint psy,uint*fflags,uint fepoch){pdl_ffn_wait(pflags,pepoch,pww,psx,psy,tokens,width,height,workw,sx,sy);mh_ffn_qkv_body<64,false,true,false,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);pdl_publish(fflags,__builtin_amdgcn_workgroup_id_x());}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_mapped_g128_qkv_bytein_fb_pdl(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy,const uint*pflags,uint pepoch,uint pww,uint psx,uint psy,uint*fflags,uint fepoch){pdl_ffn_wait(pflags,pepoch,pww,psx,psy,tokens,width,height,workw,sx,sy);mh_ffn_qkv_body<64,false,true,true,true,true,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);pdl_publish(fflags,__builtin_amdgcn_workgroup_id_x());}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_ffn_fused_c64_project_mapped_g128_qkv_fb_pdl(const float*in,const float*w,const float*aw,unsigned char*out,unsigned char*norm,uint tokens,uint width,uint height,uint workw,uint sx,uint sy,const uint*pflags,uint pepoch,uint pww,uint psx,uint psy,uint*fflags,uint fepoch){pdl_ffn_wait(pflags,pepoch,pww,psx,psy,tokens,width,height,workw,sx,sy);mh_ffn_qkv_body<64,false,true,true,true,false,true>(in,w,out,tokens,width,height,workw,sx,sy,aw,norm);pdl_publish(fflags,__builtin_amdgcn_workgroup_id_x());}
#endif

KERNEL __attribute__((amdgpu_flat_work_group_size(64,64))) void mh_pool_project_group_c64_f8w16(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<64,false,false,false,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}

KERNEL __attribute__((amdgpu_flat_work_group_size(64,64))) void mh_pool_project_group_c64_out8_f8w16(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<64,false,false,true,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}

KERNEL __attribute__((amdgpu_flat_work_group_size(64,64))) void mh_pool_project_group_c64_hin_f8w16(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<64,false,true,false,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}

KERNEL __attribute__((amdgpu_flat_work_group_size(64,64))) void mh_pool_project_group_c64_hin_out8_f8w16(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<64,false,true,true,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}

KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_pool_project_group_c128_f8w16(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<128,false,false,false,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}

KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_pool_project_group_c128_out8_f8w16(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<128,false,false,true,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}

KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_pool_project_group_c128_hin_f8w16(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<128,false,true,false,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}

KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void mh_pool_project_group_c128_hin_out8_f8w16(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<128,false,true,true,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}

KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_pool_project_group_c256_f8w16(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<256,false,false,false,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}

KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void mh_pool_project_group_c256_out8_f8w16(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<256,false,false,true,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}

KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_pool_project_group_c512_f8w16(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<512,false,false,false,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}

KERNEL __attribute__((amdgpu_flat_work_group_size(512,512))) void mh_pool_project_group_c512_out8_f8w16(const float*raw,const float*w,float*output,uint width,uint height,uint source_width,uint valid_width,uint valid_height){mh_pool_project_group_body<512,false,false,true,true>(raw,w,output,width,height,source_width,valid_width,valid_height);}

// C512 QKV+normalize, 32 tokens per wave (opt-in DLSS5_HIP_C512_M32, module c512-m32-mh; results/c512-ffn-20260926).
// mh_qkv_normalize_frag_c512 re-reads its 64 weight columns for every 16 tokens (one B fragment per WMMA); here one wave
// owns two 16-token A tiles, so each B fragment feeds two WMMAs and weight L2 traffic halves. Per-output K order, row
// sums, inv and q8(F()) are unchanged; the byte epilogue goes through a 16x64 LDS tile and two 128-bit stores per lane
// (production: 32 global_store_b8). Output identical by construction and verified bit-exact.
using c5u4=unsigned __attribute__((ext_vector_type(4)));
FASTWAVE void mh_qkv_normalize_frag_c512_m32(const unsigned char*in8,const float*w,unsigned char*out,uint tokens){
 const uint C=512,N=1536;uint l=__builtin_amdgcn_workitem_id_x(),rc=l&15,g=l>>4,block=__builtin_amdgcn_workgroup_id_x(),first=block/24*32,col=block%24*64;if(first>=tokens)return;
 const bool two=first+16<tokens;
 const unsigned char*wb=reinterpret_cast<const unsigned char*>(w);f8 acc0[4]{},acc1[4]{};
 for(uint gs=0;gs<16;gs++){const unsigned char*t0=in8+((first/16)*16+gs)*512+rc*32+g*8,*t1=t0+16*512;
  for(uint h=0;h<2;h++){i2 a0,a1{};__builtin_memcpy(&a0,t0+h*16,8);if(two)__builtin_memcpy(&a1,t1+h*16,8);
   for(uint n=0;n<4;n++){uint n0=col+n*16+rc;i2 b;__builtin_memcpy(&b,wb+((n0/16)*16+gs)*512+((h*2+g)*16+n0%16)*8,8);
    acc0[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a0,b,acc0[n]);acc1[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a1,b,acc1[n]);}}}
 __attribute__((shared)) float raw[17*65];
 __attribute__((shared)) c5u4 tile[16*4];
 unsigned char*tb=reinterpret_cast<unsigned char*>(tile);
 uint part=col/C,head0=(col%C)/32,scale=4*C*C+(C/32)*4096;
 for(uint t=0;t<2;t++){if(t&&!two)break;const uint base=first+t*16;f8*acc=t?acc1:acc0;
  if(t){WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);}
  for(uint n=0;n<4;n++)for(uint e=0;e<8;e++)raw[(g*8+e)*65+n*16+rc]=acc[n][e];
  WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);
  {uint row=l%16,h=l/16;float inv=1.f;if(base+row<tokens&&part<2){float ss=0.f;for(uint j=0;j<32;j++){float v=raw[row*65+h*32+j];ss+=v*v;}inv=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?w[scale+head0+h]:1.f);}raw[16*65+l]=inv;}
  WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);
  for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint row=g*8+e;tb[row*64+n*16+rc]=static_cast<unsigned char>(q8(F(acc[n][e]*raw[16*65+(n/2)*16+row])));}
  WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);
  {uint row=l>>1,half=l&1,p=base+row;if(p<tokens){c5u4*d=reinterpret_cast<c5u4*>(out+size_t(p)*N+col+half*32);d[0]=tile[row*4+half*2];d[1]=tile[row*4+half*2+1];}}
 }
}

// C512 QKV/normalization/attention fusion. Appended to multihead_fast_padded.hip.
// The host supplies the same tiled FFN feature and fragment-packed QKV weights
// as mh_qkv_normalize_frag_c512_m32; AV remains raster FP8 for the old projection.
#ifndef C512_FUSED_QKV_ATTN
#define C512_FUSED_QKV_ATTN 0
#endif
#ifndef C512_FUSED_WAVES
#define C512_FUSED_WAVES 2
#endif
#ifndef C512_FUSED_DEBUG
#define C512_FUSED_DEBUG 0
#endif
#if C512_FUSED_QKV_ATTN
#if C512_FUSED_WAVES != 2 && C512_FUSED_WAVES != 4
#error C512_FUSED_WAVES must be 2 or 4
#endif
DEV uint c5f_pixel(uint win,uint tok,uint width){return ((win/(width/8))*8+tok/8)*width+(win%(width/8))*8+tok%8;}
DEV float c5f_shuffle(float v,uint src){return __builtin_bit_cast(float,__builtin_amdgcn_ds_bpermute(src*4,__builtin_bit_cast(int,v)));}
DEV float c5f_serial_norm(f8 lo,f8 hi){
 // Channel order is 0..7,8..15,16..23,24..31, as in the production raw-LDS loop.
 // Both half-waves execute, but only the correct prefix is carried to the next half.
 uint row=__builtin_amdgcn_workitem_id_x()&15u;float s=0.f;
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=lo[e]*lo[e];
 s=c5f_shuffle(s,row);
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=lo[e]*lo[e];
 s=c5f_shuffle(s,row+16);
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=hi[e]*hi[e];
 s=c5f_shuffle(s,row);
 _Pragma("unroll 8") for(uint e=0;e<8;e++)s+=hi[e]*hi[e];
 return c5f_shuffle(s,row+16);
}
// Keep C512 attention's own clamp/F/byte semantics (not the C256 bounded reciprocal).
DEV float c5f_clamp(float a,float lo,float hi){float b=a>lo?a:lo;return b<hi?b:hi;}
DEV uint c5f_fp8(float x){return uint(__builtin_amdgcn_cvt_pk_fp8_f32(__builtin_amdgcn_fmed3f(x,-448.f,448.f),0.f,0,false))&255u;}
DEV float c5f_F(float x){float c=__builtin_amdgcn_fmed3f(x+0.f,-448.f,448.f);return __builtin_amdgcn_cvt_f32_fp8(__builtin_amdgcn_cvt_pk_fp8_f32(c,0.f,0,false),0);}
DEV void c5f_bits(i2&v,uint e,uint b){v[e/4]=int(uint(v[e/4])|((b&255u)<<(8*(e%4))));}
DEV i2 c5f_load(const unsigned char*p,uint row,uint col){i2 a;__builtin_memcpy(&a,p+row*32+col,8);return a;}

template<bool DebugNorm>
DEV void c5f_body(const unsigned char*in8,const float*w,unsigned char*out,uint width,uint height){
 constexpr uint C=512,NW=C512_FUSED_WAVES,QT=4/NW;
 uint tid=__builtin_amdgcn_workitem_id_x(),lane=tid&31u,row=lane&15u,half=lane>>4,wave=tid/32;
 uint wh=__builtin_amdgcn_workgroup_id_x(),windows=(width/8)*(height/8),win=wh/16,head=wh%16;
 if(width%8||height%8||wh>=windows*16)return;
 __attribute__((shared)) unsigned char packed[3*64*32];
 const unsigned char*wb=reinterpret_cast<const unsigned char*>(w);
 // Each wave owns QT independent 16-token tiles, sharing B-weight fragments.
 // Swapping WMMA operands transposes the result: lane row is now the token,
 // allowing serial channel normalization without a float scratch tile.
 _Pragma("clang loop unroll(disable)") for(uint part=0;part<3;part++){
  f8 acc[QT][2]{};
  for(uint gs=0;gs<16;gs++)for(uint h=0;h<2;h++){
   i2 a[QT];
   _Pragma("unroll") for(uint t=0;t<QT;t++){
    uint p=c5f_pixel(win,(wave*QT+t)*16+row,width);
    __builtin_memcpy(&a[t],in8+((p/16)*16+gs)*512+(p%16)*32+half*8+h*16,8);
   }
   _Pragma("unroll 2") for(uint ci=0;ci<2;ci++){
    uint n0=part*C+head*32+ci*16+row;i2 b;
    __builtin_memcpy(&b,wb+((n0/16)*16+gs)*512+((h*2+half)*16+n0%16)*8,8);
    _Pragma("unroll") for(uint t=0;t<QT;t++)acc[t][ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a[t],acc[t][ci]);
   }
  }
  _Pragma("unroll") for(uint t=0;t<QT;t++){
   float inv=1.f;
   if(part<2){float ss=c5f_serial_norm(acc[t][0],acc[t][1]);inv=__builtin_amdgcn_rsqf(maxf(ss,6.198883056640625e-5f))*(part==0?w[4*C*C+16*4096+head]:1.f);}
   _Pragma("unroll 2") for(uint ci=0;ci<2;ci++){
    i2 a{};_Pragma("unroll 8") for(uint e=0;e<8;e++)c5f_bits(a,e,q8(F(acc[t][ci][e]*inv)));
    uint tok=(wave*QT+t)*16+row;__builtin_memcpy(packed+(part*64+tok)*32+ci*16+half*8,&a,8);
   }
  }
 }
 WG_FENCE(3);__builtin_amdgcn_s_barrier();WG_FENCE(2);
 if constexpr(DebugNorm){
  for(uint i=tid;i<3*64*32;i+=NW*32){uint part=i/(64*32),tok=(i/32)%64,col=i%32,p=c5f_pixel(win,tok,width);out[(p*3+part)*512+head*32+col]=packed[i];}
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
    i2 q=c5f_load(packed,first+row,kt*16+half*8);
    i2 k=c5f_load(packed,64+key*16+row,kt*16+half*8);
    scores=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(k,q,scores);
   }
   h8 ex{};
   _Pragma("unroll 8") for(uint e=0;e<8;e++){
    uint q=first+row,k=key*16+half*8+e;
    float score=scores[e]+w[4*C*C+head*4096+q*64+k];
    float affine=c5f_clamp(score*.044921875f+1.30078125f,1.03125f,1.5693359375f);
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
   i2 a{};_Pragma("unroll 8") for(uint e=0;e<8;e++){float inv=1.f/(sums[0][e]+sums[1][e]);c5f_bits(a,e,c5f_fp8(float(exponent[key][e])*inv));}prob[key]=a;
  }
  _Pragma("unroll 2") for(uint ci=0;ci<2;ci++){
   f8 av{};
   _Pragma("unroll 4") for(uint kt=0;kt<4;kt++){
    i2 v{};_Pragma("unroll 8") for(uint e=0;e<8;e++)c5f_bits(v,e,packed[(128+kt*16+half*8+e)*32+ci*16+row]);
    av=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(v,prob[kt],av);
   }
   i2 bytes{};_Pragma("unroll 8") for(uint e=0;e<8;e++)c5f_bits(bytes,e,c5f_fp8(c5f_F(av[e])));
   uint p=c5f_pixel(win,first+row,width);__builtin_memcpy(out+p*512+head*32+ci*16+half*8,&bytes,8);
  }
 }
}
KERNEL __attribute__((amdgpu_flat_work_group_size(C512_FUSED_WAVES*32,C512_FUSED_WAVES*32)))
void c512_qkv_attention_fused(const unsigned char*in8,const float*w,unsigned char*out,uint width,uint height){c5f_body<false>(in8,w,out,width,height);}
#if C512_FUSED_DEBUG
KERNEL __attribute__((amdgpu_flat_work_group_size(C512_FUSED_WAVES*32,C512_FUSED_WAVES*32)))
void c512_qkv_fused_debug(const unsigned char*in8,const float*w,unsigned char*out,uint width,uint height){c5f_body<true>(in8,w,out,width,height);}
#endif
#endif

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

template<bool DebugNorm,bool W16=false,bool ProbUnclamped=false>
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
   uint pk[4];
   _Pragma("unroll") for(uint e=0;e<4;e++){
    uint q=first+row,k=key*16+half*8+2*e;
    float s0=scores[2*e]+w[4*C*C+head*4096+q*64+k],s1=scores[2*e+1]+w[4*C*C+head*4096+q*64+k+1];
    float af0=__builtin_amdgcn_fmed3f(s0*.044921875f+1.30078125f,1.03125f,1.5693359375f),af1=__builtin_amdgcn_fmed3f(s1*.044921875f+1.30078125f,1.03125f,1.5693359375f);
    pk[e]=__builtin_amdgcn_perm(bits(af1),bits(af0),0x06050201u)&0x7fe07fe0u;
   }
   exponent[key]=__builtin_bit_cast(h8,pk);
  }
  h8 ones{};_Pragma("unroll 8") for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;
  f8 sums[2]{};
  _Pragma("unroll 2") for(uint side=0;side<2;side++){
   _Pragma("unroll 2") for(uint h=0;h<2;h++)sums[side]=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,exponent[side+2*h],sums[side]);
  }
  i2 prob[4];
  _Pragma("unroll 4") for(uint key=0;key<4;key++){
#if C512_PAIR_PACK
   f8 pv{};_Pragma("unroll 8") for(uint e=0;e<8;e++){float inv=c5c_inv(sums[0][e]+sums[1][e]);pv[e]=ProbUnclamped?float(exponent[key][e])*inv:c5c_pair_plain_input(float(exponent[key][e])*inv);}prob[key]=c5c_pack8_pairs(pv);
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
KERNEL __attribute__((amdgpu_flat_work_group_size(C512_COMPACT_WAVES*32,C512_COMPACT_WAVES*32)))
void c512_qkv_attention_compact_w16_prob(const unsigned char*in8,const float*w,unsigned char*out,uint validwidth,uint validheight,uint workwidth,uint workheight,uint sx,uint sy){c5c_body<false,true,true>(in8,w,out,validwidth,validheight,workwidth,workheight,sx,sy);}
#endif

