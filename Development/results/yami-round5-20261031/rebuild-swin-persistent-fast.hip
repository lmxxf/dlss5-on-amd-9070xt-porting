#define HIP_ISA_HALF 1
#define W2_UP_FUSED 1
#define W2_FFN_QT_SMALL_MASK 3
#define W2_FFN_QT_BATCH 2
#define W2_BOUNDED_RCP 1
#define HIP_PREPACKED_WEIGHTS 1
#define HIP_FFN_HOIST_RES 2
#define HIP_PDL_KERNELS 0
#define W2_FRAGMENT_WEIGHTS 1
#define W2_LAUNDER_QKV 1
#define W2_SCHED_FENCE 1
#define W2_ROLL_QUERY 1
#define W2_HIDDEN_TILES 2
#define W2_PACK8 6
#define HIP_FMED3_CLAMP 1
#define W2_BYTE_INPUT_LOADS 1
#define W2_RTZ_PAIR 1
#define W2_DIRECT_COORDS 1
#define W2_Q8_MASK 1
#define W2_FFN_W16 1
#define W2_EXPLICIT_WINDOW 1
#define W2_NO_EXPORTS 1
#define HIP_SWIN_PERSISTENT_KERNELS 1
#define W2_FAST_NUM 3
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

#ifndef W2_SINGLE_PLANE_C256
#define W2_SINGLE_PLANE_C256 0
#endif
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
   uint pk[4];
   _Pragma("unroll") for(uint e=0;e<4;e++){
    uint row=qt*16+rc,k=key*16+gr*8+2*e;
    float s0=scores[2*e]+aw[4*C*C+head*4096+row*64+k],s1=scores[2*e+1]+aw[4*C*C+head*4096+row*64+k+1];
    float af0=clampf(s0*.044921875f+1.30078125f,1.03125f,1.5693359375f),af1=clampf(s1*.044921875f+1.30078125f,1.03125f,1.5693359375f);
    pk[e]=__builtin_amdgcn_perm(bits(af1),bits(af0),0x06050201u)&0x7fe07fe0u;
   }
   exponent[key]=__builtin_bit_cast(h8,pk);
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
#if defined(__gfx1201__) && W2_SINGLE_PLANE_C256
template<uint C,bool ByteIn,bool ByteOut,bool Up=false
#if W2_EXPLICIT_WINDOW
 ,bool ExplicitWindow=false
#endif
 ,bool W16=false,bool LowByte=false,bool HalfOut=false,bool SkipHalf=false,bool SkipByte=false,bool SkipByteOut=false>
DEV void swin_wave2_one_plane_body(const float*in,const float*fw,const float*aw,void*out_any,
 uint width,uint height,uint workw,uint workh,uint sx,uint sy,uint post,const float*upw=nullptr,const float*skip=nullptr
#if W2_EXPLICIT_WINDOW
 ,uint supplied_win=0
#endif
 ){
#if W2_EXPLICIT_WINDOW
 const uint win=ExplicitWindow?supplied_win:__builtin_amdgcn_workgroup_id_x(),head=__builtin_amdgcn_workitem_id_x()/32;
#else
 const uint win=__builtin_amdgcn_workgroup_id_x();uint head=__builtin_amdgcn_workitem_id_x()/32;
#endif
 uint rc=w2_rc(),gr=w2_gr();
 if(win>=(workw/8)*(workh/8))return;
 // R5: stage phase outputs in registers and reuse one LDS plane after uniform barriers.
 constexpr bool w2_one_plane=C==256&&!Up&&!W2_EXPLICIT_WINDOW&&!W2_DEFER_Q&&!W2_PHASE_REP;
 __attribute__((shared)) unsigned char plane0[64*C],plane1_backing[64*C];
 auto &plane1=w2_one_plane?plane0:plane1_backing;
 i2 one_ffn[4][2],one_mix[4][2];
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
   if constexpr(w2_one_plane)one_ffn[qb+qi][ci]=a;else w2_store<C>(plane1,qb+qi,head*2+ci,a);
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
  for(uint ci=0;ci<2;ci++){W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,w2_hmask(contract[ci][e]));W2_Q8_END(a);if constexpr(w2_one_plane)one_ffn[qt][ci]=a;else w2_store<C>(plane1,qt,head*2+ci,a);}
#else
  for(uint ci=0;ci<2;ci++){f8 rounded=w2_rtz8(contract[ci]);W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,rounded[e]);W2_Q8_END(a);if constexpr(w2_one_plane)one_ffn[qt][ci]=a;else w2_store<C>(plane1,qt,head*2+ci,a);}
#endif
#else
  for(uint ci=0;ci<2;ci++){W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,Hrtz(contract[ci][e]));W2_Q8_END(a);if constexpr(w2_one_plane)one_ffn[qt][ci]=a;else w2_store<C>(plane1,qt,head*2+ci,a);}
#endif
 }
 }
W2_REP_END
 if constexpr(w2_one_plane){w2_sync();_Pragma("unroll") for(uint qt=0;qt<4;qt++)_Pragma("unroll") for(uint ci=0;ci<2;ci++)w2_store<C>(plane0,qt,head*2+ci,one_ffn[qt][ci]);}
 w2_sync();
W2_REP_BEGIN(2)
 uint t256=__builtin_amdgcn_workitem_id_x();__asm__ volatile("":"+v"(t256));rc=t256&15u;
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
  for(uint ci=0;ci<2;ci++){W2_Q8_DECL(a);for(uint e=0;e<8;e++)W2_Q8_SET(a,e,result[ci][e]);W2_Q8_END(a);if constexpr(w2_one_plane)one_mix[qt][ci]=a;else w2_store<C>(plane0,qt,head*2+ci,a);}
 }
W2_REP_END
 if constexpr(w2_one_plane){w2_sync();_Pragma("unroll") for(uint qt=0;qt<4;qt++)_Pragma("unroll") for(uint ci=0;ci<2;ci++)w2_store<C>(plane0,qt,head*2+ci,one_mix[qt][ci]);}
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
 i2 one_residual[4][2];
 if constexpr(w2_one_plane){_Pragma("unroll") for(uint qt=0;qt<4;qt++)_Pragma("unroll") for(uint ci=0;ci<2;ci++)one_residual[qt][ci]=w2_load<C>(plane0,qt,head*2+ci);w2_sync();}
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
   uint pk[4];
   _Pragma("unroll") for(uint e=0;e<4;e++){
    uint row=qt*16+rc,k=key*16+gr*8+2*e;
    float s0=scores[2*e]+aw[4*C*C+head*4096+row*64+k],s1=scores[2*e+1]+aw[4*C*C+head*4096+row*64+k+1];
    float af0=clampf(s0*.044921875f+1.30078125f,1.03125f,1.5693359375f),af1=clampf(s1*.044921875f+1.30078125f,1.03125f,1.5693359375f);
    pk[e]=__builtin_amdgcn_perm(bits(af1),bits(af0),0x06050201u)&0x7fe07fe0u;
   }
   exponent[key]=__builtin_bit_cast(h8,pk);
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
#if !defined(W2_ATTENTION_ONLY)
   if constexpr(w2_one_plane)a=one_residual[qt][ci];else
#endif
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
#if !defined(W2_ATTENTION_ONLY)
   if constexpr(w2_one_plane)a=one_residual[qt][ci];else
#endif
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


#undef W2_KERNEL
#define W2_KERNEL(NAME,C,BI,BO) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post){if constexpr(C==256)swin_wave2_one_plane_body<C,BI,BO>(in,fw,aw,out,w,h,ww,hh,sx,sy,post);else swin_wave2_body<C,BI,BO>(in,fw,aw,out,w,h,ww,hh,sx,sy,post);}
#endif

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
#if defined(__gfx1201__) && W2_SINGLE_PLANE_C256
#define W2_KERNEL_W16(NAME,C,BI,BO) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post){if constexpr(C==256)swin_wave2_one_plane_body<C,BI,BO,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post);else swin_wave2_body<C,BI,BO,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post);}
#else
#define W2_KERNEL_W16(NAME,C,BI,BO) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post){swin_wave2_body<C,BI,BO,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post);}
#endif
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
#if defined(__gfx1201__) && W2_SINGLE_PLANE_C256
#define W2_KERNEL_HO(NAME,C) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post){if constexpr(C==256)swin_wave2_one_plane_body<C,true,false,false,false,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post);else swin_wave2_body<C,true,false,false,false,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post);}
#else
#define W2_KERNEL_HO(NAME,C) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post){swin_wave2_body<C,true,false,false,false,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post);}
#endif
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
#if defined(__gfx1201__) && W2_SINGLE_PLANE_C256
#define W2_KERNEL_HOB(NAME,C) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post,void*sb){if constexpr(C==256)swin_wave2_one_plane_body<C,true,false,false,false,false,true,false,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post,nullptr,static_cast<const float*>(sb));else swin_wave2_body<C,true,false,false,false,false,true,false,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post,nullptr,static_cast<const float*>(sb));}
#else
#define W2_KERNEL_HOB(NAME,C) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post,void*sb){swin_wave2_body<C,true,false,false,false,false,true,false,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post,nullptr,static_cast<const float*>(sb));}
#endif
W2_KERNEL_HOB(c64_wave2_bi_hob,64)
W2_KERNEL_HOB(c128_wave2_bi_hob,128)
#define W2_UP_KERNEL_SB(NAME,C) KERNEL __attribute__((amdgpu_flat_work_group_size(C,C))) void NAME(const float*in,const float*fw,const float*aw,void*out,uint w,uint h,uint ww,uint hh,uint sx,uint sy,uint post,const float*upw,const float*skip){swin_wave2_body<C,true,true,true,false,true,false,false,true>(in,fw,aw,out,w,h,ww,hh,sx,sy,post,upw,skip);}
W2_UP_KERNEL_SB(c64_wave2_up_lb_sb,64)
W2_UP_KERNEL_SB(c128_wave2_up_lb_sb,128)
#endif
#endif
#endif
#endif


// Identical host/device ABI. No HIP SDK headers required.
struct SpLayer {
 const void *in,*fw,*aw; void *out;
 unsigned w,h,ww,hh,sx,sy,post,pad;
};
struct SpNode { unsigned layer,window,need,nchild,child[4]; };
struct SpParams {
 SpLayer layers[8]; const SpNode *nodes; unsigned *state,*error_host;
 unsigned total,first,base,fault,layers_count,pad;
 unsigned long long timeout_ticks;
};
static_assert(sizeof(SpLayer)==64,"SpLayer ABI");
static_assert(sizeof(SpNode)==32,"SpNode ABI");
static_assert(sizeof(SpParams)==568,"SpParams ABI");

#ifndef HIP_SWIN_PERSISTENT_KERNELS
#define HIP_SWIN_PERSISTENT_KERNELS 0
#endif
#if HIP_SWIN_PERSISTENT_KERNELS
// State: head, tail, abort, timeout-count; counters[total], queue[total],
// per-workgroup task broadcast[total]. All storage is device memory.
DEV uint sp_load(const uint *p){return __hip_atomic_load(p,__ATOMIC_ACQUIRE,__HIP_MEMORY_SCOPE_AGENT);}
DEV uint sp_poll(const uint *p){return __hip_atomic_load(p,__ATOMIC_RELAXED,__HIP_MEMORY_SCOPE_AGENT);}
DEV void sp_store(uint*p,uint x){__hip_atomic_store(p,x,__ATOMIC_RELEASE,__HIP_MEMORY_SCOPE_AGENT);}
DEV void sp_fail(SpParams p,uint code){
 uint old=__hip_atomic_exchange(p.state+2,code,__ATOMIC_ACQ_REL,__HIP_MEMORY_SCOPE_AGENT);
 if(!old)__hip_atomic_fetch_add(p.error_host,1u,__ATOMIC_RELEASE,__HIP_MEMORY_SCOPE_SYSTEM);
}
DEV void sp_global_group_sync(){
 __builtin_amdgcn_fence(__ATOMIC_RELEASE,"agent");
 __builtin_amdgcn_s_barrier();
 __builtin_amdgcn_fence(__ATOMIC_ACQUIRE,"agent");
}
KERNEL void sp_init(SpParams p){
 uint i=__builtin_amdgcn_workgroup_id_x()*256+__builtin_amdgcn_workitem_id_x();
 if(i<3*p.total)p.state[4+i]=0;
 if(i==0){p.state[0]=p.base;p.state[1]=p.base+p.first;p.state[2]=0;p.state[3]=0;}
}
// SP_INIT_PAIR (2026-10-01 bitexact-pm): one launch initializes this stage's queue state AND the other C256 stage's
// (encoder -> decoder): the decoder's state is a separate buffer whose last user is the previous frame, so its init has
// no data dependency on anything this frame and the decoder's own sp_init dispatch goes away. Same values as two sp_init.
#ifndef SP_INIT_PAIR
#define SP_INIT_PAIR 0
#endif
#if SP_INIT_PAIR
KERNEL void sp_init_pair(SpParams p,uint*state2,uint total2,uint base2,uint first2){
 uint i=__builtin_amdgcn_workgroup_id_x()*256+__builtin_amdgcn_workitem_id_x();
 if(i<3*p.total)p.state[4+i]=0;
 if(i<3*total2)state2[4+i]=0;
 if(i==0){p.state[0]=p.base;p.state[1]=p.base+p.first;p.state[2]=0;p.state[3]=0;state2[0]=base2;state2[1]=base2+first2;state2[2]=0;state2[3]=0;}
}
#endif
template<uint C,bool W16=false> DEV void sp_run_body(SpParams p){
 const uint tid=__builtin_amdgcn_workitem_id_x(),group=__builtin_amdgcn_workgroup_id_x();
 uint *counts=p.state+4,*queue=counts+p.total,*broadcast=queue+p.total;
 if(tid==0){
  uint selected=0;
  if(!sp_poll(p.state+2)){
   uint ticket=__hip_atomic_fetch_add(p.state,1u,__ATOMIC_RELAXED,__HIP_MEMORY_SCOPE_AGENT);
   uint slot=ticket-p.base;
   if(slot>=p.total)sp_fail(p,2);
   else if(slot<p.first)selected=slot+1;
   else {
    selected=sp_poll(queue+slot);
    if(!selected){
     unsigned long long start=__builtin_readsteadycounter();
     do {
      if(sp_poll(p.state+2))break;
      __builtin_amdgcn_s_sleep(2);
      selected=sp_poll(queue+slot);
      if(!selected&&__builtin_readsteadycounter()-start>=p.timeout_ticks){
       __hip_atomic_fetch_add(p.state+3,1u,__ATOMIC_RELAXED,__HIP_MEMORY_SCOPE_AGENT);
       sp_fail(p,1);break;
      }
     }while(!selected);
    }
    // A relaxed load followed by an acquire fence consumes the queue slot's
    // release publication without flushing caches on every unsuccessful poll.
    if(selected)__builtin_amdgcn_fence(__ATOMIC_ACQUIRE,"agent");
   }
  }
  sp_store(broadcast+group,selected);
 }
 sp_global_group_sync();
 uint selected=__builtin_amdgcn_readfirstlane(sp_load(broadcast+group));
 if(!selected)return; // Uniform across the complete workgroup.
 uint job=selected-1;
 if(job>=p.total){if(tid==0)sp_fail(p,2);return;}
 uint layer_index=__builtin_amdgcn_readfirstlane(p.nodes[job].layer);
 uint window=__builtin_amdgcn_readfirstlane(p.nodes[job].window);
 if(layer_index>=p.layers_count){if(tid==0)sp_fail(p,2);return;}
 SpLayer layer=p.layers[layer_index];
 // This function is generated verbatim from the production body except for
 // supplying the window ID rather than reading workgroup_id_x.
 swin_wave2_body<C,true,true,false,true,W16>(static_cast<const float*>(layer.in),
   static_cast<const float*>(layer.fw),static_cast<const float*>(layer.aw),
   layer.out,layer.w,layer.h,layer.ww,layer.hh,layer.sx,layer.sy,layer.post,nullptr,nullptr,window);
 // Every wave publishes all its output before the leader publishes readiness.
 sp_global_group_sync();
 if(tid==0&&!sp_load(p.state+2)){
  const SpNode &node=p.nodes[job];
  // Fault injection deliberately strands the next layer; used only in lab.
  if(p.fault==1&&node.layer==0)return;
  for(uint i=0;i<node.nchild;i++){
   if(i>=4||node.child[i]>=p.total){sp_fail(p,2);break;}
   uint child=node.child[i];
   uint old=__hip_atomic_fetch_add(counts+child,1u,__ATOMIC_ACQ_REL,__HIP_MEMORY_SCOPE_AGENT);
   uint need=p.nodes[child].need;
   if(old+1>need){sp_fail(p,2);break;}
   if(old+1==need){
    uint ticket=__hip_atomic_fetch_add(p.state+1,1u,__ATOMIC_RELAXED,__HIP_MEMORY_SCOPE_AGENT);
    uint slot=ticket-p.base;
    if(slot>=p.total){sp_fail(p,2);break;}
    sp_store(queue+slot,child+1);
   }
  }
 }
}
KERNEL __attribute__((amdgpu_flat_work_group_size(64,64))) void sp_run64(SpParams p){sp_run_body<64>(p);}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void sp_run128(SpParams p){sp_run_body<128>(p);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void sp_run256(SpParams p){sp_run_body<256>(p);}
#if W2_FFN_W16
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void sp_run256_w16(SpParams p){sp_run_body<256,true>(p);}
#endif

// One workgroup, no polling and no inter-workgroup dependency. Normally it
// exits immediately. After an aborted queue it replays layer-major from the
// untouched input, preserving every operation in the production window body.
template<uint C,bool W16=false> DEV void sp_recover_body(SpParams p){
 if(!sp_load(p.state+2))return;
 for(uint li=0;li<p.layers_count;li++){
  SpLayer layer=p.layers[li];uint windows=(layer.ww/8)*(layer.hh/8);
  for(uint win=0;win<windows;win++){
   swin_wave2_body<C,true,true,false,true,W16>(static_cast<const float*>(layer.in),
    static_cast<const float*>(layer.fw),static_cast<const float*>(layer.aw),
    layer.out,layer.w,layer.h,layer.ww,layer.hh,layer.sx,layer.sy,layer.post,nullptr,nullptr,win);
   sp_global_group_sync();
  }
 }
 if(__builtin_amdgcn_workitem_id_x()==0)__hip_atomic_fetch_add(p.error_host+1,1u,__ATOMIC_RELEASE,__HIP_MEMORY_SCOPE_SYSTEM);
}
KERNEL __attribute__((amdgpu_flat_work_group_size(64,64))) void sp_recover64(SpParams p){sp_recover_body<64>(p);}
KERNEL __attribute__((amdgpu_flat_work_group_size(128,128))) void sp_recover128(SpParams p){sp_recover_body<128>(p);}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void sp_recover256(SpParams p){sp_recover_body<256>(p);}
#if W2_FFN_W16
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void sp_recover256_w16(SpParams p){sp_recover_body<256,true>(p);}
#endif

#endif

