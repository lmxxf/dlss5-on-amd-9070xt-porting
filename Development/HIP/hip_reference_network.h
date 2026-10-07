#include "submit_pulse_hip.h"
#pragma once
// SDKless HIP host graph. Scalar numerical validation path, not a realtime backend.
#include "hip_api.h"
#include "../../src/native_experimental_history.h"
#include "packed_weights.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <deque>
#include <tuple>
#include <vector>
#include <algorithm>
#include <map>
#include <memory>
#include <limits>
#include <utility>
#include <functional>
#include <set>
#include <cmath>
#include <tuple>
#include <chrono>
#include <atomic>
#include <type_traits>
namespace hip_reference {
using U=uint32_t;using Api=hip_probe::Api;using Handle=hip_probe::Handle;
inline float Half(uint16_t h){U s=U(h&0x8000u)<<16,e=(h>>10)&31u,m=h&1023u,b;if(!e){if(!m)b=s;else{int sh=0;while(!(m&1024)){m<<=1;sh++;}b=s|(U(113-sh)<<23)|((m&1023)<<13);}}else b=s|((e==31?255:e+112)<<23)|(m<<13);float f;std::memcpy(&f,&b,4);return f;}
inline std::vector<char> ReadBytes(const std::string&path){std::ifstream f(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("missing "+path);auto n=f.tellg();if(n<=0)throw std::runtime_error("empty "+path);std::vector<char>b(static_cast<size_t>(n));f.seekg(0);if(!f.read(b.data(),n))throw std::runtime_error("read "+path);return b;}
inline std::vector<float> ReadWeights(const std::string&path){std::ifstream test(std::filesystem::u8path(path),std::ios::binary);bool full=bool(test);test.close();auto b=ReadBytes(full?path:path.substr(0,path.size()-4)+".f16");if(b.size()%(full?4:2))throw std::runtime_error("weight size "+path);std::vector<float>v(b.size()/(full?4:2));if(full)std::memcpy(v.data(),b.data(),b.size());else for(size_t i=0;i<v.size();i++){uint16_t h;std::memcpy(&h,b.data()+i*2,2);v[i]=Half(h);}return v;}
#ifndef HIP_ADDR_SKEW
#define HIP_ADDR_SKEW 0 /* 2026-10-02 ideas-yami-ikaruga: 1 = compile the DLSS5_HIP_ADDR_SKEW_* allocation offset scan (diagnostic) */
#endif
struct Allocation {Api*api;void*ptr{};size_t bytes,capacity;bool owned=true;
 // Sparse weights (Options::sparse_weights): ptr is a reserved address range of `bytes`; only `sparse_maps` (offset,size,handle) are backed. capacity = backed bytes.
 size_t sparse_reserved{};struct SparseMap{size_t offset,size;void*handle;};std::vector<SparseMap>sparse_maps;
 #if HIP_ADDR_SKEW
 size_t skew{};
 // 2026-10-02 ideas-yami-ikaruga (diagnostic, default compiled out): DLSS5_HIP_ADDR_SKEW_STRIDE/_MOD/_SEED/_KIND shift the i-th
 // plain allocation by ((i+seed)*stride)%mod bytes (kind bit0 = persistent weights, bit1 = tensors). Only physical spacing changes.
 static size_t SkewFor(){static const size_t st=std::getenv("DLSS5_HIP_ADDR_SKEW_STRIDE")?std::strtoull(std::getenv("DLSS5_HIP_ADDR_SKEW_STRIDE"),nullptr,0):0,
  md=std::getenv("DLSS5_HIP_ADDR_SKEW_MOD")?std::strtoull(std::getenv("DLSS5_HIP_ADDR_SKEW_MOD"),nullptr,0):0,sd=std::getenv("DLSS5_HIP_ADDR_SKEW_SEED")?std::strtoull(std::getenv("DLSS5_HIP_ADDR_SKEW_SEED"),nullptr,0):0,
  kd=std::getenv("DLSS5_HIP_ADDR_SKEW_KIND")?std::strtoull(std::getenv("DLSS5_HIP_ADDR_SKEW_KIND"),nullptr,0):3;
  static size_t counter[2]{};const int k=SkewPersistent()?0:1;if(!st||!md||!(kd&(1u<<k)))return 0;size_t i=counter[k]++;return ((i+sd)*st)%md/256*256;}
 static bool&SkewPersistent(){static bool v=false;return v;}
#endif
 Allocation(Api&a,size_t n):api(&a),bytes(n),capacity(n){if(!n)throw std::runtime_error("zero HIP allocation");
#if HIP_ADDR_SKEW
  skew=SkewFor();if(skew){a.Check(a.hipMalloc(&ptr,n+skew),"hipMalloc");ptr=static_cast<char*>(ptr)+skew;return;}
#endif
  a.Check(a.hipMalloc(&ptr,n),"hipMalloc");}Allocation(Api&a,void*p,size_t n):api(&a),ptr(p),bytes(n),capacity(n),owned(false){}
 Allocation(Api&a,size_t n,size_t granularity,const std::vector<std::pair<size_t,size_t>>&live):api(&a),bytes(n),capacity(0){
  sparse_reserved=(n+granularity-1)/granularity*granularity;a.Check(a.hipMemAddressReserve(&ptr,sparse_reserved,granularity,nullptr,0),"hipMemAddressReserve");
  try{hip_probe::MemAllocationProp prop{};prop.type=1;prop.location.type=1;hip_probe::MemAccessDesc acc{};acc.location.type=1;acc.flags=3;
   for(auto&r:live){SparseMap m{r.first,r.second,nullptr};a.Check(a.hipMemCreate(&m.handle,m.size,&prop,0),"hipMemCreate");
    if(int e=a.hipMemMap(static_cast<char*>(ptr)+m.offset,m.size,0,m.handle,0)){a.hipMemRelease(m.handle);a.Check(e,"hipMemMap");}
    sparse_maps.push_back(m);if(int e=a.hipMemSetAccess(static_cast<char*>(ptr)+m.offset,m.size,&acc,1))throw std::runtime_error("hipMemSetAccess rc="+std::to_string(e)+" offset="+std::to_string(m.offset)+" size="+std::to_string(m.size)+" reserved="+std::to_string(sparse_reserved)+" bytes="+std::to_string(n)+" maps="+std::to_string(sparse_maps.size()));capacity+=m.size;}
  }catch(...){ReleaseSparse();throw;}}
 void ReleaseSparse(){for(auto&m:sparse_maps){api->hipMemUnmap(static_cast<char*>(ptr)+m.offset,m.size);api->hipMemRelease(m.handle);}sparse_maps.clear();if(ptr)api->hipMemAddressFree(ptr,sparse_reserved);ptr=nullptr;}
 ~Allocation(){if(ptr&&owned){api->hipDeviceSynchronize();if(sparse_reserved)ReleaseSparse();else api->hipFree(
#if HIP_ADDR_SKEW
  static_cast<char*>(ptr)-skew
#else
  ptr
#endif
  );}}};
using Tensor=std::shared_ptr<Allocation>;
#ifndef HIP_C512_PAD16
#define HIP_C512_PAD16 1 /* 2026-09-30: see NewPad16; 0 = old mh_shift_pack path (results/shift-pack-900-20260930) */
#endif
#ifndef HIP_VIT_GATHER_FOLD_HOST
#define HIP_VIT_GATHER_FOLD_HOST 0 /* 2026-09-30 (bit-exact but 900 slower 3/3, not taken): head Down stores in ViT gather order and decoder39 reads through the inverse order when the modules export mh_pool_project_group_c512_gout / decoder_project2x_h16w_gin; 2 vit_gather launches fewer; older modules = old path (results/gap-fusion-20260930) */
#endif
#ifndef HIP_C256_FFN_W16
#define HIP_C256_FFN_W16 1 /* 2026-09-30: C256 FFN weights in the wide fragment layout, launched through the _w16 exports of c64-wave2 / swin-persistent when present; 0 or older modules = @ffn-frag layout + original kernels (results/c256-w16-20260930) */
#endif
#ifndef HIP_SMALL_FFN_W16
#define HIP_SMALL_FFN_W16 1 /* 2026-09-30: the same wide FFN fragments for C64/C128 (c64/c128_wave2*_w16, *_wave2_up_w16 in c64-wave2 when built with W2_FFN_W16_SMALL); 0 or modules without them = old layout (results/w16-c64-c128-20260930) */
#endif
#ifndef HIP_DEC_F8W
#define HIP_DEC_F8W 1 /* 2026-10-01: decoder Up39/Up48 on decoder_project2x_h16w*_f8 (E4M3 weights, fp8 WMMA) when deep_fast exports them (results/c512-qkv-pipeline-20261001 §11) */
#endif
#ifndef HIP_VIT_QKV_F8W
#define HIP_VIT_QKV_F8W 1 /* 2026-10-01: ViT QKV on vit_stream_qkv_frag_hin_w5f8 (E4M3 fragments, fp8 WMMA) when vit-stream exports it; bit-identical (results/c512-qkv-pipeline-20261001 §10) */
#endif
#ifndef HIP_VIT_ATTN_KSPLIT_HOST
#define HIP_VIT_ATTN_KSPLIT_HOST 0 /* 2026-10-01 rebuild-baseline: the fast-tier _ks2 probe (5a7cd0ba) made the exact-tier host 0.005..0.02ms slower
   in every ABBA round (per-frame HasFn miss + per-launch suffix compare); compiled out by default, 1 = fast-tier hosts */
#endif
#ifndef HIP_C512_PROJ_WN4
#define HIP_C512_PROJ_WN4 0 /* 2026-10-01 gap-map-evening: 1 = C512 attention projection on mh_attention_project_frag_c512_wn4 (128-thread WG, one wave per 16-column slice) when mh_fast exports it. Bit-exact but flat/slower, not taken */
#endif
#ifndef HIP_C512_FFN_F8W
#define HIP_C512_FFN_F8W 1 /* 2026-10-01: use split_ffn_one_w2f8 (E4M3 mix/expand weights, fp8 WMMA) when c512-m32-deep exports it; bit-identical (results/c512-qkv-pipeline-20261001) */
#endif
#ifndef HIP_C512_FFN_PROJ_FUSE
#define HIP_C512_FFN_PROJ_FUSE 0 /* 2026-10-02 ideas-yami-ikaruga: 1 = C512 FFN + FFN projection as one split_ffn_proj_fused dispatch when c512-m32-deep exports it (module C512_FFN_PROJ_FUSE); contract bytes stay in LDS */
#endif
#ifndef HIP_C512_PROJ_FB8
#define HIP_C512_PROJ_FB8 0 /* 2026-10-01: C512 attention projection reads the FFN projection's E4M3 tiles as its residual (split_projection_frag_nof32 + mh_attention_project_frag_c512_fb8, modules built with C512_PROJ_FB8) and the f32 copy is not written; 0 or older modules = f32 path (results/mochizuki-gap-20261001) */
#endif
#ifndef HIP_C512_FFN_ONE
#define HIP_C512_FFN_ONE 1 /* 2026-10-01: C512 mix+FFN as one dispatch (split_ffn_one_m32 in c512-m32-deep built with C512_FFN_ONE) when the module exports it; 0 or older modules = mix + split_ffn_fused_fp8_t8 (results/mochizuki-gap-20261001) */
#endif
#ifndef HIP_C32_RTZ_TALL
#define HIP_C32_RTZ_TALL 1 /* 2026-10-03: 1080 tier (1920x1152 / 1920x1088) loads c32-wave1-rtz.hsaco (same recipe + HIP_C32_RTZ_ISA 2, builtin Hrtz) under the c32_wave1 key when the file exists; 900/720/free geometry and missing file = c32-wave1.hsaco. Bit-exact (2^32 check); 900 kept old because one ABBA round was slower (results/ideas-yami-ikaruga-20261002) */
#endif
#ifndef HIP_C32_SKIP_BYTE
#define HIP_C32_SKIP_BYTE 1 /* 2026-09-30: block4 main (C32 skip, read only by the block66 up) as E4M3 bytes when c32-wave1 exports the _b8 pair; 0 or older modules = f32 (results/c32-align-20260930) */
#endif
#ifndef HIP_C32_PRE_DOWN_BYTE
#define HIP_C32_PRE_DOWN_BYTE 1 /* 2026-09-30: block0 pooled output (read only by block1) as E4M3 bytes when c32-wave1 exports c32_wave1_prefix_b8d/c32_wave1_mapped_b8; 0 or older modules = f32 (results/prefix-post-20260930) */
#endif
#ifndef HIP_C32_POST_LOW_BYTE
#define HIP_C32_POST_LOW_BYTE 1 /* 2026-09-30: block69 main (read only by the fused post) as E4M3 bytes when c32-wave1 exports c32_wave1_finish_b8/c32_wave1_post_b8; 0 or older modules = f32 */
#endif
#ifndef HIP_C32_DOWN_BYTE
#define HIP_C32_DOWN_BYTE 1 /* with HIP_C32_SKIP_BYTE: block4 pooled (dcrop) output also as E4M3 bytes, read by mh_pool_project_c32_b8, when both exports exist */
#endif
#ifndef HIP_C512_PAD16_POISON
#define HIP_C512_PAD16_POISON 0
#endif
inline std::set<U> ParseSkipBlocks(const std::string&s){std::set<U>out;size_t p=0;while(p<s.size()){size_t q=s.find(',',p);if(q==std::string::npos)q=s.size();auto word=s.substr(p,q-p);size_t used=0;unsigned long b=std::stoul(word,&used);if(used!=word.size()||!((b>=1&&b<=30)||(b>=31&&b<=38)||(b>=40&&b<=69)))throw std::runtime_error("unsupported skipped residual block");out.insert(U(b));p=q+1;}return out;}
// Shared by host initialization and the network options builder: do not load the
// 192 MiB noise table when the procedural fast prefix is selected.
/* DLSS5_STYLE (2026-10-01, Zero): NGX Style 0/1/2 -> preprocess feature 6 = Style/128. Unset, 1, or anything that is not exactly
   0/1/2 keeps the modules' built-in Style=1 (returns -1: no override, no extra HIP calls); a bad value falls back to 1 with a note. */
inline float StyleFeatureFromEnvironment(){const char*v=std::getenv("DLSS5_STYLE");if(!v||!*v)return -1;
 if(!std::strcmp(v,"0"))return 0.f;if(!std::strcmp(v,"2"))return 2.f/128.f;if(std::strcmp(v,"1"))std::fprintf(stderr,"DLSS5_STYLE=%s invalid (0/1/2), using 1\n",v);return -1;}
inline bool FastPrefixFromEnvironment(){const char*v=std::getenv("DLSS5_HIP_FAST");if(!v)return false;if(std::strcmp(v,"0")&&std::strcmp(v,"1"))throw std::runtime_error("DLSS5_HIP_FAST must be 0 or 1");return !std::strcmp(v,"1");}
// Consumer-owned policy. Empty overrides preserve the addon/environment behavior.
struct IntegrationOptions {
 bool allow_vit_hotkey=true,allow_input_poll=true;
 bool override_multi_pass_skip=false;std::set<U> multi_pass_skip;
 // Called only during module selection. Return a module stem without .hsaco.
 // The consumer owns its supported twin list and exact-module policy.
 std::function<std::string(const std::string&,bool,const std::string&)> select_module;
};
struct Options {unsigned submit_pulse=2; /* 0 off, 1 explicit same-arch experiment, 2 driver-scoped auto. Bridge only. */bool experimental_temporal=false;unsigned temporal_valid_height=0;bool temporal_feature_tap=false; /* Experimental MP1 diagnostic: retain post half32 for original-gate validation; default off. */bool swin_run=false;unsigned vit_stream=0; /* bit0: AV FP8, bit1: contract F16; explicit paired dispatch */bool wave_owned=false;bool c512_m32=false; /* 2026-09-26: C512 QKV+mix 32 tokens per wave (results/c512-ffn-20260926) */bool vit_proj_n64=false; /* 2026-09-26: ViT projection 16 tokens x 64 columns per wave (results/m32-sweep-20260926) */bool pdl=false; /* 2026-09-25: chain launches any-order + tile flags (results/pdl-chain-20260925) */ bool mh_ffn_frag256=false;bool decoder_byte=false;std::set<U>skip_blocks;U width=512,height=512,seed=0,post_shift=0;std::string assets,modules,dump_dir,dump_only;unsigned ffn_qkv_max_c=64,runtime=7,device=0/* HIP device index; the bridge picks the one matching the D3D12 adapter (multi-GPU / iGPU hosts) */,tiled_ffn_min_c=64,vit_weight_mask=0;bool mh_window_fused=false;/* 2026-09-17 experiment: C64 blocks as one kernel per 8x8 window (FFN+QKV+attention+projection, Development/HIP/c64_window_fused.hip, module c64-window-fused.hsaco) */bool mh_proj_diag_fb=false;/* 2026-09-17: residual scales as diagonal MMAs in the byte-feature attention-project kernels (c64/c128/c256 *_fb_diag) */bool sparse_weights=false;std::string sparse_filter;/* diagnostic: only keys ending with this go sparse */bool sparse_full=false;/* diagnostic: VMM path with every page backed (isolates mapping from dead-range accounting) *//* 2026-09-17 experiment, DO NOT SHIP: in-place packing leaves 3/4 of every E4M3 region (1/2 of every f16 region) dead in the f32 layout; this maps only the live 64 KiB pages of each weight through the VMM API (addresses unchanged; weights 607 -> 236 MiB). Copies work, but on driver 32.0.31007.2048 kernels hang (64 KiB chunks) or fail (2 MiB chunks) on any reservation backed by more than one physical chunk, and hipMemMap refuses partial mappings of one chunk; only reservation == one chunk is kernel-visible, which cannot skip holes. All three hashes change. Kept for re-testing on newer drivers (vmm_probe / vmm_kernel_probe). */bool vit_qkv_fused=false,ffn_qkv=false,grouped_mh_contract=false,direct_prefix_input=false,prefix_fused=false,mh_input_mapped=false,mh_project_crop=false,vit_qkv_blocked=false,split_project_blocked=false,split_mix_blocked=false,split_ffn_fused=false,fused_ffn_project=false,post_merge_fold=false,pre_main8=false,raw_chain=false,elide_identity_shift=false,fast_vit=false,wmma=false,pooled=false,profile=false,wall_profile=false,wave=false,tiled=false,fast_c32=false,fused_c32=false,fast_mh=false,fast_deep=false,fast_prefix=false,mh_wave=false,fused_ffn=false,fused_mh=false,packed_weights=false,packed_c32=false,fp8_normalized=false,fp8_ffn=false,fp8_av=false,fp8_deep=false,fp8_middle=false,half_c32=false,crop_c32=false,fused_qkv_norm=false,vit_pack_input=false,vit_contract_blocked=false,vit_blocked=false,vit_split_k=false,mapped_c32=false,fused_mh_ffn=false,tiled_mh_ffn=false,graph=false,vit_attn_fused=false,vit_qkv_fp8=false,mh_byte_stream=false,vit_expand_m4=false,vit_expand_frag=false,vit_byte_stream=false,vit_half_stream=false,vit_qkv_n4=false,ffn_qkv_batched_norm=false,pool_project_fused=false,qkv_norm_wave_c512=false,vit_expand_m2=false,vit_input_tiled=false,vit_ffn_fused=false,split_mix_fused=false,split_mix_h16w=false,pool_project_h16w=false,decoder_h16w=false,mh_attn_w16=false,c512_qkv_frag=false,c512_proj_frag=false,c512_proj_tiles=false,mh_feature_byte=false,mh_proj_diag=false,post_head_fused=false,c32_finish_fused=false,down_crop_fused=false,pool32_h16w=false,tiled_ffn_small=false,pool_project_group=false,prefix_inline=false,vit_proj_frag=false,vit_qkv_frag=false,vit_contract_frag=false,mh_ffn_frag=false;unsigned dup_count=2;std::string dup_prefix;/* diagnostic: launch matching kernels twice (pure kernels: identical output, frame delta = in-frame marginal cost) */IntegrationOptions integration;};
// 2026-09-26: C512 QKV/mix 32-token kernels (DLSS5_HIP_C512_M32) replace only the production h16w-mix + frag-QKV configuration.
// 2026-09-26: the ViT projection 64-column kernel (DLSS5_HIP_VIT_PROJ_N64) replaces only the production fragment projection.
inline bool VitProjN64Compatible(const Options&opt){return opt.vit_proj_n64&&opt.fast_deep&&opt.packed_weights&&opt.vit_proj_frag;}
inline bool VitStreamCompatible(const Options&o){return o.vit_stream&&o.vit_stream<=3&&VitProjN64Compatible(o)&&o.fast_vit&&o.vit_attn_fused&&o.vit_qkv_fp8&&o.vit_qkv_frag&&o.vit_contract_frag&&o.vit_contract_blocked&&!o.vit_byte_stream&&!o.vit_ffn_fused&&!o.vit_split_k;}
inline bool C512M32Compatible(const Options&opt){return opt.c512_m32&&opt.fast_deep&&opt.packed_weights&&opt.split_mix_h16w&&!opt.split_mix_fused&&opt.split_ffn_fused&&opt.c512_proj_tiles&&opt.c512_qkv_frag&&opt.fused_qkv_norm;}
// Opt-in recipe validated against prod8; incompatible diagnostic layouts retain the original path.
inline bool WaveOwnedCompatible(const Options&o){
 return o.wave_owned&&!o.graph&&std::none_of(o.skip_blocks.begin(),o.skip_blocks.end(),[](U b){return b<=22||(b>=48&&b<=70);})&&o.fast_c32&&o.fused_ffn&&o.packed_c32&&o.half_c32&&o.raw_chain&&o.pre_main8&&o.c32_finish_fused&&o.prefix_inline&&o.post_head_fused&&o.post_merge_fold&&o.mapped_c32&&o.fast_mh&&o.fused_mh&&o.grouped_mh_contract&&o.packed_weights&&o.mh_byte_stream&&o.mh_proj_diag_fb&&o.fp8_av&&o.fp8_normalized&&o.ffn_qkv&&o.ffn_qkv_max_c>=256&&o.mh_input_mapped&&o.mh_project_crop&&o.elide_identity_shift;
}
/* DLSS5_FAST_NUMERIC (2026-10-03, Zero): 1 = load the lossy fast-numeric builds of the C32 and C64/C128 wave kernels
   (c32-wave1-fast for every tier, c64-wave2-fast: CW_FAST_NUM 3 / W2_FAST_NUM 3, f32 activation/normalisation, softmax
   1/sum by rcp) next to the normal ones; a missing -fast file falls back to the normal module with one stderr line. Unset or 0 =
   default, bit-exact; anything else is reported and treated as 0. Read at network creation (results/fast-numeric-option-20261003). */
inline bool FastNumericFromEnvironment(){const char*v=std::getenv("DLSS5_FAST_NUMERIC");if(!v||!*v||!std::strcmp(v,"0"))return false;
 if(!std::strcmp(v,"1"))return true;std::fprintf(stderr,"DLSS5_FAST_NUMERIC=%s invalid (0/1), using 0\n",v);return false;}
/* DLSS5_MULTI_PASS=1/2/3 (2026-10-03, results/multi-pass-20261003): run the whole network N times per frame. Pass k+1 takes pass k's
   final RGB as its colour input (alpha 1), with the same history, seed and noise: the network output is already the clamped [0,1] picture
   in the input's working encoding (post head = input RGB + residual), so "decode then re-encode" is the identity there and is skipped
   (only the RGBA16F storage rounding of the encode pass is not repeated). Unset/empty/1 = one pass, the code path is unchanged; other
   values are reported and treated as 1. Read at network creation. Adaptive ViT reuse is off while N>1 (its cache is per frame, not per pass). */
#ifndef HIP_POOL64_BYTE_EDGE
#define HIP_POOL64_BYTE_EDGE 1
#endif
#ifndef HIP_FINAL_OUTPUT_DIRECT
#define HIP_FINAL_OUTPUT_DIRECT 1
#endif
#ifndef HIP_FINAL_COPY_REPEAT
#define HIP_FINAL_COPY_REPEAT 1
#endif
inline bool MultiSkinFromEnvironment(){const char*v=std::getenv("DLSS5_MULTI_PASS_SKIN_PROTECT");return v&&!std::strcmp(v,"1");}
inline bool MultiPredictFromEnvironment(){const char*v=std::getenv("DLSS5_MULTI_PASS_PREDICT");return !v||!*v||!std::strcmp(v,"1");}
inline unsigned MultiPassFromEnvironment(){const char*v=std::getenv("DLSS5_MULTI_PASS");if(!v||!*v||!std::strcmp(v,"1"))return 1;
 if(!std::strcmp(v,"2"))return 2;if(!std::strcmp(v,"3"))return 3;std::fprintf(stderr,"DLSS5_MULTI_PASS=%s invalid (1/2/3), using 1\n",v);return 1;}
/* DLSS5_MULTI_PASS_SKIP_BLOCKS (2026-10-03, results/multi-pass-skip-20261003): residual blocks skipped in passes 2..N only (same list
   syntax as DLSS5_SKIP_BLOCKS); pass 1 always runs the configured network. Empty/unset = no extra skip (code path unchanged). LOSSY when
   set. An unparsable list is reported and treated as empty; the network constructor also rejects blocks its pipeline cannot skip. */
inline std::set<U> MultiPassSkipFromEnvironment(){const char*v=std::getenv("DLSS5_MULTI_PASS_SKIP_BLOCKS");if(!v||!*v)return {};
 try{return ParseSkipBlocks(v);}catch(...){std::fprintf(stderr,"DLSS5_MULTI_PASS_SKIP_BLOCKS=%s invalid (block list), using none\n",v);return {};}}
/* Fixed processing geometries (tiers + the 512x512 test) and, since 2026-10-02, free ones (DLSS5_NETWORK_FREE_RES, native_network_geometry.h
   NativeNetworkGeometry::Free): both axes multiples of 128, so every level is in the 1920x1152 divisibility class; the ViT grid is
   each axis /64 rounded up to 4. Tiers keep their own (historical) grid rules; a free geometry never equals a tier. */
inline bool TierGeometry(U w,U h){return (w==512&&h==512)||(w==1920&&h==1152)||(w==1920&&h==1088)||(w==1280&&h==768)||(w==1600&&h==1024)||(w==1600&&h==960);}
inline bool FreeGeometry(U w,U h){return !TierGeometry(w,h)&&w%64==0&&h%64==0&&w>=320&&h>=320&&w<=8192&&h<=8192;} /* the shipped rule only makes multiples of 128; 64 is reachable through the DLSS5_NETWORK_FREE_PAD diagnostic */
#ifndef HIP_SP_1440
#define HIP_SP_1440 1
#endif
inline bool Sp1440Geometry(const Options&o){return o.width==2560&&o.height==1472;}
inline bool SwinRunCompatible(const Options&o){
 return o.swin_run&&o.pooled&&WaveOwnedCompatible(o)&&
  ((o.width==1600&&o.height==960)||(o.width==1920&&(o.height==1152||o.height==1088))||(HIP_SP_1440&&Sp1440Geometry(o)));
}
inline std::atomic<int> AdaptivePreviewState{0};
class Network {friend class D3D12Bridge;
 Tensor native_post_row;void* native_post_output=nullptr;
 bool sp1440_ready=false;bool final_direct_compatible=false;bool pool64_byte_available=false;
 bool vit_contract_byte_edge=false; // paired exact representation of an already E4M3-valued edge
 bool free_geometry=false; /* DLSS5_NETWORK_FREE_RES geometry (FreeGeometry): generic ViT grid, no 640-token cap */
 bool wave_owned_active=false;bool c32_skip_byte=false;bool c32_pre_down_byte=false;bool c32_post_low_byte=false;bool c512_m32_active=false;bool vit_proj_n64_active=false;unsigned vit_stream_active=0;bool c32_norm900_loaded=false;bool fast_numeric=false; /* DLSS5_FAST_NUMERIC (cached at construction): load the lossy *-fast module twins where present */
 /* ---- programmatic-dependent-launch emulation (opt.pdl; results/pdl-chain-20260925) ----
    C64/C128/C256 chain launches after the chain head go out with hipExtAnyOrderLaunch (no AQL barrier bit) and the _pdl
    kernel twins wait on / publish per-tile counters. One counter array per (kind,c,ww,hh) so every use bumps every tile
    and cumulative targets stay exact; cleared only after a full drain before uint32 rollover. The last blocks' tensors are held so the pool cannot recycle a buffer
    an in-flight launch still reads. Correctness audit and remaining acquire/scheduling conditions: results/pdl-audit-20260927. */
 unsigned pdl_mode=0,pdl_calls=0;unsigned*pdl_flags=nullptr;bool pdl_anyorder=false;std::deque<Tensor>pdl_keep;
 bool pdl_requested=false,pdl_effective=false;std::string pdl_reason;
 struct PdlPrev{unsigned*flags=nullptr;unsigned epoch=0,ww=0,sx=0,sy=0;}pdl_prev;unsigned*pdl_ffn_flags=nullptr;unsigned pdl_ffn_epoch=0;
 int(*ext_launch)(Handle,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,size_t,Handle,void**,void**,Handle,Handle,unsigned)=nullptr;
 static constexpr unsigned PDL_SLOTS=16384,PDL_RING=64;
 std::map<std::tuple<unsigned,unsigned,unsigned,unsigned>,unsigned>pdl_slot_of;std::vector<unsigned>pdl_total;unsigned pdl_last_target=0;
 unsigned*PdlSlot(unsigned kind,unsigned c,unsigned ww,unsigned hh,unsigned waves){auto key=std::make_tuple(kind,c,ww,hh);auto it=pdl_slot_of.find(key);if(it==pdl_slot_of.end()){if(pdl_slot_of.size()>=PDL_RING)throw std::runtime_error("pdl slots exhausted");it=pdl_slot_of.emplace(key,unsigned(pdl_slot_of.size())).first;pdl_total.push_back(0);}unsigned s=it->second;
  // Unsigned '< target' is invalid across wrap. Drain every old user before resetting this slot;
  // the second wait also keeps an any-order producer from overtaking the memset.
  if(pdl_total[s]>std::numeric_limits<unsigned>::max()-waves){
   api.Check(api.hipStreamSynchronize(stream),"pdl rollover drain");
   api.Check(api.hipMemsetAsync(pdl_flags+size_t(s)*PDL_SLOTS,0,size_t(PDL_SLOTS)*sizeof(unsigned),stream),"pdl rollover clear");
   api.Check(api.hipStreamSynchronize(stream),"pdl rollover ready");pdl_total[s]=0;
  }
  pdl_total[s]+=waves;pdl_last_target=pdl_total[s];return pdl_flags+size_t(s)*PDL_SLOTS;}
 static bool PdlChainHead(U block){return block==5||block==9||block==15||block==23||block==40||block==48||block==56||block==62;}
 void PreflightPdl(){
  pdl_mode=0;pdl_effective=false;pdl_reason.clear();
  if(!opt.pdl){pdl_requested=false;pdl_reason="pdl disabled by configuration";return;}
  pdl_requested=true;
  if(free_geometry&&size_t(W/4)*(H/4)/16>PDL_SLOTS){pdl_reason="processing geometry exceeds pdl counter capacity";return;}
  if(opt.graph){pdl_reason="pdl requires graph off";return;}
  ext_launch=reinterpret_cast<decltype(ext_launch)>(GetProcAddress(api.dll,"hipExtModuleLaunchKernel"));
  if(!ext_launch){pdl_reason="driver extension hipExtModuleLaunchKernel missing";return;}
  auto itFast=modules.find("mh_fast");auto itFused=modules.find("mh_fused");
  if(itFast==modules.end()||itFused==modules.end()){pdl_reason="required modules for pdl missing";return;}
  static const char* const kPdlFusedSymbols[]={
   "c64_attention_project_fb_diag_pdl","c64_attention_project_fb_bout_diag_pdl",
   "c128_attention_project_fb_diag_pdl","c128_attention_project_fb_bout_diag_pdl",
   "c256_attention_project_fb_diag_pdl","c256_attention_project_fb_bout_diag_pdl"
  };
  for(const char* sym : kPdlFusedSymbols){
   Handle fn{};
   if(api.hipModuleGetFunction(&fn,itFused->second,sym)!=0||!fn){
    pdl_reason=std::string("missing pdl symbol in mh_fused: ")+sym;return;
   }
  }
  static const char* const kPdlFastSymbols[]={
   "mh_ffn_fused_c64_project_g128_qkv_fb_pdl","mh_ffn_fused_c64_project_g128_qkv_bytein_fb_pdl",
   "mh_ffn_fused_c64_project_mapped_g128_qkv_fb_pdl","mh_ffn_fused_c64_project_mapped_g128_qkv_bytein_fb_pdl",
   "mh_ffn_fused_c128_project_g128_qkv_fb_pdl","mh_ffn_fused_c128_project_g128_qkv_bytein_fb_pdl",
   "mh_ffn_fused_c128_project_mapped_g128_qkv_fb_pdl","mh_ffn_fused_c128_project_mapped_g128_qkv_bytein_fb_pdl",
   "mh_ffn_fused_c256_frag_project_g128_qkv_fb_pdl","mh_ffn_fused_c256_frag_project_g128_qkv_bytein_fb_pdl",
   "mh_ffn_fused_c256_frag_project_mapped_g128_qkv_fb_pdl","mh_ffn_fused_c256_frag_project_mapped_g128_qkv_bytein_fb_pdl"
  };
  for(const char* sym : kPdlFastSymbols){
   Handle fn{};
   if(api.hipModuleGetFunction(&fn,itFast->second,sym)!=0||!fn){
    pdl_reason=std::string("missing pdl symbol in mh_fast: ")+sym;return;
   }
  }
  api.Check(api.hipMalloc((void**)&pdl_flags,size_t(PDL_SLOTS)*PDL_RING*4),"pdl flags");
  api.Check(api.hipMemsetAsync(pdl_flags,0,size_t(PDL_SLOTS)*PDL_RING*4,stream),"pdl flags zero");
  pdl_mode=7;pdl_effective=true;pdl_reason="enabled";
 }
 public:
 unsigned PdlCalls()const{return pdl_calls;}
 bool PdlRequested()const{return pdl_requested;}
 bool PdlEffective()const{return pdl_effective;}
 const std::string& PdlReason()const{return pdl_reason;}
 private:
#ifdef DLSS5_LAYER_BENCH
 friend struct LayerBenchmark;
 unsigned diagnostic_kernel_repeats=1;
#endif
 Api api;Handle stream{};HipSubmitPulseOps pulse_ops;SubmitPulseLease<HipSubmitPulseOps> pulse_lease;bool pulse_fallback_logged=false,pulse_runtime_reported=false,pulse_frame_history=false,pulse_ordered_down_c256=false,pulse_platform_scope=false,pulse_driver_validated=false,pulse_capabilities_ok=false,pulse_bridge_diagnostics=false;unsigned long long pulse_site_visits=0,pulse_record_attempts=0,pulse_record_accepted=0;unsigned pulse_last_reject_mask=0,pulse_frame_pdl_start=0;std::map<std::string,Handle>modules,functions;std::set<std::string>missing_functions;std::map<std::string,Tensor>weights;Options opt;
 struct Timing{std::string name;Handle begin{},end{};};std::vector<Timing>timings;
 std::map<std::string,std::pair<double,unsigned>>wall_timings;
 Handle captured_graph{},graph_exec{};void*graph_input{},*graph_history{},*graph_output{};U graph_seed{};bool graph_warmed{},graph_capturing{};unsigned graph_builds{},graph_replays{};
 void ClearGraph(){if(graph_exec){api.hipGraphExecDestroy(graph_exec);graph_exec=nullptr;}if(captured_graph){api.hipGraphDestroy(captured_graph);captured_graph=nullptr;}}
 std::vector<Tensor>pool;Tensor device_noise;std::map<U,Tensor>gather_maps[2];

 Tensor temporal_features;bool temporal_feature_tap_active=false;
 Tensor experimental_history,experimental_prefix,experimental_raw,experimental_logit,experimental_weights,experimental_sig,experimental_recip;
 bool experimental_ready=false;unsigned experimental_frames=0;

 U W,H;std::function<void(const std::string&)>progress;std::function<void(const std::string&,void*,size_t)>observer;
 static U Count(size_t n){if(!n||n>std::numeric_limits<U>::max())throw std::runtime_error("HIP reference index count overflow");return U(n);}
 Tensor New(size_t n){Count(n);size_t bytes=n*4;if(opt.pooled){Tensor*best=nullptr;for(auto&t:pool)if(t.use_count()==1&&t->capacity>=bytes&&(!best||t->capacity<(*best)->capacity))best=&t;if(best){(*best)->bytes=bytes;return *best;}if(graph_capturing)throw std::runtime_error("graph capture requires warmed allocation pool");auto t=std::make_shared<Allocation>(api,bytes);pool.push_back(t);return t;}return std::make_shared<Allocation>(api,bytes);}
 /* 2026-09-30 (HIP_C512_PAD16): C512 tensors whose token count is not a multiple of 16 (900: 50x30=1500) get a
    buffer rounded up to 16 tokens; `bytes` stays the valid size (stage dumps unchanged). CompactC512Body then reads the
    input in place instead of mh_shift_pack copying it into a zero-padded buffer: every C512 kernel is row-independent
    (WMMA rows = tokens), the pad rows only reach pad rows, and attention/projection read or write valid pixels only. */
 Tensor NewPad16(size_t valid,size_t ch){size_t pad=(valid+15)&~size_t(15);auto t=New(pad*ch);t->bytes=valid*ch*4;
#if HIP_C512_PAD16_POISON
  if(pad>valid)api.Check(api.hipMemsetAsync(static_cast<char*>(t->ptr)+valid*ch*4,0xff,(pad-valid)*ch*4,stream),"pad16 poison"); /* diagnostic: NaN pad rows must not change any output */
#endif
  return t;}
 Tensor Upload(const void*p,size_t bytes,bool persistent=false){if(!bytes||bytes%4)throw std::runtime_error("upload size");Tensor t;if(persistent){
#if HIP_ADDR_SKEW
  Allocation::SkewPersistent()=true;t=std::make_shared<Allocation>(api,bytes);Allocation::SkewPersistent()=false;
#else
  t=std::make_shared<Allocation>(api,bytes);
#endif
  }else t=New(bytes/4);api.Check(api.hipStreamSynchronize(stream),"before upload");api.Check(api.hipMemcpy(t->ptr,p,bytes,1),"upload");return t;}
 void* P(const Tensor&t){return t?t->ptr:nullptr;}
 // Packing wrappers record the byte ranges the in-place packers leave dead; UploadWeight maps only the rest when sparse_weights is on.
 std::vector<std::pair<size_t,size_t>>dead;size_t sparse_granularity{};size_t sparse_logical{},sparse_backed{};unsigned sparse_dense{};
 void Fp8(std::vector<float>&v,const std::vector<std::pair<size_t,size_t>>&regions){PackWeightRegions(v,regions);for(auto&r:regions)dead.push_back({r.first*4+r.second,(r.first+r.second)*4});}
 void HalfR(std::vector<float>&v,size_t begin,size_t count){PackHalfMatrixRounded(v,begin,count);dead.push_back({begin*4+2*count,(begin+count)*4});}
 void HalfExact(std::vector<float>&v,size_t begin,size_t count){if(begin+count>v.size())throw std::runtime_error("half matrix shape");for(size_t i=0;i<count;i++){uint16_t h=ExactWeightHalf(v[begin+i]);std::memcpy(reinterpret_cast<unsigned char*>(v.data()+begin)+i*2,&h,2);}dead.push_back({begin*4+2*count,(begin+count)*4});}
 Tensor UploadWeight(const std::vector<float>&v,const std::string&key){auto d=std::move(dead);dead.clear();const size_t bytes=v.size()*4;
  const bool selected=opt.sparse_filter.empty()||(key.size()>=opt.sparse_filter.size()&&key.compare(key.size()-opt.sparse_filter.size(),opt.sparse_filter.size(),opt.sparse_filter)==0);
  if(opt.sparse_full)d.clear();
  if(!opt.sparse_weights||!selected||(d.empty()&&!opt.sparse_full))return Upload(v.data(),bytes,true);
  const size_t g=sparse_granularity;std::sort(d.begin(),d.end());std::vector<std::pair<size_t,size_t>>live;size_t at=0;
  for(auto&r:d){size_t b=std::min(r.first,bytes),e=std::min(r.second,bytes);if(b>at)live.push_back({at,b});at=std::max(at,e);}if(at<bytes)live.push_back({at,bytes});
  std::vector<std::pair<size_t,size_t>>pages;for(auto&r:live){size_t b=r.first/g*g,e=(r.second+g-1)/g*g;if(!pages.empty()&&b<=pages.back().second)pages.back().second=std::max(pages.back().second,e);else pages.push_back({b,e});}
  size_t backed=0;for(auto&p:pages)backed+=p.second-p.first;sparse_logical+=bytes;
  if(backed+g>=bytes&&!opt.sparse_full){sparse_backed+=bytes;sparse_dense++;return Upload(v.data(),bytes,true);}
  /* one physical chunk per granularity page: the Windows HIP 7 runtime rejects (hipErrorInvalidValue at hipMemSetAccess) or hangs on reservations whose chunks differ in size; equal-size tilings of any size pass (vmm_probe.exe tile) */std::vector<std::pair<size_t,size_t>>maps;for(auto&p:pages)for(size_t o=p.first;o<p.second;o+=g)maps.push_back({o,g});auto t=std::make_shared<Allocation>(api,bytes,g,maps);sparse_backed+=backed;
  api.Check(api.hipStreamSynchronize(stream),"before sparse upload");for(auto&r:live)api.Check(api.hipMemcpy(static_cast<char*>(t->ptr)+r.first,reinterpret_cast<const char*>(v.data())+r.first,r.second-r.first,1),"sparse upload");return t;}
 static size_t WeightElements(const std::string&name){
  if(name=="head-matrix.f32")return 524288;
  if(name=="decoder39-weights.f32")return 524800;
  if(name=="post70-scales.f32")return 64;
  if(name=="post70-head.f32")return 96;
  U b=70;std::string type;
  if(name.rfind("block",0)==0){auto dash=name.find('-');if(dash==std::string::npos)throw std::runtime_error("weight name");b=U(std::stoul(name.substr(5,dash-5)));type=name.substr(dash+1);}else if(name.rfind("post70-",0)==0)type=name.substr(7);else throw std::runtime_error("unknown weight "+name);
  U c=b<=4?32:b<=8?64:b<=14?128:b<=22?256:b<=30?512:b<=38?1024:b<=47?512:b<=55?256:b<=61?128:b<=65?64:32;
  if(type=="ds.f32")return size_t(2)*c*c;
  if(type=="weights.f32")return size_t(2)*c*c+c;
  if(type=="ffwd.f32")return 524288;
  if(type=="ffwd-projection.f32")return 262656;
  if(type=="ffn.f32")return c==32?8736:size_t(9)*c*c+c;
  if(type=="attention.f32")return c==32?8225:size_t(4)*c*c+(c/32)*4096+c/32+c;
  if(type=="expand.f32")return 4194304;
  if(type=="contract.f32")return 4195328;
  if(type=="qkv.f32")return 3145760;
  if(type=="projection.f32")return 1049600;
  throw std::runtime_error("unknown weight suffix "+name);
 }
 void* Weight(const std::string&name){auto it=weights.find(name);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("weight shape "+name+": "+std::to_string(v.size()));it=weights.emplace(name,UploadWeight(v,name)).first;}return P(it->second);}
 // QKV rows as fragment tiles (mh_qkv_normalize_frag_c512); projection bytes and the f32 scales stay where PackedMhWeight puts them.
 // ffwd-projection [512][512] E4M3 bytes in fragment-tile layout for split_projection_frag; the residual scale floats keep their offset.
 void* PackedSplitProjectionFrag(const std::string&name){auto key=name+"@proj-frag";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("split projection weight shape");Fp8(v,{{0,262144}});FragmentPackedMatrix(v,0,512,512);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 void* PackedMhWeightQkvFrag(const std::string&name,U c){const std::string key=name+"@qkv-frag";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("packed weight shape "+name);size_t cc=size_t(c)*c;Fp8(v,{{0,3*cc},{3*cc,cc}});FragmentPackedMatrix(v,0,3*size_t(c),c);FragmentPackedMatrix(v,3*cc,c,c);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 // PackedMhWeight(attention) plus the three residual-scale pieces as diagonal fragment B tiles appended after the float tail
 // (byte 4*WeightElements; c*96 bytes): the fused attention-project kernels then form the residual with three MMAs instead of scalar FMAs.
 void* WaveOwnedAttentionWeight(const std::string&name,U c){
  std::string key=name+"@wave-owned-frag-diag";auto it=weights.find(key);
  if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);size_t cc=size_t(c)*c;
   if(v.size()!=WeightElements(name))throw std::runtime_error("wave-owned weight shape");
   Fp8(v,{{0,3*cc},{3*cc,cc}});FragmentPackedMatrix(v,0,3*c,c);FragmentPackedMatrix(v,3*cc,c,c);AppendMhResidualDiagonals(v,c);
   it=weights.emplace(key,UploadWeight(v,key)).first;
  }return P(it->second);
 }
 void* PackedMhWeightDiag(const std::string&name,U c){const std::string key=name+"@fp8-diag";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("packed weight shape "+name);size_t cc=size_t(c)*c;Fp8(v,{{0,3*cc},{3*cc,cc}});AppendMhResidualDiagonals(v,c);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 void* PackedMhWeight(const std::string&name,U c,bool attention){
  if(!opt.packed_weights)return Weight(name);
  const std::string key=name+"@fp8"+((opt.grouped_mh_contract&&!attention)?"-g128":"");auto it=weights.find(key);
  if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("packed weight shape "+name);size_t cc=size_t(c)*c;
   if(opt.grouped_mh_contract&&!attention)ValidateGroupedMhContract(v,c);
   if(attention)Fp8(v,{{0,3*cc},{3*cc,cc}});else Fp8(v,{{0,4*cc},{4*cc,4*cc},{8*cc,cc}});
   it=weights.emplace(key,UploadWeight(v,key)).first;
  }return P(it->second);
 }
 U TiledMin()const{return opt.tiled_ffn_small?64:opt.tiled_ffn_min_c;}
 // FFN expand/contract/project regions as FragmentPackedMatrix tiles (mh_ffn_fused_c{64,128}_frag_*); float tails unchanged.
 void* PackedFusedMhWeightFrag(const std::string&name,U c){auto key=name+"@ffn-frag";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("fused FFN weight shape");if(opt.grouped_mh_contract)ValidateGroupedMhContract(v,c);size_t cc=size_t(c)*c;Fp8(v,{{0,4*cc},{4*cc,4*cc},{8*cc,cc}});FragmentPackedMatrix(v,0,4*c,c);FragmentPackedMatrix(v,4*cc,c,4*c);FragmentPackedMatrix(v,8*cc,c,c);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 // Attention weights with only the QKV rows as fragment tiles (the FFN/QKV kernel's aw); the projection region stays row-major.
 // C256 FFN in the wide fragment layout (W2_FFN_W16), consumed only by the _w16 exports (c256_wave2*_w16, sp_*256_w16).
 void* PackedFusedMhWeightFragW16(const std::string&name,U c){auto key=name+"@ffn-frag-w16";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("fused FFN weight shape");if(opt.grouped_mh_contract)ValidateGroupedMhContract(v,c);size_t cc=size_t(c)*c;Fp8(v,{{0,4*cc},{4*cc,4*cc},{8*cc,cc}});FragmentPackedMatrixW16(v,0,4*c,c);FragmentPackedMatrixW16(v,4*cc,c,4*c);FragmentPackedMatrixW16(v,8*cc,c,c);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 void* PackedMhWeightQkvFragOnly(const std::string&name,U c){auto key=name+"@qkv-frag-only";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("packed weight shape "+name);size_t cc=size_t(c)*c;Fp8(v,{{0,3*cc},{3*cc,cc}});FragmentPackedMatrix(v,0,3*size_t(c),c);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 void* PackedFusedMhWeight(const std::string&name,U c,bool force_tiled=false){
  if(!force_tiled&&(!opt.tiled_mh_ffn||c<TiledMin()))return PackedMhWeight(name,c,false);
  auto key=name+"@ffn-tiled"+(opt.grouped_mh_contract?"-g128":"");auto it=weights.find(key);
  if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("fused FFN weight shape");if(opt.grouped_mh_contract)ValidateGroupedMhContract(v,c);size_t cc=size_t(c)*c;Fp8(v,{{0,4*cc},{4*cc,4*cc},{8*cc,cc}});TilePackedMatrix(v,0,4*c,c);TilePackedMatrix(v,4*cc,c,4*c);it=weights.emplace(key,UploadWeight(v,key)).first;}
  return P(it->second);
 }
 void* PackedC32Weight(const std::string&name,bool attention){
  if(!opt.packed_c32)return Weight(name);
  const std::string key=name+"@c32fp8";auto it=weights.find(key);
  if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);
   if(v.size()!=WeightElements(name))throw std::runtime_error("packed C32 weight shape");
   if(attention)Fp8(v,{{0,4096}});else{Fp8(v,{{512,4096},{4608,4096}});AppendC32ResidualDiagonals(v);}
   it=weights.emplace(key,UploadWeight(v,key)).first;
  }return P(it->second);
 }
 // Split FFN weights with the mix region also packed to RNE half (kernel split_mix_blocked_h16w); expand/contract as PackedSplitFfnWeight.
 void* PackedSplitFfnWeightMixHalf(const std::string&name){const auto key=name+"@split-mix-f16";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("split FFN weight shape");HalfR(v,0,262144);HalfExact(v,262144,131072);Fp8(v,{{393216,131072}});it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 void* PackedSplitFfnWeightMixFp8(const std::string&name){const auto key=name+"@split-mix-fp8";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("split FFN weight shape");Fp8(v,{{0,262144},{262144,131072},{393216,131072}});it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);} /* 2026-10-01: mix+expand+contract all E4M3 (exact) for split_ffn_one_w2f8 (results/c512-qkv-pipeline-20261001) */
 void* PackedSplitFfnWeight(const std::string&name){const auto key=name+"@split-expand-f16-contract-fp8";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("split FFN weight shape");HalfExact(v,262144,131072);Fp8(v,{{393216,131072}});it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 void* PackedDeepWeight(const std::string&name,size_t matrix_elements){
  if(!opt.packed_weights||!opt.fast_deep)return Weight(name);
  const std::string key=name+"@fp8";auto it=weights.find(key);if(it==weights.end()){
   auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("packed deep weight shape "+name);
   Fp8(v,{{0,matrix_elements}});it=weights.emplace(key,UploadWeight(v,key)).first;
  }return P(it->second);
 }
 // ViT projection E4M3 bytes as FragmentPackedMatrix tiles (vit_project_frag); tail floats (skip scales) stay at their offsets.
 void* PackedVitProjectionFrag(const std::string&name){auto key=name+"@vit-proj-frag";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("ViT projection shape");Fp8(v,{{0,1048576}});FragmentPackedMatrix(v,0,1024,1024);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 // ViT QKV halves as 16x16 f16 B fragment tiles per part (vit_qkv_project_normalize_fused_f16compact_fp8_frag): tile (nt*64+kt), lane (g,rc) k=g*8..+8; scales at the compact offset 1572864.
 void* PackedVitQkvWeightFrag8(const std::string&name){auto key=name+"@qkv-fp8-frag";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=3*1048576+32)throw std::runtime_error("ViT QKV compact shape");std::vector<float>scales(v.begin()+3*1048576,v.end());std::vector<uint8_t>t(size_t(3)*1048576);for(size_t part=0;part<3;part++)for(size_t nt=0;nt<64;nt++)for(size_t kt=0;kt<64;kt++)for(unsigned g=0;g<2;g++)for(unsigned rc=0;rc<16;rc++)for(unsigned e=0;e<8;e++)t[part*1048576+(nt*64+kt)*256+(g*16+rc)*8+e]=ExactWeightFp8(v[part*1048576+(nt*16+rc)*1024+kt*16+g*8+e]);v.resize(3*1048576/4+32);std::memcpy(v.data(),t.data(),t.size());std::memcpy(v.data()+3*1048576/4,scales.data(),32*sizeof(float));it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);} /* 2026-10-01 VIT_QKV_F8W: E4M3 fragments (exact) for vit_stream_qkv_frag_hin_w5f8 (results/c512-qkv-pipeline-20261001 §10) */
 void* PackedVitQkvWeightFrag(const std::string&name){auto key=name+"@qkv-f16-frag";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=3*1048576+32)throw std::runtime_error("ViT QKV compact shape");std::vector<float>scales(v.begin()+3*1048576,v.end());std::vector<uint16_t>t(size_t(3)*1048576);for(size_t part=0;part<3;part++)for(size_t nt=0;nt<64;nt++)for(size_t kt=0;kt<64;kt++)for(unsigned g=0;g<2;g++)for(unsigned rc=0;rc<16;rc++)for(unsigned e=0;e<8;e++)t[part*1048576+(nt*64+kt)*256+(g*16+rc)*8+e]=ExactWeightHalf(v[part*1048576+(nt*16+rc)*1024+kt*16+g*8+e]);v.resize(3*1048576/2+32);std::memcpy(v.data(),t.data(),t.size()*2);std::memcpy(v.data()+3*1048576/2,scales.data(),32*sizeof(float));it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 void* PackedVitQkvWeight(const std::string&name){auto key=name+"@qkv-f16-compact";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("ViT QKV shape");if(v.size()!=3*1048576+32)throw std::runtime_error("ViT QKV compact shape");PackHalfMatrix(v,3*1048576);std::memmove(v.data()+3*1048576/2,v.data()+3*1048576,32*sizeof(float));v.resize(3*1048576/2+32);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 void* PackedVitWeight(const std::string&name,U rows,U cols,bool tiled,bool frag=false){
  if(!tiled)return PackedDeepWeight(name,size_t(rows)*cols);
  auto key=name+(frag?"@vit-frag":"@vit-tiled");auto it=weights.find(key);
  if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("ViT tiled weight shape");Fp8(v,{{0,size_t(rows)*cols}});if(frag)FragmentPackedMatrix(v,0,rows,cols);else TilePackedMatrix(v,0,rows,cols);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);
 }
 /* Optional export probe: modules without the symbol keep the old launch shape. */
 bool HasFn(const std::string&m,const std::string&name){std::string key=m+":"+name;if(functions.count(key))return true;if(missing_functions.count(key))return false;auto mi=modules.find(m);if(mi==modules.end())return false;Handle f{};if(api.hipModuleGetFunction(&f,mi->second,name.c_str())){missing_functions.insert(key);return false;}functions.emplace(key,f);return true;} /* 2026-10-01 rebuild-baseline: misses are cached too -- per-frame probes (ViT _ks2, ...) otherwise call hipModuleGetFunction every frame (+0.01..0.02ms ABBA) */
 bool C32Norm900Active()const{return c32_norm900_loaded&&fast_numeric&&W==1600&&H==960&&multi_pass==1&&!opt.graph&&!opt.experimental_temporal;}
 Handle Fn(const std::string&m,const std::string&name){static const std::string normkey="c32_norm900";const std::string&actual=m=="c32_wave1"&&C32Norm900Active()?normkey:m;std::string key=actual+":"+name;auto it=functions.find(key);if(it!=functions.end())return it->second;Handle f{};api.Check(api.hipModuleGetFunction(&f,modules.at(actual),name.c_str()),name.c_str());functions.emplace(key,f);return f;}
 /* DLSS5_FAST_NUMERIC twin (2026-10-03 fast-vit-c512): <stem>.hsaco -> <stem>-fast.hsaco when the option is 1 and the file
    exists; missing twin falls back to the exact module with one stderr line (same contract as the C32/C64 swap). */
 std::string FastTwin(const std::string&requested,bool wave_c32=false)const{
  if(opt.integration.select_module)return opt.integration.select_module(requested,fast_numeric,opt.modules);
  const std::string stem=wave_c32?"c32-wave1":requested;
  if(!fast_numeric)return stem;const std::string fast=stem+"-fast";
  if(std::ifstream(std::filesystem::u8path(opt.modules+"/"+fast+".hsaco"),std::ios::binary).good())return fast;
  std::fprintf(stderr,"DLSS5_FAST_NUMERIC=1: %s.hsaco missing, using %s.hsaco\n",fast.c_str(),stem.c_str());return stem;}
 template<class...A>void Run(const char*m,const char*name,size_t n,A...args){
  U count=Count(n),threads=256;unsigned groups=0;std::string module=m,kernel=name;
  if(wave_owned_active){
   static const std::map<std::string,std::string> kernels={
    {"c32_fast_ffn_attention_fused_half_chain","c32_wave1_chain"},{"c32_fast_ffn_attention_fused_half_mapped","c32_wave1_mapped"},
    {"c32_fast_ffn_attention_fused_half_chain_finish","c32_wave1_finish"},{"c32_fast_ffn_attention_fused_half_chain_finish_dcrop","c32_wave1_finish_dcrop"},
    {"c32_post_merge_head_half","c32_wave1_post"},{"c32_fast_ffn_attention_fused_half_prefix_finish_main8","c32_wave1_prefix"}};
   auto it=kernels.find(kernel);if(it!=kernels.end()){module="c32_wave1";kernel=it->second;groups=count;threads=32;}
  }
  static const std::set<std::string> accelerated={"c32_ffn_expand","c32_ffn_contract","c32_attn_qkv","c32_attn_scores","c32_attn_av","c32_attn_project","mh_ffn_expand","mh_ffn_contract","mh_ffn_project","mh_qkv","mh_scores_exp","mh_attention_av","mh_attention_project","mh_pool_project","split_mix","split_expand","split_contract","split_projection","vit_expand","vit_project","vit_qkv_project","decoder_project2x"};
  if(opt.wmma&&accelerated.count(kernel)){if(count%256&&kernel!="decoder_project2x")throw std::runtime_error("WMMA tile count: "+kernel);module+="_wmma";if(module!="deep_wmma")kernel+="_wmma";threads=32;}
  if(opt.tiled){std::string original=name;
   if(original=="c32_ffn_expand"||original=="c32_ffn_contract"||original=="c32_attn_qkv"||original=="c32_attn_project"){
    U columns=original=="c32_ffn_expand"?128:original=="c32_attn_qkv"?96:32,tokens=count/columns;
    module="c32_tiled";kernel=original+"_tiled";threads=columns==128?512:256;groups=((tokens+63)/64)*(columns==128?2:columns==96?3:1);
   }else if(original=="mh_ffn_expand"||original=="mh_ffn_contract"||original=="mh_ffn_project"||original=="mh_qkv"||original=="mh_attention_project"||original=="mh_pool_project"){
    auto tuple=std::make_tuple(args...);auto integer=[](auto x)->U{if constexpr(std::is_integral_v<decltype(x)>)return U(x);else return 0;};
    U channels=original=="mh_attention_project"?integer(std::get<sizeof...(A)-2>(tuple)):integer(std::get<sizeof...(A)-1>(tuple));
    U columns=channels*(original=="mh_ffn_expand"?4:original=="mh_qkv"?3:original=="mh_pool_project"?2:1);if(!columns)throw std::runtime_error("tiled channel ABI");U tokens=count/columns;
    module="mh_tiled";kernel=original+"_tiled";threads=512;groups=((tokens+63)/64)*((columns+63)/64);
   }
  }
  if(opt.fast_deep&&std::string(m)=="deep"&&std::string(name)!="vit_gather"){
   module="deep_fast";kernel=name;threads=32;groups=0;
   if(kernel.rfind("vit_expand_blocked_fp8",0)==0||kernel.rfind("vit_contract_blocked_fp8",0)==0||kernel.rfind("vit_contract_parts_blocked_fp8",0)==0)groups=count/1024;
   if(kernel.rfind("vit_expand_blocked_fp8",0)==0&&kernel.size()>3&&kernel.compare(kernel.size()-3,3,"_m4")==0)groups=((count/4096+63)/64)*64;
   if(kernel.rfind("vit_expand_blocked_fp8",0)==0&&kernel.size()>3&&kernel.compare(kernel.size()-3,3,"_m2")==0)groups=((count/4096+31)/32)*64;
   if(kernel=="vit_qkv_project_blocked"||kernel=="vit_qkv_project_normalize_fused"||kernel=="vit_qkv_project_normalize_fused_f16compact"||kernel=="vit_qkv_project_normalize_fused_f16compact_fp8"||kernel=="vit_qkv_project_normalize_fused_f16compact_fp8_bytein"||kernel=="vit_qkv_project_normalize_fused_f16compact_fp8_h16in"||kernel=="vit_qkv_project_normalize_fused_f16compact_fp8_frag")groups=count/512;
   if(kernel=="vit_qkv_project_normalize_fused_f16compact_fp8_h16in_n4")groups=count/1024;
   if(kernel=="split_mix_blocked"||kernel=="split_mix_blocked_h16w"||kernel=="split_projection_blocked"||kernel=="split_projection_blocked_t8"||kernel=="split_projection_frag"||kernel=="split_projection_frag_nof32")groups=count/1024;
   if(kernel=="split_ffn_fused"||kernel=="split_ffn_fused_fp8"||kernel=="split_ffn_fused_fp8_mix"||kernel=="split_ffn_fused_fp8_t8"){threads=128;groups=count/1024;}
   if(kernel=="vit_ffn_fused"){threads=512;groups=count/16384;}
   if(kernel=="vit_contract_combine"||kernel=="vit_pack_input"||kernel=="vit_pack_input_tiled")threads=256;
   if(kernel=="vit_qkv_normalize")count=Count(size_t(count)*32);
   if(kernel=="vit_attention_inverse_fast")count=Count(size_t(count)*16);
   if(HIP_VIT_ATTN_KSPLIT_HOST&&kernel.size()>4&&kernel.compare(kernel.size()-4,4,"_ks2")==0&&kernel.rfind("vit_attention_fused_",0)==0){threads=64;groups=count/256;}
  }
  if(module=="mh_fast"){
   if(kernel.rfind("mh_pool_project_fused_c",0)==0){threads=32;groups=count/1024;}
   else if(kernel.rfind("mh_pool_project_group_c",0)==0){U c=U(std::stoul(kernel.substr(23)));threads=c;groups=(count/(2*c)+15)/16;}
   else if(kernel=="mh_qkv_normalize_wave_c512"){threads=128;groups=count/4096;}
   else if(kernel=="mh_qkv_normalize_frag_c512"){threads=32;groups=count/1024;}
   else if(kernel=="mh_attention_project_frag_c512"&&HIP_C512_PROJ_WN4&&HasFn(module,"mh_attention_project_frag_c512_wn4")){kernel="mh_attention_project_frag_c512_wn4";threads=128;groups=count/1024;}
   else if(kernel=="mh_attention_project_frag_c512"||kernel=="mh_attention_project_frag_c512_fb8"){threads=32;groups=count/1024;}
   else if(kernel.rfind("mh_ffn_fused_c",0)==0){U c=U(std::stoul(kernel.substr(14)));if((c!=64&&c!=128&&c!=256)||count%(c*16))throw std::runtime_error("fused FFN dispatch shape");threads=c*2;groups=count/(c*16);}
   else if(kernel=="mh_qkv_normalize_fast"||kernel=="mh_qkv_normalize_fast_wave"||kernel=="mh_qkv_normalize_fast_wave_fp8"){threads=256;if(kernel!="mh_qkv_normalize_fast")count=Count(size_t(count)*32);}
   else if(kernel=="mh_pool_project_c32_b8_out8"){threads=32;groups=unsigned(((count/64+15)/16)*4);}
   else if(kernel=="mh_scores_exp_fast"||kernel=="mh_probabilities_fast"||kernel=="mh_attention_av_fast"||kernel=="mh_pool_project_production"||kernel=="mh_pool_project_production_h16w"||kernel=="mh_pool_project_c32_b8")threads=32;
   else {auto tuple=std::make_tuple(args...);auto integer=[](auto x)->U{if constexpr(std::is_integral_v<decltype(x)>)return U(x);else return 0;};
    std::string shape=kernel;if(shape.size()>4&&shape.substr(shape.size()-4)=="_fp8")shape.resize(shape.size()-4);
    bool project=shape=="mh_attention_project_fast_matrix"||shape=="mh_attention_project_fast_scalar";
    U c=project?integer(std::get<sizeof...(A)-2>(tuple)):integer(std::get<sizeof...(A)-1>(tuple));U cols=c*(shape=="mh_ffn_expand_fast"?4:(shape=="mh_qkv_fast"||shape=="mh_qkv_normalize_fused")?3:1);
    if(!cols)throw std::runtime_error("MH fast channel ABI");groups=((count/cols+63)/64)*((cols+63)/64);threads=512;
   }
  }
  if(module=="c32_fused"||module=="c32_fused_ffn"||module=="mh_fused"){groups=count;threads=128;}
  if(module=="c64_wave2"){groups=count;threads=kernel.rfind("c64_",0)==0?64:kernel.rfind("c128_",0)==0?128:256;}
  if(module=="c32_wave1"&&(kernel=="c32_wave1_up"||kernel=="c32_wave1_up_b8"||kernel=="c32_wave1_up_lb"||kernel=="c32_wave1_up_b8_lb"||kernel=="c32_wave1_finish_dcrop_b8"||kernel=="c32_wave1_finish_dcrop_b8d"||kernel=="c32_wave1_prefix_b8d"||kernel=="c32_wave1_mapped_b8"||kernel=="c32_wave1_finish_b8"||(kernel=="c32_wave1_post_logit"||kernel=="c32_wave1_post_b8_logit"||kernel=="c32_wave1_post_b8"||kernel=="c32_wave1_post_b8_rgba"||kernel=="c32_wave1_post_b8_features"))){groups=count;threads=32;}
  if(module=="c512_m32_mh"||module=="c512_m32_deep"){groups=count/1024;threads=32;}
  if(kernel=="split_ffn_one_w2"||kernel=="split_ffn_one_w2f8")threads=64;
  if(kernel=="split_ffn_proj_fused"){threads=256;groups=count/8192;}
  if(module=="c512_m32_mh"&&kernel=="c512_qkv_attention_fused"){groups=count;threads=64;}
  if(module=="c512_m32_mh"&&kernel=="c512_qkv_attention_compact"){groups=count;threads=64;}
  if(vit_proj_n64_active&&module=="deep_fast"&&kernel=="vit_project_frag"&&count%1024==0){module="vit_wide_deep";kernel="vit_project_frag_n64";groups=unsigned(((count/1024+15)/16)*16);threads=32;}
  if(kernel=="vit_stream_contract_frag_hout"||kernel=="vit_stream_contract_frag_bout"){module="vit_stream";groups=count/1024;threads=32;}
  if(kernel=="vit_stream_qkv_frag_bin_w5f8"){module="vit_stream";groups=96*((count/3072/16+4)/5);threads=160;}
  if(kernel=="vit_stream_qkv_frag_hin"){module="vit_stream";groups=count/512;threads=32;if(count%3072==0&&HIP_VIT_QKV_F8W&&HasFn(module,"vit_stream_qkv_frag_hin_w5f8")){kernel="vit_stream_qkv_frag_hin_w5f8";groups=96*((count/3072/16+4)/5);threads=160;}else if(count%3072==0&&HasFn(module,"vit_stream_qkv_frag_hin_w5")){kernel="vit_stream_qkv_frag_hin_w5";groups=96*((count/3072/16+4)/5);threads=160;}} /* w5: five waves share one head's weights through LDS; same per-wave math */
  if(vit_stream_active&&kernel=="vit_project_frag_n64"){module="vit_stream";kernel=vit_contract_byte_edge?"vit_stream_project_n64_bb":vit_stream_active==1?"vit_stream_project_n64_b":vit_stream_active==2?"vit_stream_project_n64_h":"vit_stream_project_n64_bh";}
  if(module=="mh_window"){groups=count;threads=256;} /* one 8x8 window per 256-thread group (c64_window_fused*) */
  if(kernel.rfind("c64_attention_project",0)==0||kernel.rfind("c128_attention_project",0)==0||kernel.rfind("c256_attention_project",0)==0){groups=count;threads=(kernel.rfind("c64_attention_project",0)==0&&kernel.compare(kernel.size()-4,4,"_w16")!=0)?256:512;}
  if(module=="mp_predict"&&(kernel=="mp_predict_gain"||kernel=="mp_predict_gain_stride")){threads=32;groups=count/32;}
  if(module=="prefix_fast"){threads=(kernel=="dlss5_prefix_fast_project"||kernel=="dlss5_prefix_fast_fused"||kernel=="dlss5_prefix_fast_fused_raster")?32:256;if(kernel=="dlss5_prefix_fast_fused"||kernel=="dlss5_prefix_fast_fused_raster")groups=count/512;}
  if(module=="c32_fast_ffn"){bool expand=kernel=="c32_ffn_expand_fast";U tokens=count/(expand?128:32);threads=expand?512:256;groups=((tokens+63)/64)*(expand?2:1);}
  if(module=="c32_fast_attention")threads=32;
  if(opt.wave&&(kernel=="c32_attn_normalize"||kernel=="c32_attn_probabilities"||kernel=="mh_normalize"||kernel=="mh_probabilities")){module="wave";kernel+="_wave";count=Count(size_t(count)*32);}
  const U dec_nt=(kernel.rfind("decoder_project2x_h16w_n",0)==0||kernel.rfind("decoder_project2x_h16w_byteout_n",0)==0)?U(std::stoul(kernel.substr(kernel.rfind('_')+2))):1u;const bool dec_w=kernel=="decoder_project2x_h16w_w"||kernel=="decoder_project2x_h16w_byteout_w"||kernel=="decoder_project2x_h16w_w_f8"||kernel=="decoder_project2x_h16w_byteout_w_f8"; /* HIP_DEC_NT exports: NT column tiles per wave */
  if((kernel=="decoder_project2x"||kernel=="decoder_project2x_h16w"||kernel=="decoder_project2x_h16w_byteout"||kernel=="decoder_project2x_h16w_f8"||kernel=="decoder_project2x_h16w_byteout_f8"||dec_nt>1||dec_w)&&(module=="deep_fast"||module=="deep_wmma")){
   if constexpr(sizeof...(A)==10){auto tuple=std::make_tuple(args...);auto integer=[](auto x)->U{if constexpr(std::is_integral_v<decltype(x)>)return U(x);else return 0;};
    const U columns=integer(std::get<9>(tuple));if(!columns||columns%(16*dec_nt)||count%columns)throw std::runtime_error("decoder tile ABI");
    groups=Count(((size_t(count)/columns+15)/16)*(columns/16/dec_nt));
   }else throw std::runtime_error("decoder argument count");
  }
  if(opt.wall_profile)api.Check(api.hipStreamSynchronize(stream),"wall profile drain");
  auto wall_begin=opt.wall_profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
  Timing timing{kernel};if(opt.profile){api.Check(api.hipEventCreate(&timing.begin),"event create");api.Check(api.hipEventCreate(&timing.end),"event create");timings.push_back(timing);api.Check(api.hipEventRecord(timing.begin,stream),"event begin");}
  unsigned launch_repeats=1;
#ifdef DLSS5_LAYER_BENCH
  if(opt.wall_profile)launch_repeats=diagnostic_kernel_repeats;
#endif
#ifdef HIP_SWIN_PERSISTENT_TRACE
  std::printf("TOPO,%s,%s,%u,%u,%u\n",module.c_str(),kernel.c_str(),count,unsigned(groups?groups:(count+255ull)/256),threads);
#endif
  void*reuse_gate_arg=adaptive_active?P(adaptive_state):nullptr;void*argv[]={static_cast<void*>(&args)...,static_cast<void*>(&reuse_gate_arg)};unsigned repeats_here=launch_repeats*((!opt.dup_prefix.empty()&&(opt.dup_prefix.back()=='$'?kernel==opt.dup_prefix.substr(0,opt.dup_prefix.size()-1):kernel.rfind(opt.dup_prefix,0)==0))?opt.dup_count:1u);for(unsigned repeat=0;repeat<repeats_here;repeat++){unsigned gg=unsigned(groups?groups:(count+255ull)/256);if(pdl_anyorder){++pdl_calls;api.Check(ext_launch(Fn(module,kernel),gg*threads,1,1,threads,1,1,0,stream,argv,nullptr,nullptr,nullptr,1u/*hipExtAnyOrderLaunch*/),name);}else api.Check(api.hipModuleLaunchKernel(Fn(module,kernel),gg,1,1,threads,1,1,0,stream,argv,nullptr),name);}if(opt.profile)api.Check(api.hipEventRecord(timing.end,stream),"event end");if(!opt.pooled)api.Check(api.hipStreamSynchronize(stream),name);
  if(opt.wall_profile){api.Check(api.hipStreamSynchronize(stream),"wall profile completion");auto&entry=wall_timings[kernel];entry.first+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-wall_begin).count()/launch_repeats;++entry.second;}
 }
 void Stage(const std::string&name,const Tensor&t){if(progress)progress(name);if(observer){Synchronize();observer(name,P(t),t->bytes);}if(opt.dump_dir.empty()||(!opt.dump_only.empty()&&(","+opt.dump_only+",").find(","+name+",")==std::string::npos))return;api.Check(api.hipStreamSynchronize(stream),"before dump");std::vector<char>b(t->bytes);api.Check(api.hipMemcpy(b.data(),P(t),b.size(),2),"stage dump");std::ofstream f(opt.dump_dir+"/"+name+".f32",std::ios::binary);if(!f.write(b.data(),b.size()))throw std::runtime_error("stage dump write");}
 static std::string Block(U b,const char*s){return "block"+std::to_string(b)+"-"+s+".f32";}
 struct C32Result{Tensor main,down,raw;U workw,workh,sx,sy;bool down_cropped=false;bool down_byte=false;};
 Tensor inline_rgba,inline_hist;U inline_seed=0,inline_temporal=0;bool inline_prefix=false; // prefix_inline: block 0 inputs handed to C32Fast
 C32Result C32Fast(Tensor input,U w,U h,const std::string&fw,const std::string&aw,bool need_main=true,bool need_down=true,U cropw=0,U croph=0,U sx=0,U sy=0){
  U n=w*h,windows=n/64;bool diagonal=fw=="block2-ffn.f32"||fw=="block3-ffn.f32"||fw=="block4-ffn.f32"||fw=="block67-ffn.f32"||fw=="block68-ffn.f32"||fw=="block69-ffn.f32";
  Tensor raw;bool finish_fused=opt.c32_finish_fused&&opt.fused_ffn&&opt.half_c32&&!cropw&&opt.pre_main8&&fw=="block0-ffn.f32"&&need_main&&need_down;
  if(finish_fused&&inline_prefix&&HIP_C32_PRE_DOWN_BYTE&&wave_owned_active&&opt.raw_chain&&opt.mapped_c32&&!opt.skip_blocks.count(1)&&HasFn("c32_wave1","c32_wave1_prefix_b8d")&&HasFn("c32_wave1","c32_wave1_mapped_b8")){inline_prefix=false;c32_pre_down_byte=true;auto main=New(size_t(n)*8),down=New(size_t(n/4)*8);Run("c32_wave1","c32_wave1_prefix_b8d",windows,P(inline_rgba),P(inline_hist?inline_hist:inline_rgba),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(main),P(down),windows,U(diagonal?3:0),U(1),w,h,inline_seed,inline_temporal);inline_rgba.reset();inline_hist.reset();return {main,down,Tensor{},w,h,0,0};}
  if(finish_fused&&inline_prefix){inline_prefix=false;auto main=New(size_t(n)*8),down=New(size_t(n/4)*32);Run("c32_fused_ffn","c32_fast_ffn_attention_fused_half_prefix_finish_main8",windows,P(inline_rgba),P(inline_hist?inline_hist:inline_rgba),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(main),P(down),windows,U(diagonal?3:0),U(1),w,h,inline_seed,inline_temporal);inline_rgba.reset();inline_hist.reset();return {main,down,Tensor{},w,h,0,0};}
  if(finish_fused){auto main=New(size_t(n)*8),down=New(size_t(n/4)*32);Run("c32_fused_ffn","c32_fast_ffn_attention_fused_half_finish_main8",windows,P(input),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(main),P(down),windows,U(diagonal?3:0),U(1),w,h);return {main,down,Tensor{},w,h,0,0};}
  if(opt.fused_ffn){raw=New(size_t(n)*(opt.half_c32?16:32));if(opt.mapped_c32&&cropw){Run("c32_fused_ffn","c32_fast_ffn_attention_fused_half_mapped",windows,P(input),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(raw),windows,U(diagonal?3:0),U(1),cropw,croph,sx,sy);}else{Run("c32_fused_ffn",opt.half_c32?"c32_fast_ffn_attention_fused_half":"c32_fast_ffn_attention_fused",windows,P(input),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(raw),windows,U(diagonal?3:0),U(1));}}else{
  auto hidden=New(size_t(n)*128),ffn=New(size_t(n)*32);
  Run("c32_fast_ffn","c32_ffn_expand_fast",size_t(n)*128,P(input),Weight(fw),P(hidden),n);

  Run("c32_fast_ffn",diagonal?"c32_ffn_contract_fast_map3":"c32_ffn_contract_fast",size_t(n)*32,P(input),P(hidden),Weight(fw),P(ffn),n,U(1));hidden.reset();
  if(opt.fused_c32){raw=New(size_t(n)*32);Run("c32_fused","c32_fast_attention_fused",windows,P(ffn),Weight(aw),P(raw),windows,U(1));}else{
  auto qkv=New(size_t(n)*96),norm=New(size_t(n)*96);auto weights=Weight(aw);
  Run("c32_fast_attention","c32_fast_qkv",size_t(n)*96,P(ffn),weights,P(qkv),n);
  Run("c32_fast_attention","c32_fast_normalize",size_t(n)*96,P(qkv),weights,P(norm),n);qkv.reset();
  auto ex=New(size_t(windows)*4096),prob=New(size_t(windows)*4096);
  Run("c32_fast_attention","c32_fast_scores",size_t(windows)*4096,P(norm),weights,P(ex),windows);
  Run("c32_fast_attention","c32_fast_probabilities",size_t(windows)*4096,P(ex),P(prob),windows);ex.reset();
  auto av=New(size_t(n)*32);raw=New(size_t(n)*32);Run("c32_fast_attention","c32_fast_av",size_t(n)*32,P(prob),P(norm),P(av),windows);prob.reset();norm.reset();
  Run("c32_fast_attention","c32_fast_project",size_t(n)*32,P(av),P(ffn),weights,P(raw),n,U(1));ffn.reset();av.reset();
  }ffn.reset();
  }
  bool main8=opt.pre_main8&&fw=="block0-ffn.f32";auto main=need_main?New((cropw?size_t(cropw)*croph:size_t(n))*(main8?8:32)):Tensor{},down=need_down?New(size_t(n/4)*32):Tensor{};
  if((main||down)&&cropw){Run("boundary_fast","c32_finish_crop_half",size_t(n)*32,P(raw),P(main),P(down),w,h,cropw,croph,sx,sy);}else if(main||down)Run("boundary_fast",main8?"c32_finish_fast_half_main8":opt.half_c32?"c32_finish_fast_half":"c32_finish_fast",size_t(n)*32,P(raw),P(main),P(down),w,h);return {main,down,raw,w,h,0,0};
 }
 C32Result C32Body(Tensor input,U w,U h,const std::string&fw,const std::string&aw,bool need_main=true,bool need_down=true){if(opt.fast_c32)return C32Fast(input,w,h,fw,aw,need_main,need_down);U n=w*h,windows=n/64;auto hidden=New(size_t(n)*128),ffn=New(size_t(n)*32);Run("c32","c32_ffn_expand",size_t(n)*128,P(input),Weight(fw),P(hidden),n);Run("c32","c32_ffn_contract",size_t(n)*32,P(input),P(hidden),Weight(fw),P(ffn),n,U(1));hidden.reset();auto qkv=New(size_t(n)*96),norm=New(size_t(n)*64);Run("c32","c32_attn_qkv",size_t(n)*96,P(ffn),Weight(aw),P(qkv),n);Run("c32","c32_attn_normalize",n,P(qkv),Weight(aw),P(norm),n);auto ex=New(size_t(windows)*4096),prob=New(size_t(windows)*4096);Run("c32","c32_attn_scores",size_t(windows)*4096,P(norm),Weight(aw),P(ex),windows);norm.reset();Run("c32","c32_attn_probabilities",size_t(n),P(ex),P(prob),windows);ex.reset();auto av=New(size_t(n)*32);Run("c32","c32_attn_av",size_t(n)*32,P(prob),P(qkv),P(av),windows);prob.reset();qkv.reset();auto raw=New(size_t(n)*32);Run("c32","c32_attn_project",size_t(n)*32,P(ffn),P(av),Weight(aw),P(raw),n,U(1));ffn.reset();av.reset();auto main=New(size_t(n)*32),down=New(size_t(n/4)*32);Run("c32","c32_finish",size_t(n)*32,P(raw),P(main),P(down),w,h);return {main,down,raw,w,h,0,0};}
 C32Result C32(Tensor input,U w,U h,U shift,const std::string&fw,const std::string&aw){U sx=(shift&1)?4:0,sy=(shift&2)?4:0,ww=w+2*sx,hh=h+2*sy;auto packed=opt.mapped_c32?input:New(size_t(ww)*hh*32);if(!opt.mapped_c32)Run("boundary","hip_c32_pack",size_t(ww)*hh*32,P(input),P(packed),w,h,ww,hh,sx,sy);auto result=opt.crop_c32?C32Fast(packed,ww,hh,fw,aw,fw!="post70-ffn.f32",fw=="block4-ffn.f32",w,h,sx,sy):C32Body(packed,ww,hh,fw,aw,fw!="post70-ffn.f32",fw=="block4-ffn.f32");if(result.main&&!opt.crop_c32){auto cropped=New(size_t(w)*h*32);Run("mh","mh_shift_crop",size_t(w)*h*32,P(result.main),P(cropped),w,h,ww,sx,sy,U(32));result.main=cropped;}result.sx=sx;result.sy=sy;return result;}
 bool C32UpBodyPath()const{return wave_owned_active&&opt.raw_chain&&opt.mapped_c32&&opt.half_c32&&opt.packed_c32&&opt.decoder_h16w&&opt.fast_deep&&!opt.skip_blocks.count(66)&&(W/2)%2==0&&(H/2)%2==0;}
 /* The block4 main is consumed only by the block66 up (skips[0]); store it as bytes only when that consumer is the wave-owned up with a byte reader. */
 bool C32SkipByte(){return HIP_C32_SKIP_BYTE&&C32UpBodyPath()&&HasFn("c32_wave1","c32_wave1_finish_dcrop_b8")&&HasFn("c32_wave1","c32_wave1_up_b8");}
 C32Result C32UpBody(Tensor low,Tensor skip,U w,U h,bool low_bytes=false){
  U shift=Shift(66),sx=(shift&1)?4:0,sy=(shift&2)?4:0,ww=w+2*sx,hh=h+2*sy,windows=ww*hh/64;
  auto raw=New(size_t(ww)*hh*16);
  Run("c32_wave1",c32_skip_byte?(low_bytes?"c32_wave1_up_b8_lb":"c32_wave1_up_b8"):(low_bytes?"c32_wave1_up_lb":"c32_wave1_up"),windows,P(low),PackedDecoderHalf("block66-weights.f32",size_t(64)*32),P(skip),PackedC32Weight("block66-ffn.f32",false),PackedC32Weight("block66-attention.f32",true),P(raw),windows,w,h,sx,sy);
  return {Tensor{},Tensor{},raw,ww,hh,sx,sy};
 }
 C32Result C32Chain(Tensor input,const C32Result*prev,U w,U h,U shift,const std::string&fw,const std::string&aw,bool finish,bool down){
  U sx=(shift&1)?4:0,sy=(shift&2)?4:0,ww=w+2*sx,hh=h+2*sy,n=ww*hh,windows=n/64;
  if(opt.c32_finish_fused&&prev&&finish&&down&&opt.down_crop_fused&&C32SkipByte()){c32_skip_byte=true;bool db=HIP_C32_DOWN_BYTE&&opt.pool32_h16w&&opt.fast_mh&&HasFn("c32_wave1","c32_wave1_finish_dcrop_b8d")&&HasFn("mh_fast","mh_pool_project_c32_b8");auto main=New(size_t(w)*h*8),pooled=New(size_t(w/2)*(h/2)*(db?8:32));Run("c32_wave1",db?"c32_wave1_finish_dcrop_b8d":"c32_wave1_finish_dcrop_b8",windows,P(prev->raw),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(main),P(pooled),windows,U(3),U(1),w,h,sx,sy,prev->workw,prev->sx,prev->sy);C32Result r{main,pooled,Tensor{},ww,hh,sx,sy,true};r.down_byte=db;return r;}
  if(opt.c32_finish_fused&&prev&&finish&&!down&&fw=="block69-ffn.f32"&&HIP_C32_POST_LOW_BYTE&&wave_owned_active&&opt.post_merge_fold&&opt.post_head_fused&&HasFn("c32_wave1","c32_wave1_finish_b8")&&HasFn("c32_wave1","c32_wave1_post_b8")){c32_post_low_byte=true;auto main=New(size_t(w)*h*8);Run("c32_wave1","c32_wave1_finish_b8",windows,P(prev->raw),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(main),static_cast<void*>(nullptr),windows,U(3),U(1),w,h,sx,sy,prev->workw,prev->sx,prev->sy);return {main,Tensor{},Tensor{},ww,hh,sx,sy,false};}
  if(opt.c32_finish_fused&&prev&&(finish||down)){if(finish&&down)c32_skip_byte=false;bool dcrop=opt.down_crop_fused&&down;auto main=finish?New(size_t(w)*h*32):Tensor{},pooled=down?New(dcrop?size_t(w/2)*(h/2)*32:size_t(n/4)*32):Tensor{};Run("c32_fused_ffn",dcrop?"c32_fast_ffn_attention_fused_half_chain_finish_dcrop":"c32_fast_ffn_attention_fused_half_chain_finish",windows,P(prev->raw),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(main),P(pooled),windows,U(3),U(1),w,h,sx,sy,prev->workw,prev->sx,prev->sy);return {main,pooled,Tensor{},ww,hh,sx,sy,dcrop};}
  auto raw=New(size_t(n)*16);
  if(prev)Run("c32_fused_ffn","c32_fast_ffn_attention_fused_half_chain",windows,P(prev->raw),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(raw),windows,U(3),U(1),w,h,sx,sy,prev->workw,prev->sx,prev->sy);
  else if(c32_pre_down_byte){c32_pre_down_byte=false;Run("c32_wave1","c32_wave1_mapped_b8",windows,P(input),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(raw),windows,U(0),U(1),w,h,sx,sy);}
  else Run("c32_fused_ffn","c32_fast_ffn_attention_fused_half_mapped",windows,P(input),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(raw),windows,U(0),U(1),w,h,sx,sy);
  auto main=finish?New(size_t(w)*h*32):Tensor{},pooled=down?New(size_t(n/4)*32):Tensor{};
  if(main||pooled)Run("boundary_fast","c32_finish_crop_half",size_t(n)*32,P(raw),P(main),P(pooled),ww,hh,w,h,sx,sy);
  return {main,pooled,raw,ww,hh,sx,sy};
 }
 // Diagnostic (DLSS5_SKIP_BLOCKS on a chain-finishing C32 block): crop/pool the previous block's raw state in its own
 // shift geometry, as the finishing block would have done after its own kernel.
 void SkipChainFinish(C32Result&last,U w,U h,bool down){if(!last.raw)throw std::runtime_error("cannot skip every C32 chain block");auto main=New(size_t(w)*h*32),pooled=down?New(size_t(last.workw)*last.workh/4*32):Tensor{};Run("boundary_fast","c32_finish_crop_half",size_t(last.workw)*last.workh*32,P(last.raw),P(main),P(pooled),last.workw,last.workh,w,h,last.sx,last.sy);last.main=main;last.down=pooled;}
 Tensor AttentionFast(Tensor input,U w,U h,U c,const std::string&aw,bool raw,bool rounded_output=false,U cropw=0,U croph=0,U sx=0,U sy=0,Tensor ready_norm={},bool feature_byte=false,bool out_byte=false,Tensor ready_av={}){
  U n=w*h,windows=n/64,heads=c/32;if(ready_av&&(c!=512||!opt.fp8_av||!opt.fused_mh||ready_av->bytes<size_t(n)*c))throw std::runtime_error("produced C512 AV format/capacity");if(ready_norm&&(!opt.fp8_normalized||ready_norm->bytes<size_t(n)*3*c))throw std::runtime_error("produced QKV format/capacity");auto qkv=(ready_av||opt.fused_qkv_norm)?Tensor{}:New(size_t(n)*3*c),norm=ready_av?Tensor{}:ready_norm?ready_norm:New(size_t(n)*3*c/(opt.fp8_normalized?4:1));auto weights=PackedMhWeight(aw,c,true);
  if(!ready_av&&!ready_norm){if(opt.fused_qkv_norm){if(opt.qkv_norm_wave_c512&&c==512){if(n%16)throw std::runtime_error("C512 wave QKV token count");Run("mh_fast","mh_qkv_normalize_wave_c512",size_t(n)*3*c,P(input),weights,P(norm),n);}else Run("mh_fast","mh_qkv_normalize_fused",size_t(n)*3*c,P(input),weights,P(norm),n,c);}else{
  Run("mh_fast","mh_qkv_fast",size_t(n)*3*c,P(input),weights,P(qkv),n,c);
  Run("mh_fast",opt.fp8_normalized?"mh_qkv_normalize_fast_wave_fp8":opt.mh_wave?"mh_qkv_normalize_fast_wave":"mh_qkv_normalize_fast",size_t(n)*heads,P(qkv),weights,P(norm),n,c);qkv.reset();}}
  if(wave_owned_active&&c==256){
   if(!feature_byte||!ready_norm||!opt.mh_proj_diag_fb)throw std::runtime_error("attention-only production inputs required");
   auto out=New((cropw?size_t(cropw)*croph:size_t(n))*c/(out_byte?4:1));
   unsigned*af=nullptr;unsigned ae=0;const unsigned*ff=pdl_ffn_flags;unsigned fe=pdl_ffn_epoch;
   if(ff){af=PdlSlot(1,c,w,h,8u);ae=pdl_last_target;pdl_anyorder=true;}
   Run("c64_wave2",out_byte?"c256_attn_wave_bo":"c256_attn_wave",windows,P(norm),WaveOwnedAttentionWeight(aw,c),P(input),P(out),cropw?cropw:w,cropw?croph:h,w,h,sx,sy,U(raw?3:rounded_output?0:4),ff,fe,af);
   pdl_anyorder=false;pdl_prev=ff?PdlPrev{af,ae,w,sx,sy}:PdlPrev{};pdl_ffn_flags=nullptr;return out;
  }
  if((c==64||c==128||c==256)&&opt.packed_weights&&opt.fused_mh&&opt.fp8_av&&opt.fp8_normalized){if(out_byte&&(raw||!feature_byte))throw std::runtime_error("byte attention output needs lattice output and byte feature");auto out=New((cropw?size_t(cropw)*croph:size_t(n))*c/(out_byte?4:1));std::string project=c==64?"c64_attention_project":c==128?"c128_attention_project":"c256_attention_project";if(feature_byte)project+="_fb";if(out_byte)project+="_bout";if(opt.mh_attn_w16&&c==64&&!feature_byte&&!out_byte)project+="_w16";if(opt.mh_proj_diag&&!feature_byte&&!out_byte){project+="_diag";weights=PackedMhWeightDiag(aw,c);}else if(opt.mh_proj_diag_fb&&feature_byte){project+="_diag";weights=PackedMhWeightDiag(aw,c);}if(pdl_ffn_flags&&(project.size()>8&&project.compare(project.size()-8,8,"_fb_diag")==0||project.size()>13&&project.compare(project.size()-13,13,"_fb_bout_diag")==0)){
    unsigned*aflags=PdlSlot(1,c,w,h,c==64?8u:16u);unsigned aepoch=pdl_last_target;const unsigned*ff=pdl_ffn_flags;unsigned fe=pdl_ffn_epoch;pdl_anyorder=true;
    Run("mh_fused",(project+"_pdl").c_str(),windows,P(norm),weights,P(input),P(out),w,h,U(raw?3:rounded_output?0:4),cropw,croph,sx,sy,ff,fe,aflags,aepoch);pdl_anyorder=false;
    pdl_prev={aflags,aepoch,w,sx,sy};pdl_ffn_flags=nullptr;return out;}
   pdl_prev={};pdl_ffn_flags=nullptr;Run("mh_fused",project.c_str(),windows,P(norm),weights,P(input),P(out),w,h,U(raw?3:rounded_output?0:4),cropw,croph,sx,sy);return out;}
  if(feature_byte||out_byte)throw std::runtime_error("byte feature/output requires the fused attention-project path");
  auto av=ready_av?ready_av:New(size_t(n)*c/(opt.fp8_av?4:1)),out=New((cropw?size_t(cropw)*croph:size_t(n))*c);if(!ready_av){if(opt.fused_mh){Run("mh_fused",opt.fp8_av?"mh_attention_fused_fp8_out":opt.fp8_normalized?"mh_attention_fused_fp8":"mh_attention_fused",size_t(windows)*heads,P(norm),weights,P(av),w,h,c);norm.reset();}else{
  auto ex=New(size_t(windows)*heads*4096),prob=New(size_t(windows)*heads*4096);
  Run("mh_fast","mh_scores_exp_fast",size_t(windows)*heads*4096,P(norm),weights,P(ex),w,h,c);
  Run("mh_fast","mh_probabilities_fast",size_t(windows)*heads*1024,P(ex),P(prob),windows,c);ex.reset();
  Run("mh_fast","mh_attention_av_fast",size_t(n)*c,P(prob),P(norm),P(av),w,h,c);prob.reset();norm.reset();
  }
  } // ready_av already contains the fused QKV/normalization/attention output.
  if(cropw)if(opt.c512_proj_frag&&c==512)Run("mh_fast","mh_attention_project_frag_c512",size_t(n)*c,P(av),P(input),PackedMhWeightQkvFrag(aw,512),P(out),n,U(raw?3:0),cropw,croph,w,sx,sy);else Run("mh_fast","mh_attention_crop",size_t(n)*c,P(av),P(input),weights,P(out),n,U(raw?3:(c==512||rounded_output)?0:4),cropw,croph,w,sx,sy,c);
  else Run("mh_fast",opt.fp8_av?(c==512?"mh_attention_project_fast_scalar_fp8":"mh_attention_project_fast_matrix_fp8"):(c==512?"mh_attention_project_fast_scalar":"mh_attention_project_fast_matrix"),size_t(n)*c,P(av),P(input),weights,P(out),n,c,U(raw?3:(c==512||rounded_output)?0:4));return out;
 }
 Tensor Attention(Tensor input,U w,U h,U c,const std::string&aw,bool raw,bool rounded_output=false){if(opt.fast_mh)return AttentionFast(input,w,h,c,aw,raw,rounded_output);U n=w*h,windows=n/64,heads=c/32;auto qkv=New(size_t(n)*3*c),qk=New(size_t(n)*2*c);Run("mh","mh_qkv",size_t(n)*3*c,P(input),Weight(aw),P(qkv),n,c);Run("mh","mh_normalize",size_t(n)*heads,P(qkv),Weight(aw),P(qk),n,c);auto ex=New(size_t(windows)*heads*4096),prob=New(size_t(windows)*heads*4096);Run("mh","mh_scores_exp",size_t(windows)*heads*4096,P(qk),Weight(aw),P(ex),w,h,c);qk.reset();Run("mh","mh_probabilities",size_t(windows)*heads*64,P(ex),P(prob),windows,c);ex.reset();auto av=New(size_t(n)*c);Run("mh","mh_attention_av",size_t(n)*c,P(prob),P(qkv),P(av),w,h,c);prob.reset();qkv.reset();auto out=New(size_t(n)*c);Run("mh","mh_attention_project",size_t(n)*c,P(av),P(input),Weight(aw),P(out),n,c,U(raw));return out;}
 /* byte_in/byte_out (option mh_byte_stream, C64/128/256 chains only): the residual stream between consecutive blocks of a
    chain is the E4M3 byte of the lattice-exact block output; chain heads read the f32 tensor from pool/Up, chain tails
    (raw Hrtz outputs, Down/skip consumers) still write f32. Inside a block the FFN feature is always bytes on this path. */
 Tensor CompactC512Body(Tensor input,U w,U h,U ww,U hh,U sx,U sy,U block,bool raw){
  U valid=w*h,n=(valid+15)&~15u;
  auto compact=input;
  if(n!=valid&&!(HIP_C512_PAD16&&input->capacity>=size_t(n)*512*4)){compact=New(size_t(n)*512);Run("mh","mh_shift_pack",size_t(n)*512,P(input),P(compact),valid,U(1),n,U(1),U(0),U(0),U(512),U(0));}
  auto mixed=New(size_t(n)*512),contract=New(size_t(n)*512),contract8=New(size_t(n)*128),ffn=New(size_t(n)*512),ffn8=New(size_t(n)*128);
  const bool fpf=HIP_C512_FFN_PROJ_FUSE&&HIP_C512_FFN_ONE&&HIP_C512_FFN_F8W&&!HIP_C512_PROJ_FB8&&HasFn("c512_m32_deep","split_ffn_proj_fused");
  if(fpf)Run("c512_m32_deep","split_ffn_proj_fused",size_t(n)*512,P(compact),PackedSplitFfnWeightMixFp8(Block(block,"ffwd")),PackedSplitProjectionFrag(Block(block,"ffwd-projection")),P(ffn),P(ffn8),n);
  else if(HIP_C512_FFN_ONE&&HIP_C512_FFN_F8W&&HasFn("c512_m32_deep","split_ffn_one_w2f8"))Run("c512_m32_deep","split_ffn_one_w2f8",size_t((n+31)/32*32)*256,P(compact),PackedSplitFfnWeightMixFp8(Block(block,"ffwd")),P(contract8),n);
  else if(HIP_C512_FFN_ONE&&HasFn("c512_m32_deep","split_ffn_one_w2"))Run("c512_m32_deep","split_ffn_one_w2",size_t((n+31)/32*32)*256,P(compact),PackedSplitFfnWeightMixHalf(Block(block,"ffwd")),P(contract8),n);
  else if(HIP_C512_FFN_ONE&&HasFn("c512_m32_deep","split_ffn_one_m32"))Run("c512_m32_deep","split_ffn_one_m32",size_t((n+31)/32*32)*256,P(compact),PackedSplitFfnWeightMixHalf(Block(block,"ffwd")),P(contract8),n);
  else{Run("c512_m32_deep","split_mix_blocked_h16w_m32",size_t((n+31)/32*32)*256,P(compact),PackedSplitFfnWeightMixHalf(Block(block,"ffwd")),P(mixed),n);
  Run("deep","split_ffn_fused_fp8_t8",size_t(n)*512,P(mixed),PackedSplitFfnWeightMixHalf(Block(block,"ffwd")),P(contract),P(contract8),n);}
  mixed.reset();
  const bool fb8=HIP_C512_PROJ_FB8&&HasFn("deep_fast","split_projection_frag_nof32")&&HasFn("mh_fast","mh_attention_project_frag_c512_fb8");
  if(!fpf)Run("deep",fb8?"split_projection_frag_nof32":"split_projection_frag",size_t(n)*512,P(contract8),PackedSplitProjectionFrag(Block(block,"ffwd-projection")),P(compact),P(ffn),P(ffn8),n);
  compact.reset();contract.reset();contract8.reset();
  auto av=New(size_t(n)*128);
  Run("c512_m32_mh","c512_qkv_attention_compact",size_t(ww)*hh/64*16,P(ffn8),PackedMhWeightQkvFrag(Block(block,"attention"),512),P(av),w,h,ww,hh,sx,sy);
  if(!fb8)ffn8.reset();
  auto result=HIP_C512_PAD16?NewPad16(valid,512):New(size_t(valid)*512);
  Run("mh_fast",fb8?"mh_attention_project_frag_c512_fb8":"mh_attention_project_frag_c512",size_t(n)*512,P(av),fb8?P(ffn8):P(ffn),PackedMhWeightQkvFrag(Block(block,"attention"),512),P(result),n,U(raw?3:0),w,h,w,U(0),U(0));
  Stage("block"+std::to_string(block),result);return result;
 }
#include "swin_persistent_network.h"
 Tensor Body(Tensor input,U w,U h,U c,U shift,U block,bool raw,bool byte_in=false,bool byte_out=false,bool half_out=false,Tensor*skip_b8=nullptr){if(PdlChainHead(block))pdl_prev={};pdl_ffn_flags=nullptr;if(opt.skip_blocks.count(block)){Stage("block"+std::to_string(block),input);return input;}U sx=(shift&1)?4:0,sy=(shift&2)?4:0,ww=(w+sx+7)&~7u,hh=(h+sy+7)&~7u,n=ww*hh;
 if(c==512&&c512_m32_active&&opt.fast_deep&&opt.packed_weights&&opt.split_mix_h16w&&!opt.split_mix_fused&&opt.split_ffn_fused&&opt.c512_proj_tiles&&opt.c512_qkv_frag&&opt.c512_proj_frag&&opt.fp8_av&&opt.fused_mh&&opt.fp8_normalized&&!byte_in&&!byte_out&&ww%8==0&&hh%8==0)return CompactC512Body(input,w,h,ww,hh,sx,sy,block,raw);
 const bool identity=opt.elide_identity_shift&&!sx&&!sy&&ww==w&&hh==h;bool mapped=opt.mh_input_mapped&&c!=512&&!identity;const bool byte_feature=(opt.mh_byte_stream||opt.mh_feature_byte)&&c!=512;if((byte_in||byte_out)&&!byte_feature)throw std::runtime_error("byte residual stream requires the mh_byte_stream C64/128/256 path");if(byte_in&&!(identity||mapped))throw std::runtime_error("byte block input requires mapped/identity FFN input");if(byte_out&&(raw||!(identity||opt.mh_project_crop)))throw std::runtime_error("byte block output requires a lattice output on the crop/identity path");if(wave_owned_active&&(c==64||c==128||(c==256&&((opt.width==1920&&(opt.height==1152||opt.height==1088))||(opt.width==2560&&opt.height==1472&&(!std::getenv("DLSS5_LAB_C2561440")||std::strcmp(std::getenv("DLSS5_LAB_C2561440"),"0"))))))){
  // C256 whole-block fusion wins at 1080; retain the split PDL path at smaller tiers.
  if(!(identity||mapped)||!opt.grouped_mh_contract||!opt.packed_weights||!byte_feature||!opt.mh_proj_diag_fb)throw std::runtime_error("wave-owned block input contract");
  pdl_prev={};pdl_ffn_flags=nullptr;pdl_anyorder=false;
  if(half_out&&(!raw||byte_out||!byte_in||c==256))throw std::runtime_error("half tail contract");
  auto out=New(size_t(w)*h*c/(byte_out?4:half_out?2:1));
  std::string name="c"+std::to_string(c)+"_wave2"+(byte_in?"_bi":"")+(byte_out?"_bo":"")+(half_out?"_ho":"");
  const bool w16=!half_out&&(c==256?HIP_C256_FFN_W16:HIP_SMALL_FFN_W16)&&HasFn("c64_wave2",name+"_w16");if(w16)name+="_w16";
#if HIP_SWIN_PERSISTENT_DIAGNOSTICS
  {static bool shown[3]{};U si=c==64?0:c==128?1:2;if(!shown[si]){shown[si]=true;std::printf("W2_C%u %s\n",c,name.c_str());}}
#endif
  if(skip_b8){if(!half_out)throw std::runtime_error("byte skip copy needs the half tail");*skip_b8=New(size_t(w)*h*c/4);name+="b"; /* *_bi_hob */
   Run("c64_wave2",name.c_str(),n/64,P(input),PackedFusedMhWeightFrag(Block(block,"ffn"),c),WaveOwnedAttentionWeight(Block(block,"attention"),c),P(out),w,h,ww,hh,sx,sy,U(3),P(*skip_b8));}
  else
  Run("c64_wave2",name.c_str(),n/64,P(input),w16?PackedFusedMhWeightFragW16(Block(block,"ffn"),c):PackedFusedMhWeightFrag(Block(block,"ffn"),c),WaveOwnedAttentionWeight(Block(block,"attention"),c),P(out),w,h,ww,hh,sx,sy,U(raw?3:(block==48||block==55||block==61||block==65)?0:4));
  Stage("block"+std::to_string(block),out);return out;
 }
 auto packed=(identity||mapped)?input:New(size_t(n)*c);if(!identity&&!mapped)Run("mh","mh_shift_pack",size_t(n)*c,P(input),P(packed),w,h,ww,hh,sx,sy,c,U(0));Tensor ffn=New(byte_feature?size_t(n)*c/4:size_t(n)*c);Tensor producer_norm=(opt.ffn_qkv&&c<=opt.ffn_qkv_max_c&&c!=512)?New(size_t(n)*3*c/4):Tensor{};
 if(opt.mh_window_fused&&c==64&&producer_norm&&opt.fused_mh_ffn&&opt.fused_ffn_project&&opt.grouped_mh_contract&&!opt.mh_ffn_frag&&!(opt.tiled_mh_ffn&&c>=TiledMin())&&!byte_feature&&opt.mh_proj_diag&&opt.packed_weights&&opt.fused_mh&&opt.fp8_av&&opt.fp8_normalized&&(identity||mapped)&&!opt.mh_attn_w16){
  /* 2026-09-17 experiment: the whole C64 block per 8x8 window — the FFN/QKV producer and the diag attention-project in one kernel, normalized bytes and feature never leave LDS. Same kernels' arithmetic; bit-exact by construction, verified on the goldens. */
  if(n%64)throw std::runtime_error("window fused token count");const bool crop=opt.mh_project_crop&&!identity;
  auto out=New((crop?size_t(w)*h:size_t(n))*c/(byte_out?4:1));std::string name=std::string("c64_window_fused")+(mapped?"_mapped":"")+(byte_in?"_bytein":"")+(byte_out?"_bout":"");
  Run("mh_window",name.c_str(),n/64,P(packed),PackedFusedMhWeight(Block(block,"ffn"),c),PackedMhWeightDiag(Block(block,"attention"),c),P(out),w,h,ww,hh,sx,sy,U(raw?3:(block==48||block==55||block==61||block==65)?0:4),U(crop?1:0));
  packed.reset();auto res=(identity||crop)?out:New(size_t(w)*h*c);if(!identity&&!crop)Run("mh","mh_shift_crop",size_t(w)*h*c,P(out),P(res),w,h,ww,sx,sy,c);Stage("block"+std::to_string(block),res);return res;}
 Tensor fused_c512_av;
 if(c==512){auto mixed=New(size_t(n)*512),hidden=opt.split_ffn_fused?Tensor{}:New(size_t(n)*(opt.fp8_deep?512:2048)),contract=New(size_t(n)*512);auto fw=Weight(Block(block,"ffwd"));Tensor contract8;if(opt.split_mix_fused){if(n%16)throw std::runtime_error("fused split mix token count");Run("deep","split_ffn_fused_fp8_mix",size_t(n)*512,P(packed),PackedSplitFfnWeight(Block(block,"ffwd")),P(contract),n);}else{if(opt.split_mix_h16w){if(c512_m32_active)Run("c512_m32_deep","split_mix_blocked_h16w_m32",size_t((n+31)/32*32)*256,P(packed),PackedSplitFfnWeightMixHalf(Block(block,"ffwd")),P(mixed),n);else Run("deep","split_mix_blocked_h16w",size_t(n)*512,P(packed),PackedSplitFfnWeightMixHalf(Block(block,"ffwd")),P(mixed),n);}else Run("deep",opt.split_mix_blocked?"split_mix_blocked":"split_mix",size_t(n)*512,P(packed),fw,P(mixed),n);if(opt.split_ffn_fused){if(opt.c512_proj_tiles){contract8=New(size_t(n)*128);Run("deep","split_ffn_fused_fp8_t8",size_t(n)*512,P(mixed),opt.split_mix_h16w?PackedSplitFfnWeightMixHalf(Block(block,"ffwd")):PackedSplitFfnWeight(Block(block,"ffwd")),P(contract),P(contract8),n);}else Run("deep",opt.packed_weights?"split_ffn_fused_fp8":"split_ffn_fused",size_t(n)*512,P(mixed),opt.packed_weights?(opt.split_mix_h16w?PackedSplitFfnWeightMixHalf(Block(block,"ffwd")):PackedSplitFfnWeight(Block(block,"ffwd"))):fw,P(contract),n);mixed.reset();}else{Run("deep",opt.fp8_deep?"split_expand_fp8":"split_expand",size_t(n)*2048,P(mixed),fw,P(hidden),n);mixed.reset();Run("deep",opt.fp8_deep?"split_contract_fp8":"split_contract",size_t(n)*512,P(hidden),fw,P(contract),n);}}hidden.reset();if(opt.c512_qkv_frag){auto ffn8=New(size_t(n)*128);if(opt.c512_proj_tiles)Run("deep","split_projection_frag",size_t(n)*512,P(contract8),PackedSplitProjectionFrag(Block(block,"ffwd-projection")),P(packed),P(ffn),P(ffn8),n);else Run("deep","split_projection_blocked_t8",size_t(n)*512,P(contract),PackedDeepWeight(Block(block,"ffwd-projection"),262144),P(packed),P(ffn),P(ffn8),n);if(c512_m32_active&&opt.fp8_av&&opt.fused_mh&&opt.fp8_normalized&&ww%8==0&&hh%8==0){
 fused_c512_av=New(size_t(n)*128);
 Run("c512_m32_mh","c512_qkv_attention_fused",size_t(n/64)*16,P(ffn8),PackedMhWeightQkvFrag(Block(block,"attention"),512),P(fused_c512_av),ww,hh);
 }else{producer_norm=New(size_t(n)*384);if(c512_m32_active)Run("c512_m32_mh","mh_qkv_normalize_frag_c512_m32",size_t((n+31)/32*32)*768,P(ffn8),PackedMhWeightQkvFrag(Block(block,"attention"),512),P(producer_norm),n);else Run("mh_fast","mh_qkv_normalize_frag_c512",size_t(n)*1536,P(ffn8),PackedMhWeightQkvFrag(Block(block,"attention"),512),P(producer_norm),n);}}else Run("deep",opt.split_project_blocked?"split_projection_blocked":"split_projection",size_t(n)*512,P(contract),PackedDeepWeight(Block(block,"ffwd-projection"),262144),P(packed),P(ffn),n);}
 else{auto hidden=opt.fused_mh_ffn?Tensor{}:New(size_t(n)*c*(opt.fp8_ffn?1:4)),middle=opt.fused_ffn_project?Tensor{}:New(size_t(n)*c/(opt.fp8_middle?4:1));const bool ffn_frag=((opt.mh_ffn_frag&&c<=128)||(opt.mh_ffn_frag256&&c==256&&producer_norm&&byte_feature))&&opt.fused_mh_ffn&&opt.fused_ffn_project&&opt.grouped_mh_contract;auto fw=ffn_frag?PackedFusedMhWeightFrag(Block(block,"ffn"),c):opt.fast_mh?PackedFusedMhWeight(Block(block,"ffn"),c):Weight(Block(block,"ffn"));if(opt.fused_mh_ffn){auto name="mh_ffn_fused_c"+std::to_string(c)+(ffn_frag?"_frag":opt.tiled_mh_ffn&&c>=TiledMin()?"_tiled":"")+(opt.fused_ffn_project?"_project":"")+(mapped?"_mapped":"")+(opt.grouped_mh_contract?"_g128":"")+(producer_norm?"_qkv":"");if(byte_feature){if(!producer_norm)throw std::runtime_error("byte feature requires the fused FFN/QKV kernel");name+=byte_in?"_bytein_fb":"_fb";}else if(producer_norm&&opt.ffn_qkv_batched_norm&&c<=128)name+="_bn";if(producer_norm){void*qw=ffn_frag?PackedMhWeightQkvFragOnly(Block(block,"attention"),c):PackedMhWeight(Block(block,"attention"),c,true);
   if(pdl_mode&&(c==64||c==128||c==256)&&byte_feature&&(identity||mapped)&&(identity||opt.mh_project_crop)){
    const bool head=!pdl_prev.flags;unsigned*fflags=PdlSlot(0,c,ww,hh,c*2/32);unsigned fepoch=pdl_last_target;const unsigned*pflags=head?nullptr:pdl_prev.flags;unsigned pepoch=head?0u:pdl_prev.epoch,pww=head?8u:pdl_prev.ww,psx=head?0u:pdl_prev.sx,psy=head?0u:pdl_prev.sy;
    pdl_anyorder=!head;Run("mh_fast",(name+"_pdl").c_str(),size_t(n)*c,P(packed),fw,qw,P(ffn),P(producer_norm),n,w,h,ww,sx,sy,pflags,pepoch,pww,psx,psy,fflags,fepoch);pdl_anyorder=false;pdl_ffn_flags=fflags;pdl_ffn_epoch=fepoch;}
   else Run("mh_fast",name.c_str(),size_t(n)*c,P(packed),fw,qw,P(ffn),P(producer_norm),n,w,h,ww,sx,sy);}else if(mapped)Run("mh_fast",name.c_str(),size_t(n)*c,P(packed),fw,P(ffn),n,w,h,ww,sx,sy);else Run("mh_fast",name.c_str(),size_t(n)*c,P(packed),fw,P(opt.fused_ffn_project?ffn:middle),n);}else{Run(opt.fast_mh?"mh_fast":"mh",opt.fp8_ffn?"mh_ffn_expand_fast_fp8":opt.fast_mh?"mh_ffn_expand_fast":"mh_ffn_expand",size_t(n)*c*4,P(packed),fw,P(hidden),n,c);Run(opt.fast_mh?"mh_fast":"mh",opt.fp8_middle?"mh_ffn_contract_fast_fp8_out":opt.fp8_ffn?"mh_ffn_contract_fast_fp8":opt.fast_mh?"mh_ffn_contract_fast":"mh_ffn_contract",size_t(n)*c,P(hidden),fw,P(middle),n,c);hidden.reset();}if(!opt.fused_ffn_project)Run(opt.fast_mh?"mh_fast":"mh",opt.fp8_middle?"mh_ffn_project_fast_fp8":opt.fast_mh?"mh_ffn_project_fast":"mh_ffn_project",size_t(n)*c,P(middle),P(packed),fw,P(ffn),n,c);}packed.reset();bool crop=opt.mh_project_crop&&!identity;auto attended=(crop||producer_norm||fused_c512_av)?AttentionFast(ffn,ww,hh,c,Block(block,"attention"),raw,block==48||block==55||block==61||block==65,crop?w:0,crop?h:0,sx,sy,producer_norm,byte_feature,byte_out,fused_c512_av):Attention(ffn,ww,hh,c,Block(block,"attention"),raw,block==48||block==55||block==61||block==65);auto out=(identity||crop)?attended:New(size_t(w)*h*c);if(!identity&&!crop)Run("mh","mh_shift_crop",size_t(w)*h*c,P(attended),P(out),w,h,ww,sx,sy,c);if(pdl_mode){pdl_keep.push_back(input);pdl_keep.push_back(ffn);pdl_keep.push_back(producer_norm);pdl_keep.push_back(out);while(pdl_keep.size()>32)pdl_keep.pop_front();}Stage("block"+std::to_string(block),out);return out;}
 // Downsample projection weights for mh_pool_project_production_h16w: c==32 -> E4M3 bytes of the clamped RNE cast (pack()),
 // c>=64 -> RNE half; the [2c][c] matrix is the first 2c*c floats of the file, the rest is left as is.
 void* PackedDsWeightCast(const std::string&name,U c){auto key=name+"@ds-cast";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()<size_t(2)*c*c)throw std::runtime_error("downsample weight shape");size_t n=size_t(2)*c*c;if(c==32){auto*b=reinterpret_cast<uint8_t*>(v.data());std::vector<uint8_t>packed(n);for(size_t i=0;i<n;i++){float x=v[i];if(x>448.f)x=448.f;if(x<-448.f)x=-448.f;packed[i]=ExactWeightFp8(HostF(x));}std::memcpy(b,packed.data(),n);dead.push_back({n,4*n});}else HalfR(v,0,n);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 // Downsample projection weights [2c][c] as 16x16 f16 B fragment tiles (mh_pool_project_group_c*): tile (nt*(c/16)+kt), lane (g,rc) holds k=g*8..+8 of column nt*16+rc.
 void* PackedDsWeightFrag(const std::string&name,U c){auto key=name+"@ds-frag";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()<size_t(2)*c*c)throw std::runtime_error("downsample weight shape");size_t N=size_t(2)*c,K=c;std::vector<uint16_t>t(N*K);for(size_t nt=0;nt<N/16;nt++)for(size_t kt=0;kt<K/16;kt++)for(unsigned g=0;g<2;g++)for(unsigned rc=0;rc<16;rc++)for(unsigned e=0;e<8;e++)t[(nt*(K/16)+kt)*256+(g*16+rc)*8+e]=ExactWeightHalf(v[(nt*16+rc)*K+kt*16+g*8+e]);v.resize(size_t(c)*c);std::memcpy(v.data(),t.data(),t.size()*2);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 void* PackedDsWeight(const std::string&name,U c){auto key=name+"@ds-f16";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()<size_t(2)*c*c)throw std::runtime_error("downsample weight shape");v.resize(size_t(2)*c*c);PackHalfMatrix(v,size_t(2)*c*c);v.resize(size_t(c)*c);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 Tensor Down(Tensor raw,U w,U h,U c,const std::string&file,bool head=false,bool half_in=false){if(half_in&&!(opt.pool_project_group&&(c==64||c==128)))throw std::runtime_error("half pool contract");U ow=w/2,oh=h/2,vw=0,vh=0;if(head){U rw=w/2,rh=h/2;if(free_geometry){ow=(rw+3)/4*4;oh=(rh+3)/4*4;}else if(w==60&&(h==36||h==34)){ow=32;oh=20;}else{ow=rw;oh=(rw*rh)%16?rh+1:rh;}if(ow*oh!=rw*rh){vw=rw;vh=rh;}}if(opt.pool_project_group&&(c==64||c==128||c==256||(head&&c==512))){auto out=(HIP_C512_PAD16&&c==256)?NewPad16(size_t(ow)*oh,512):New(size_t(ow)*oh*c*2);Run("mh_fast",("mh_pool_project_group_c"+std::to_string(c)+(head&&c==512&&gather_fold?"_gout":"")+(half_in?"_hin":"")).c_str(),size_t(ow)*oh*c*2,P(raw),PackedDsWeightFrag(file,c),P(out),ow,oh,w,vw,vh);if(c==256&&!head&&!pdl_anyorder)pulse_ordered_down_c256=true;return out;}
  if(opt.pool_project_fused&&(c==64||c==128||c==256)){if((ow*oh)%16)throw std::runtime_error("fused pool project token count");auto out=New(size_t(ow)*oh*c*2);Run("mh_fast",("mh_pool_project_fused_c"+std::to_string(c)).c_str(),size_t(ow)*oh*c*2,P(raw),PackedDsWeight(file,c),P(out),ow,oh,w,vw,vh);return out;}auto pooled=New(size_t(ow)*oh*c),out=New(size_t(ow)*oh*c*2);Run("mh","mh_pool",size_t(ow)*oh*c,P(raw),P(pooled),ow,oh,w,vw,vh,c);if(opt.pool_project_h16w&&opt.fast_mh)Run("mh_fast","mh_pool_project_production_h16w",size_t(ow)*oh*c*2,P(pooled),PackedDsWeightCast(file,c),P(out),ow,oh,vw,vh,c);else Run(opt.fast_mh?"mh_fast":"mh",opt.fast_mh?"mh_pool_project_production":"mh_pool_project",size_t(ow)*oh*c*2,P(pooled),Weight(file),P(out),ow,oh,vw,vh,c);return out;}
 Tensor adaptive_anchor_in,adaptive_anchor_out,adaptive_gain,adaptive_stats,adaptive_state;
 /* DLSS5_VIT_ADAPTIVE_IDLE_MS (2026-10-01, c128-c64-inchain AE ledger): the adaptive state resets after this much wall-clock idle between
    frames (default 500 = unchanged production behaviour). Regression bit-exact checks set it very large so a host stall cannot reset the
    decisions and make adaptive.csv differ between otherwise identical runs. */
 static long long AdaptiveIdleMs(){static const long long ms=[]{const char*v=std::getenv("DLSS5_VIT_ADAPTIVE_IDLE_MS");if(!v||!*v)return 500LL;char*e=nullptr;long long x=std::strtoll(v,&e,10);return (e&&!*e&&x>=0)?x:500LL;}();return ms;}
 Tensor adaptive_image_anchor,adaptive_image_signature,adaptive_image_delta;void*adaptive_image=nullptr;
 void*adaptive_prev_history=nullptr;void*adaptive_prev_input=nullptr;U adaptive_prev_seed=0;
 bool adaptive_allowed=true,adaptive_dirty=false,adaptive_key_down=false,adaptive_user_disabled=false;U adaptive_last_mode=0,adaptive_frame=0;
 bool adaptive_active=false;U adaptive_n=0;std::chrono::steady_clock::time_point adaptive_last{};
 Tensor AdaptiveVitGroup(Tensor input,U n){
  const char*mode_s=std::getenv("DLSS5_VIT_ADAPTIVE");U mode=adaptive_allowed&&mode_s?U(std::stoul(mode_s)):0;
  const char*hotkey=std::getenv("DLSS5_VIT_REUSE_HOTKEY");
  if(opt.integration.allow_vit_hotkey&&hotkey&&!strcmp(hotkey,"1")){bool down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;if(down&&!adaptive_key_down)adaptive_user_disabled=!adaptive_user_disabled;adaptive_key_down=down;if(adaptive_user_disabled)mode=0;}
  AdaptivePreviewState.store(mode?1:0);if(mode!=adaptive_last_mode){adaptive_dirty=true;adaptive_last_mode=mode;}
  if(multi_pass>1)mode=0;
  if(!mode){Tensor full=input;for(U b=31;b<=38;b++)full=Vit(full,n,b);return full;}++adaptive_frame;
  if(mode>3||opt.graph||!opt.fast_deep||!opt.pooled||opt.vit_byte_stream||std::any_of(opt.skip_blocks.begin(),opt.skip_blocks.end(),[](U b){return b>=31&&b<=38;}))throw std::runtime_error("adaptive ViT requires fast pooled graph-off non-byte-stream unskipped network, mode1..3");
  auto param=[](const char*key,float fallback){const char*v=std::getenv(key);float f=v?std::stof(v):fallback;if(!std::isfinite(f)||f<0)throw std::runtime_error("adaptive threshold");return f;};
  float global=param("DLSS5_VIT_REUSE_GLOBAL",.22f),local=param("DLSS5_VIT_REUSE_LOCAL",1.f),image_limit=param("DLSS5_VIT_REUSE_IMAGE",.35f);
  U period=4;if(const char*v=std::getenv("DLSS5_VIT_REUSE_PERIOD"))period=U(std::stoul(v));if(period<1||period>16)throw std::runtime_error("adaptive max period1..16");
  auto now=std::chrono::steady_clock::now();bool reset=adaptive_dirty||!adaptive_state||adaptive_n!=n||(adaptive_last.time_since_epoch().count()&&now-adaptive_last>std::chrono::milliseconds(AdaptiveIdleMs()));adaptive_last=now;adaptive_dirty=false;
  if(!adaptive_gain){
   std::vector<float>gain(1024,1.f);
   if(const char*path=std::getenv("DLSS5_VIT_REUSE_GAIN");path&&*path){auto bytes=ReadBytes(path);if(bytes.size()!=4096)throw std::runtime_error("adaptive gain shape");memcpy(gain.data(),bytes.data(),4096);}
   else{std::vector<double>product(1024,1.);for(U block=31;block<=38;block++)for(const char*part:{"contract","projection"}){auto weight=ReadWeights(opt.assets+"/block"+std::to_string(block)+"-"+part+".f32");size_t matrix=!strcmp(part,"contract")?4194304:1048576;if(weight.size()!=matrix+1024)throw std::runtime_error("adaptive gain weight shape");for(U i=0;i<1024;i++)product[i]*=double(weight[matrix+i]);}for(U i=0;i<1024;i++)gain[i]=float(product[i]);}
   for(float v:gain)if(!std::isfinite(v))throw std::runtime_error("adaptive gain nonfinite");adaptive_gain=Upload(gain.data(),4096,true);
  }
  if(reset){
   // PrepareStagedKernels warms the first allocation before producer waits.
   // Same-geometry resets must not free old anchors or synchronously upload:
   // queued frames may still use them, waiting for a producer not yet submitted.
   if(!adaptive_state||adaptive_n!=n){
    adaptive_state=std::make_shared<Allocation>(api,8*sizeof(U));
    adaptive_anchor_in=New(size_t(n)*1024);adaptive_anchor_out=New(size_t(n)*1024);adaptive_stats=New(size_t(n)*4);adaptive_n=n;
    U tiles=((W+31)/32)*((H+31)/32);adaptive_image_anchor=New(size_t(tiles)*3);adaptive_image_signature=New(size_t(tiles)*3);adaptive_image_delta=New(tiles);
   }
   api.Check(api.hipMemsetAsync(P(adaptive_state),0,8*sizeof(U),stream),"adaptive reset");
  }
  U stride=free_geometry?(W/64+3)/4*4:W==1920?32:W/64,tiles=((W+31)/32)*((H+31)/32);
  if(!adaptive_image)throw std::runtime_error("adaptive image missing");
  Run("deep","reuse_image_stats",size_t(tiles)*256,adaptive_image,P(adaptive_image_anchor),P(adaptive_state),P(adaptive_image_signature),P(adaptive_image_delta),W,H);
  Run("deep","reuse_token_stats",size_t(n)*256,P(input),P(adaptive_anchor_in),P(adaptive_state),P(adaptive_stats),n,W/64,H/64,stride);
  Run("deep","reuse_decide",256,P(adaptive_stats),P(adaptive_state),n,period,global,local,mode,P(adaptive_image_delta),tiles,(W+31)/32,image_limit);
  Tensor full=input;adaptive_active=true;
  try{for(U block=31;block<=38;block++)full=Vit(full,n,block);}catch(...){adaptive_active=false;throw;}adaptive_active=false;
  auto result=New(size_t(n)*1024);Run("deep","reuse_finish",size_t(n)*1024,P(input),P(full),P(adaptive_anchor_in),P(adaptive_anchor_out),P(adaptive_gain),P(adaptive_state),P(result),U(n*1024),P(adaptive_image_signature),P(adaptive_image_anchor),U(tiles*3));
  if(const char*log=std::getenv("DLSS5_VIT_ADAPTIVE_LOG")){if(*log){Synchronize();U state[8];api.Check(api.hipMemcpy(state,P(adaptive_state),sizeof(state),2),"adaptive diagnostic");float g,l,image_peak;memcpy(&image_peak,state+7,4);memcpy(&g,state+4,4);memcpy(&l,state+5,4);FILE*f=fopen(log,"ab");if(!f)throw std::runtime_error("adaptive log open");fprintf(f,"%u,%u,%u,%u,%.9g,%.9g,%.9g\n",adaptive_frame,state[0],state[1],state[6],g,l,image_peak);fclose(f);}}
  return result;
 }

 Tensor Gather(Tensor input,U n,bool inverse){auto it=gather_maps[inverse].find(n);if(it==gather_maps[inverse].end()){std::vector<U>map(size_t(n)*1024);for(U t=0;t<n;t++)for(U c=0;c<1024;c++){U raster=(t&~15u)|((t&1u)<<3)|((t&14u)>>1),ch=(c&~31u)|((c&1u)<<1)|((c&2u)>>1)|((c&4u)<<2)|((c&24u)>>1),to=t*1024+c,from=raster*1024+ch;if(inverse)map[from]=to;else map[to]=from;}it=gather_maps[inverse].emplace(n,Upload(map.data(),map.size()*4,true)).first;}auto out=New(size_t(n)*1024);Run("deep","vit_gather",size_t(n)*1024,P(input),P(it->second),P(out),n*1024);return out;}
 Tensor vit_in8; // byte stream: block input bytes produced by the previous block's vit_project_bytein_bout
 Tensor Vit(Tensor input,U n,U block){if(block==31)vit_in8.reset();if(opt.skip_blocks.count(block)){vit_in8.reset();Stage("block"+std::to_string(block),input);return input;}Tensor expanded_input=input;if(opt.vit_byte_stream&&vit_in8)expanded_input=vit_in8;else if(opt.vit_pack_input){expanded_input=New(size_t(n)*256);Run("deep",opt.vit_input_tiled?"vit_pack_input_tiled":"vit_pack_input",size_t(n)*256,P(input),P(expanded_input),U(n*1024));}
  if(opt.vit_byte_stream){Tensor in8=expanded_input;vit_in8.reset();if(n%16||(n>640&&!free_geometry))throw std::runtime_error("ViT byte stream token count");
   auto hidden=New(size_t(n)*1024),contract8=New(size_t(n)*(opt.vit_half_stream?512:256));
   Run("deep",opt.vit_expand_frag?(opt.vit_expand_m2?"vit_expand_blocked_fp8_frag_bytein_m2":"vit_expand_blocked_fp8_frag_bytein"):"vit_expand_blocked_fp8_tiled_bytein",size_t(n)*4096,P(in8),PackedVitWeight(Block(block,"expand"),4096,1024,true,opt.vit_expand_frag),P(hidden),n,U(1024),U(4096));
   Run("deep",opt.vit_half_stream?(opt.vit_weight_mask&2?"vit_contract_blocked_fp8_tiled_hstream":"vit_contract_blocked_fp8_hstream"):(opt.vit_weight_mask&2?"vit_contract_blocked_fp8_tiled_bstream":"vit_contract_blocked_fp8_bstream"),size_t(n)*1024,P(hidden),PackedVitWeight(Block(block,"contract"),1024,4096,opt.vit_weight_mask&2),P(in8),P(contract8),n,U(4096),U(1024));hidden.reset();
   auto norm=New(size_t(n)*768);Run("deep",opt.vit_half_stream?(opt.vit_qkv_n4?"vit_qkv_project_normalize_fused_f16compact_fp8_h16in_n4":"vit_qkv_project_normalize_fused_f16compact_fp8_h16in"):"vit_qkv_project_normalize_fused_f16compact_fp8_bytein",size_t(n)*3072,P(contract8),PackedVitQkvWeight(Block(block,"qkv")),P(norm),n);
   auto av=New(size_t(n)*256);std::string fused=n<=256?"vit_attention_fused_256":n<=400?"vit_attention_fused_400":"vit_attention_fused_640";fused+="_bytein_bout";if(HIP_VIT_ATTN_KSPLIT_HOST&&n>256&&HasFn("deep_fast",fused+"_ks2"))fused+="_ks2"; /* fast tier HIP_VIT_ATTN_KSPLIT (lossy, module opt-in) */Run("deep",fused.c_str(),size_t(n)*512,P(norm),P(av),n);norm.reset();
   auto out=New(size_t(n)*1024);Tensor out8;if(block<38)out8=New(size_t(n)*256);
   Run("deep",opt.vit_half_stream?"vit_project_bytein_bout_hskip":"vit_project_bytein_bout",size_t(n)*1024,P(av),PackedDeepWeight(Block(block,"projection"),1048576),P(contract8),P(out),P(out8),n);
   vit_in8=out8;Stage("block"+std::to_string(block),out);return out;}std::string expand_name="vit_expand_blocked_fp8";if(opt.vit_weight_mask&1)expand_name+="_tiled";if(opt.vit_pack_input)expand_name+="_bytein";if(opt.vit_expand_m4)expand_name+="_m4";if(opt.vit_expand_frag)expand_name=opt.vit_expand_m4?"vit_expand_blocked_fp8_frag_bytein_m4":opt.vit_expand_m2?"vit_expand_blocked_fp8_frag_bytein_m2":"vit_expand_blocked_fp8_frag_bytein";if(opt.vit_input_tiled)expand_name="vit_expand_blocked_fp8_frag_tiledin";auto hidden=opt.vit_ffn_fused?Tensor{}:New(size_t(n)*(opt.fp8_deep?1024:4096)),contract=New(size_t(n)*(vit_contract_byte_edge?256:(vit_stream_active&2)?512:1024));if(opt.vit_ffn_fused){if(n%16)throw std::runtime_error("fused ViT FFN token count");Run("deep","vit_ffn_fused",size_t(n)*1024,P(expanded_input),PackedVitWeight(Block(block,"expand"),4096,1024,true,true),PackedVitWeight(Block(block,"contract"),1024,4096,false),P(input),P(contract),n);expanded_input.reset();}else{Run("deep",opt.vit_blocked?expand_name.c_str():opt.fp8_deep?"vit_expand_fp8":"vit_expand",size_t(n)*4096,P(expanded_input),PackedVitWeight(Block(block,"expand"),4096,1024,opt.vit_weight_mask&1,opt.vit_expand_frag),P(hidden),n,U(1024),U(4096));expanded_input.reset();if(opt.vit_split_k){auto parts=New(size_t(n)*4096);auto cw=PackedVitWeight(Block(block,"contract"),1024,4096,opt.vit_weight_mask&2);Run("deep",opt.vit_contract_blocked?(opt.vit_weight_mask&2?"vit_contract_parts_blocked_fp8_tiled":"vit_contract_parts_blocked_fp8"):"vit_contract_parts_fp8",size_t(n)*4096,P(hidden),cw,P(parts),n);Run("deep","vit_contract_combine",size_t(n)*1024,P(parts),cw,P(input),P(contract),n);}else{if(opt.vit_contract_frag&&opt.vit_contract_blocked)Run("deep",vit_contract_byte_edge?"vit_stream_contract_frag_bout":(vit_stream_active&2)?"vit_stream_contract_frag_hout":"vit_contract_blocked_fp8_frag",size_t(n)*1024,P(hidden),PackedVitWeight(Block(block,"contract"),1024,4096,true,true),P(input),P(contract),n,U(4096),U(1024));else Run("deep",opt.vit_contract_blocked?(opt.vit_weight_mask&2?"vit_contract_blocked_fp8_tiled":"vit_contract_blocked_fp8"):opt.fp8_deep?"vit_project_fp8":"vit_project",size_t(n)*1024,P(hidden),PackedVitWeight(Block(block,"contract"),1024,4096,opt.vit_weight_mask&2),P(input),P(contract),n,U(4096),U(1024));}hidden.reset();}auto qkv=opt.vit_qkv_fused?Tensor{}:New(size_t(n)*3072),norm=New(opt.vit_qkv_fp8?size_t(n)*768:size_t(n)*3072);auto qw=(opt.vit_qkv_fused&&opt.packed_weights)?(opt.vit_qkv_frag&&opt.vit_qkv_fp8?((vit_stream_active&2)&&HIP_VIT_QKV_F8W&&HasFn("vit_stream","vit_stream_qkv_frag_hin_w5f8")?PackedVitQkvWeightFrag8(Block(block,"qkv")):PackedVitQkvWeightFrag(Block(block,"qkv"))):PackedVitQkvWeight(Block(block,"qkv"))):Weight(Block(block,"qkv"));if(opt.vit_qkv_fused)Run("deep",opt.vit_qkv_fp8?(opt.vit_qkv_frag?(vit_contract_byte_edge?"vit_stream_qkv_frag_bin_w5f8":(vit_stream_active&2)?"vit_stream_qkv_frag_hin":"vit_qkv_project_normalize_fused_f16compact_fp8_frag"):"vit_qkv_project_normalize_fused_f16compact_fp8"):opt.packed_weights?"vit_qkv_project_normalize_fused_f16compact":"vit_qkv_project_normalize_fused",size_t(n)*3072,P(contract),qw,P(norm),n);else{Run("deep",opt.vit_qkv_blocked?"vit_qkv_project_blocked":"vit_qkv_project",size_t(n)*3072,P(contract),qw,P(qkv),n);Run("deep","vit_qkv_normalize",size_t(n)*96,P(qkv),qw,P(norm),n);}qkv.reset();auto av=New(size_t(n)*((vit_stream_active&1)?256:1024));if(opt.vit_attn_fused){if(n%16||(n>640&&!free_geometry))throw std::runtime_error("fused ViT attention token count");std::string fused=n<=256?"vit_attention_fused_256":n<=400?"vit_attention_fused_400":"vit_attention_fused_640";if(opt.vit_qkv_fp8)fused+="_bytein";if(vit_stream_active&1)fused+="_bout";if(HIP_VIT_ATTN_KSPLIT_HOST&&n>256&&fused.size()>12&&fused.compare(fused.size()-12,12,"_bytein_bout")==0&&HasFn("deep_fast",fused+"_ks2"))fused+="_ks2"; /* fast tier HIP_VIT_ATTN_KSPLIT */Run("deep",fused.c_str(),size_t(n)*512,P(norm),P(av),n);}else{auto ex=New(size_t(n)*32*n),inv=New(size_t(n)*32);Run("deep",opt.fast_vit?"vit_attention_scores_fast":"vit_attention_scores",size_t(n)*32*n,P(norm),P(ex),n);Run("deep",opt.fast_vit?"vit_attention_inverse_fast":"vit_attention_inverse",size_t(n)*32,P(ex),P(inv),n);Run("deep",opt.fast_vit?"vit_attention_av_fast":"vit_attention_av",size_t(n)*1024,P(norm),P(ex),P(inv),P(av),n);ex.reset();inv.reset();}norm.reset();auto out=New(size_t(n)*1024);if(opt.vit_proj_frag)Run("deep","vit_project_frag",size_t(n)*1024,P(av),PackedVitProjectionFrag(Block(block,"projection")),P(contract),P(out),n,U(1024),U(1024));else Run("deep","vit_project",size_t(n)*1024,P(av),PackedDeepWeight(Block(block,"projection"),1048576),P(contract),P(out),n,U(1024),U(1024));Stage("block"+std::to_string(block),out);return out;}
 void* PackedDecoderFp8(const std::string&name,size_t matrix){auto key=name+"@decoder-fp8";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()<matrix)throw std::runtime_error("decoder weight shape");Fp8(v,{{0,matrix}});it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);} /* 2026-10-01 HIP_DEC_F8W: exact E4M3 matrix for decoder_project2x_h16w*_f8 (results/c512-qkv-pipeline-20261001 §11) */
 void* PackedDecoderHalf(const std::string&name,size_t matrix){auto key=name+"@decoder-f16r";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()<matrix)throw std::runtime_error("decoder weight shape");HalfR(v,0,matrix);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 Tensor UpBody(Tensor low,Tensor skip,U w,U h,U c,U block,bool low_bytes=false,bool skip_f16=false,Tensor skip_b8={}){
  U shift=Shift(block),sx=(shift&1)?4:0,sy=(shift&2)?4:0,ww=(w+sx+7)&~7u,hh=(h+sy+7)&~7u;
  pdl_prev={};pdl_ffn_flags=nullptr;pdl_anyorder=false;
  auto out=New(size_t(w)*h*c/4);
  std::string name="c"+std::to_string(c)+"_wave2_up";
  const bool w16=!low_bytes&&!skip_f16&&HIP_SMALL_FFN_W16&&HasFn("c64_wave2",name+"_w16");if(w16)name+="_w16";if(low_bytes)name+="_lb";const bool sb=low_bytes&&skip_f16&&skip_b8&&HasFn("c64_wave2",name+"_sb");if(sb)name+="_sb";else if(skip_f16)name+="_sh";
#if HIP_SWIN_PERSISTENT_DIAGNOSTICS
  {static bool shown[2]{};if(!shown[c==128]){shown[c==128]=true;std::printf("W2_UP %s\n",name.c_str());}}
#endif
  Run("c64_wave2",name.c_str(),size_t(ww)*hh/64,P(low),w16?PackedFusedMhWeightFragW16(Block(block,"ffn"),c):PackedFusedMhWeightFrag(Block(block,"ffn"),c),WaveOwnedAttentionWeight(Block(block,"attention"),c),P(out),w,h,ww,hh,sx,sy,U(4),PackedDecoderHalf(Block(block,"weights"),size_t(2*c)*c),sb?P(skip_b8):P(skip));
  Stage("block"+std::to_string(block),out);return out;
 }
 bool gather_fold=false;
 bool GatherFoldOk(){if(!HIP_VIT_GATHER_FOLD_HOST||!opt.pool_project_group||!opt.decoder_h16w||!opt.fast_deep)return false;if(const char*v=std::getenv("DLSS5_HIP_GATHER_FOLD"))if(!strcmp(v,"0"))return false;
  U rw=W/64,rh=H/64,ow=rw,oh=(rw*rh)%16?rh+1:rh;if(W/32==60&&(H/32==36||H/32==34)){ow=32;oh=20;}if(free_geometry){ow=(rw+3)/4*4;oh=(rh+3)/4*4;}if((ow*oh)%16)return false;
  bool ok=HasFn("mh_fast","mh_pool_project_group_c512_gout")&&HasFn("deep_fast","decoder_project2x_h16w_gin");
  if(const char*d=std::getenv("DLSS5_HIP_GATHER_FOLD_LOG"))if(*d){static bool once=false;if(!once){once=true;fprintf(stderr,"GATHER_FOLD %d\n",ok?1:0);}}
  return ok;}
 Tensor Up(Tensor input,Tensor skip,U iw,U ih,U ow,U oh,U ic,U oc,const std::string&file,bool byte_out=false){if(byte_out&&(!(opt.decoder_h16w&&opt.fast_deep)||oc==32))throw std::runtime_error("decoder byte output requires quantized fast path");auto out=(HIP_C512_PAD16&&oc==512&&!byte_out)?NewPad16(size_t(ow)*oh,512):New(size_t(ow)*oh*oc/(byte_out?4:1));if(opt.decoder_h16w&&opt.fast_deep){std::string dn=byte_out?"decoder_project2x_h16w_byteout":(gather_fold&&ic==1024&&oc==512)?"decoder_project2x_h16w_gin":"decoder_project2x_h16w";
  /* 2026-10-01 tail-c32-gap: NT column tiles per wave (HIP_DEC_NT exports), same bits; widest present */
  if(!(gather_fold&&ic==1024&&oc==512)&&((ic==1024&&oc==512&&!byte_out)||(ic==512&&oc==256&&byte_out))){if(HasFn("deep_fast",dn+"_w"))dn+="_w";else for(U nt:{8u,4u,2u})if(HasFn("deep_fast",dn+"_n"+std::to_string(nt))){dn+="_n"+std::to_string(nt);break;}} /* HIP_DEC_WIDE exports (_w): same tiling, wide epilogue */
  const bool f8=HIP_DEC_F8W&&(((dn=="decoder_project2x_h16w"||dn=="decoder_project2x_h16w_w")&&ic==1024&&oc==512)||((dn=="decoder_project2x_h16w_byteout"||dn=="decoder_project2x_h16w_byteout_w")&&ic==512&&oc==256))&&HasFn("deep_fast",dn+"_f8");if(f8)dn+="_f8"; /* bitexact-pm: _w_f8 = HIP_DEC_WIDE epilogue + HIP_DEC_F8W main loop (both module macros) */
  Run("deep",dn.c_str(),size_t(iw)*ih*oc,P(input),f8?PackedDecoderFp8(file,size_t(ic)*oc):PackedDecoderHalf(file,size_t(ic)*oc),P(skip),P(out),iw,ih,ow,oh,ic,oc);}else Run("deep","decoder_project2x",size_t(iw)*ih*oc,P(input),Weight(file),P(skip),P(out),iw,ih,ow,oh,ic,oc);return out;}
 static U Shift(U block){static constexpr U s[]={0,3,1,2,0,3,1,2,0,3,1,2,0,3,1,2,1,2,0,3,1,2,0,3,1,2,0,3,1,2};if(block<40||block>69)throw std::runtime_error("decoder shift");return s[block-40];}
public:
 Network(const Network&)=delete;Network&operator=(const Network&)=delete;
 explicit Network(Options o):api(o.runtime),pulse_ops{int(o.device),&stream,api.hipSetDevice,api.hipEventCreate,api.hipEventRecord,api.hipStreamSynchronize,api.hipEventDestroy},pulse_lease(pulse_ops),opt(std::move(o)),W(opt.width),H(opt.height){api.style_feature=StyleFeatureFromEnvironment();{const char*v=std::getenv("DLSS5_OVERLAP");final_direct_compatible=HIP_FINAL_OUTPUT_DIRECT&&!opt.graph&&(!v||!*v||!std::strcmp(v,"0"));}multi_pass=MultiPassFromEnvironment();multi_predict=MultiPredictFromEnvironment();multi_skin=MultiSkinFromEnvironment();multi_skip=opt.integration.override_multi_pass_skip?opt.integration.multi_pass_skip:MultiPassSkipFromEnvironment();std::fprintf(stderr,"multi_pass=%u multi_pass_predict=%u actual_network_passes=%u\n",multi_pass,unsigned(multi_predict),multi_predict&&multi_pass==3?2:multi_pass);fast_numeric=FastNumericFromEnvironment();if(opt.experimental_temporal){
  const std::string tap=fast_numeric?"c32-wave1-temporal-fast":"c32-wave1-temporal";
  bool assets=std::ifstream(opt.modules+"/"+tap+".hsaco").good()&&std::ifstream(opt.modules+"/temporal-history.hsaco").good()&&std::ifstream(opt.assets+"/post70-history-head.f16").good()&&std::ifstream(opt.assets+"/native-temporal-sigmoid.f32").good();
  if(!NativeExperimentalTemporalCompatible()||!assets||opt.graph||multi_pass!=1||multi_skin||!opt.temporal_valid_height||opt.temporal_valid_height>H){opt.experimental_temporal=false;std::fprintf(stderr,"history_experiment requested=1 active=0 reason=incompatible_or_missing_assets\n");}
  else opt.temporal_feature_tap=true;
 }wave_owned_active=WaveOwnedCompatible(opt);c512_m32_active=C512M32Compatible(opt);vit_proj_n64_active=VitProjN64Compatible(opt);vit_stream_active=VitStreamCompatible(opt)?opt.vit_stream:0;if(opt.sparse_weights){api.EnableVmm();hip_probe::MemAllocationProp prop{};prop.type=1;prop.location.type=1;api.Check(api.hipMemGetAllocationGranularity(&sparse_granularity,&prop,1),"VMM granularity");if(!sparse_granularity||sparse_granularity&(sparse_granularity-1))throw std::runtime_error("VMM granularity");}if(opt.tiled_mh_ffn&&!opt.fused_mh_ffn)throw std::runtime_error("tiled FFN weights require fused FFN");if(opt.fused_mh_ffn&&(!opt.fp8_middle||!opt.packed_weights||!opt.mh_wave))throw std::runtime_error("fused MH FFN requires packed weights and byte middle");if(opt.fused_qkv_norm&&!opt.fp8_normalized)throw std::runtime_error("fused QKV requires byte normalized pipeline");if((opt.vit_weight_mask||opt.vit_pack_input)&&(!opt.vit_blocked||!opt.vit_contract_blocked||opt.vit_weight_mask>3))throw std::runtime_error("ViT layout requires blocked pipeline and mask0..3");if((opt.vit_blocked||opt.vit_contract_blocked)&&(!opt.fast_deep||!opt.fp8_deep||!opt.packed_weights))throw std::runtime_error("blocked ViT requires packed byte deep pipeline");if(opt.vit_split_k&&(!opt.fast_deep||!opt.fp8_deep))throw std::runtime_error("ViT SplitK requires byte deep path");if(opt.vit_attn_fused&&(!opt.fast_deep||!opt.fast_vit))throw std::runtime_error("fused ViT attention requires fast deep/ViT pipeline");if(opt.vit_qkv_fp8&&(!opt.vit_qkv_fused||!opt.packed_weights||!opt.vit_attn_fused))throw std::runtime_error("ViT byte QKV requires fused packed QKV and fused attention");if(opt.vit_expand_frag&&(!opt.vit_blocked||!opt.vit_pack_input||!(opt.vit_weight_mask&1)))throw std::runtime_error("ViT expand fragment layout requires blocked tiled byte-input expand");if(opt.vit_expand_m4&&(!opt.vit_blocked||!opt.vit_pack_input||!(opt.vit_weight_mask&1)))throw std::runtime_error("ViT expand M4 requires blocked tiled byte-input expand");if(opt.vit_byte_stream&&(!opt.vit_qkv_fp8||!opt.vit_attn_fused||!opt.vit_contract_blocked||!opt.vit_pack_input||!(opt.vit_weight_mask&1)||opt.vit_split_k||opt.vit_expand_m4))throw std::runtime_error("ViT byte stream requires fused byte QKV/attention, blocked tiled byte-input expand, no split-K/M4");if((opt.vit_half_stream&&!opt.vit_byte_stream)||(opt.vit_qkv_n4&&!opt.vit_half_stream))throw std::runtime_error("ViT half stream requires the byte stream; QKV N4 requires the half stream");if(opt.mh_feature_byte&&(!opt.ffn_qkv||opt.ffn_qkv_max_c!=256||!opt.fused_ffn_project||!opt.mh_project_crop||!opt.fp8_av||!opt.fused_mh||!opt.packed_weights))throw std::runtime_error("MH byte feature requires the fused FFN/QKV crop pipeline");if(opt.c512_proj_tiles&&(!opt.c512_qkv_frag||!opt.split_ffn_fused||opt.split_mix_fused))throw std::runtime_error("C512 tiled projection requires fragment QKV and fused split FFN");if(opt.c512_proj_frag&&(!opt.mh_project_crop||!opt.fp8_av||!opt.packed_weights))throw std::runtime_error("C512 fragment projection requires crop byte-AV packed pipeline");if(opt.c512_qkv_frag&&(!opt.split_project_blocked||!opt.packed_weights||!opt.fused_qkv_norm||!opt.fp8_normalized||opt.qkv_norm_wave_c512))throw std::runtime_error("C512 fragment QKV requires blocked packed projection and byte normalized QKV");if(opt.split_mix_h16w&&(!opt.split_mix_blocked||!opt.packed_weights||opt.split_mix_fused))throw std::runtime_error("split mix half weights require blocked mix with packed weights");if(opt.split_mix_fused&&(!opt.split_ffn_fused||!opt.packed_weights||!opt.split_mix_blocked))throw std::runtime_error("fused split mix requires packed fused split FFN");if(opt.vit_ffn_fused&&(!opt.vit_expand_frag||!opt.vit_pack_input||(opt.vit_weight_mask&2)||opt.vit_split_k||opt.vit_byte_stream||opt.vit_input_tiled||opt.vit_expand_m2||opt.vit_expand_m4))throw std::runtime_error("fused ViT FFN requires fragment expand weights, packed input, row-major contract weights");if(opt.vit_input_tiled&&(!opt.vit_expand_frag||opt.vit_expand_m4||opt.vit_expand_m2||opt.vit_byte_stream||!opt.vit_pack_input))throw std::runtime_error("ViT tiled input requires the fragment expand, packed input, no M2/M4/byte stream");if(opt.vit_expand_m2&&(!opt.vit_expand_frag||opt.vit_expand_m4))throw std::runtime_error("ViT expand M2 requires the fragment expand and no M4");if(opt.qkv_norm_wave_c512&&(!opt.fused_qkv_norm||!opt.packed_weights))throw std::runtime_error("C512 wave QKV requires fused packed QKV normalize");if(opt.pool_project_fused&&!opt.fast_mh)throw std::runtime_error("fused pool project requires the fast MH module");if(opt.ffn_qkv_batched_norm&&(!opt.ffn_qkv||opt.mh_byte_stream))throw std::runtime_error("batched QKV normalization requires the fused FFN/QKV kernel without the byte stream");if(opt.mh_byte_stream){if(!opt.ffn_qkv||opt.ffn_qkv_max_c!=256||!opt.fused_ffn_project||!opt.mh_input_mapped||!opt.mh_project_crop||!opt.fp8_normalized||!opt.fp8_av||!opt.fused_mh||!opt.packed_weights||!opt.elide_identity_shift)throw std::runtime_error("MH byte stream requires the fused FFN/QKV, mapped input, crop project pipeline");for(U s:opt.skip_blocks)if((s>=5&&s<=22)||(s>=48&&s<=65))throw std::runtime_error("MH byte stream cannot skip a C64/128/256 block");}{bool bad=false;for(U s:multi_skip)if((opt.mh_byte_stream&&((s>=5&&s<=22)||(s>=48&&s<=65)))||(!opt.raw_chain&&(s<=4||s>=66)))bad=true;if(bad){std::fprintf(stderr,"DLSS5_MULTI_PASS_SKIP_BLOCKS: a listed block cannot be skipped by this pipeline, using none\n");multi_skip.clear();}}if(opt.mapped_c32&&!opt.crop_c32)throw std::runtime_error("mapped C32 requires half/crop pipeline");if(opt.crop_c32&&!opt.half_c32)throw std::runtime_error("fused C32 crop requires half raw");if(opt.half_c32&&(!opt.fast_c32||!opt.fused_ffn))throw std::runtime_error("half C32 requires fused fast pipeline");if(opt.fp8_middle&&!opt.fp8_ffn)throw std::runtime_error("byte middle requires byte FFN pipeline");if(opt.fp8_deep&&!opt.fast_deep)throw std::runtime_error("byte deep requires fast deep pipeline");if((opt.fp8_ffn||opt.fp8_av)&&(!opt.fast_mh||!opt.mh_wave||(opt.fp8_av&&!opt.fp8_normalized)))throw std::runtime_error("byte intermediates require compatible MH pipeline");if(opt.fp8_normalized&&(!opt.fast_mh||!opt.mh_wave||!opt.fused_mh))throw std::runtime_error("FP8 normalized requires fast MH wave/fused pipeline");if(opt.post_shift>3)throw std::runtime_error("post shift must be 0..3");free_geometry=FreeGeometry(W,H);if(!(TierGeometry(W,H)||free_geometry))throw std::runtime_error("unsupported processing geometry");api.Check(api.hipInit(0),"hipInit");api.Check(api.hipSetDevice(int(opt.device)),"device");api.Check(api.hipStreamCreate(&stream),"stream");try{const char*names[][2]={{"c32","c32_prefix_reference.hsaco"},{"mh","multihead-reference.hsaco"},{"deep","deep_reference.hsaco"},{"boundary","boundary_reference.hsaco"}};for(auto&v:names){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+v[1]).c_str()),v[1]);modules[v[0]]=m;}if(opt.wmma){const char*wm[][2]={{"c32_wmma","c32_wmma.hsaco"},{"mh_wmma","multihead-wmma.hsaco"},{"deep_wmma","deep_wmma.hsaco"}};for(auto&v:wm){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+v[1]).c_str()),v[1]);modules[v[0]]=m;}}if(opt.fused_ffn){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+(opt.packed_c32?"/c32_fused_ffn_attention-packed.hsaco":"/c32_fused_ffn_attention.hsaco")).c_str()),"fused FFN attention module");modules["c32_fused_ffn"]=m;}
if(c512_m32_active){
 const char*extra[][2]={{"c512_m32_mh","c512-m32-mh.hsaco"},{"c512_m32_deep","c512-m32-deep.hsaco"}};
 for(auto&entry:extra){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+entry[1]).c_str()),entry[1]);modules[entry[0]]=m;}}
if(vit_stream_active){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+FastTwin("vit-stream")+".hsaco").c_str()),"vit-stream.hsaco");modules["vit_stream"]=m;}
if(vit_proj_n64_active){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/vit-wide-deep.hsaco").c_str()),"vit-wide-deep.hsaco");modules["vit_wide_deep"]=m;}
if(vit_stream_active==3&&HIP_VIT_QKV_F8W&&opt.vit_qkv_fused&&opt.vit_qkv_fp8&&opt.vit_qkv_frag&&opt.vit_contract_frag&&opt.vit_contract_blocked&&!opt.vit_split_k&&!opt.vit_ffn_fused){const char*v=std::getenv("DLSS5_LAB_VIT_BYTE_EDGE");vit_contract_byte_edge=(!v||std::strcmp(v,"0"))&&HasFn("vit_stream","vit_stream_contract_frag_bout")&&HasFn("vit_stream","vit_stream_qkv_frag_bin_w5f8")&&HasFn("vit_stream","vit_stream_project_n64_bb");}
std::fprintf(stderr,"vit_contract_byte_edge=%u\n",unsigned(vit_contract_byte_edge));


if(wave_owned_active){
 const bool rtz_tall=HIP_C32_RTZ_TALL&&W==1920&&(H==1152||H==1088)&&std::ifstream(std::filesystem::u8path(opt.modules+"/c32-wave1-rtz.hsaco"),std::ios::binary).good();
 std::string extra[][2]={{"c64_wave2","c64-wave2"},{"c32_wave1",rtz_tall?"c32-wave1-rtz":"c32-wave1"}};
 for(auto&entry:extra)entry[1]=entry[0]==std::string("c32_wave1")&&opt.experimental_temporal?(fast_numeric?"c32-wave1-temporal-fast":"c32-wave1-temporal"):FastTwin(entry[1],entry[0]==std::string("c32_wave1")); /* c32: the rtz build of the fast C32 disassembles identically, so the twin stem is always c32-wave1 */
 for(auto&entry:extra){entry[1]+=".hsaco";Handle m{};
  api.Check(api.LoadModule(&m,(opt.modules+"/"+entry[1]).c_str()),entry[1].c_str());modules[entry[0]]=m;
  if(entry[0]=="c32_wave1"){
   const bool geometry=fast_numeric&&W==1600&&H==960&&!opt.graph&&!opt.experimental_temporal;
   const char*norm900="c32-wave1-fast-norm900.hsaco";Handle candidate{};
   if(geometry&&std::ifstream(std::filesystem::u8path(opt.modules+"/"+norm900),std::ios::binary).good()){
    int ec=api.LoadModule(&candidate,(opt.modules+"/"+norm900).c_str());bool complete=!ec;const char*missing=nullptr;
    // Locked fast-numeric baseline/hoist ABI contains26 exports, including postfeatures.
    // Whole fallback on any missing legacy export; never partially select an old callee.
    const char*required[]={"c32_fast_ffn_attention_fused","c32_fast_ffn_attention_fused_half","c32_fast_ffn_attention_fused_half_mapped","c32_fast_ffn_attention_fused_half_chain","c32_fast_ffn_attention_fused_half_finish_main8","c32_fast_ffn_attention_fused_half_chain_finish","c32_fast_ffn_attention_fused_half_prefix_finish_main8","c32_fast_ffn_attention_fused_half_chain_finish_dcrop","c32_post_merge_head_half","c32_post_merge_fused_half","c32_wave1_chain","c32_wave1_mapped","c32_wave1_finish","c32_wave1_finish_dcrop","c32_wave1_post","c32_wave1_prefix","c32_wave1_finish_dcrop_b8","c32_wave1_finish_dcrop_b8d","c32_wave1_prefix_b8d","c32_wave1_mapped_b8","c32_wave1_finish_b8","c32_wave1_post_b8","c32_wave1_post_b8_rgba","c32_wave1_post_b8_features","c32_wave1_up_b8","c32_wave1_up"};
    if(complete)for(const char*name:required){Handle f{};if(api.hipModuleGetFunction(&f,candidate,name)){complete=false;missing=name;break;}}
    if(complete){modules["c32_norm900"]=candidate;c32_norm900_loaded=true;}
    else{if(candidate)api.hipModuleUnload(candidate);std::fprintf(stderr,"c32_norm900 active=0 reason=optional-load-or-full26-export-fallback load_status=%d missing_export=%s\n",ec,missing?missing:"none");}
   }
   std::fprintf(stderr,"c32_norm900 scope=%u loaded=%u selected=%s processing=%ux%u fast_numeric=%u mp=%u graph=%u experimental=%u\n",unsigned(geometry&&multi_pass==1),unsigned(c32_norm900_loaded),C32Norm900Active()?norm900:entry[1].c_str(),W,H,unsigned(fast_numeric),multi_pass,unsigned(opt.graph),unsigned(opt.experimental_temporal));
  }
 }}
if(SwinRunCompatible(opt)){
 const bool new1440=Sp1440Geometry(opt);const std::string file=new1440&&fast_numeric?"swin-persistent-fast.hsaco":"swin-persistent.hsaco";
 if(new1440&&(!fast_numeric||opt.graph||observer||!opt.dump_dir.empty())){opt.swin_run=false;}
 else if(!std::ifstream(std::filesystem::u8path(opt.modules+"/"+file),std::ios::binary).good()){opt.swin_run=false;std::fprintf(stderr,"swin_run:nomodule %s\n",file.c_str());}
 else if(new1440){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+file).c_str()),"1440 SP math-paired module");modules["sp"]=m;
  sp1440_ready=HasFn("sp","sp_init")&&HasFn("sp","sp_run256_w16")&&HasFn("sp","sp_recover256_w16");
  if(!sp1440_ready){opt.swin_run=false;std::fprintf(stderr,"swin1440:missing paired exports, old Body fallback\n");}
  else{SpGetPlan(W/16,H/16,256,16,6);SpGetPlan(W/16,H/16,256,49,6);std::fprintf(stderr,"swin1440_ready=1 module=%s fast_numeric=%u\n",file.c_str(),unsigned(fast_numeric));}
 }
}
if(opt.fused_mh){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/multihead_fused_attention.hsaco").c_str()),"fused MH attention module");modules["mh_fused"]=m;}
if(opt.mh_window_fused){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/c64-window-fused.hsaco").c_str()),"C64 window fused module");modules["mh_window"]=m;}
if(opt.fast_deep){const std::string deep_stem=opt.packed_weights?"deep_fast-packed":"deep_fast";Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+FastTwin(deep_stem)+".hsaco").c_str()),"deep fast module");modules["deep_fast"]=m;}
if(opt.fast_mh){const std::string mh_stem=opt.packed_weights?(opt.mh_wave?"multihead-fast-padded-wave-packed":"multihead-fast-packed"):(opt.mh_wave?"multihead-fast-padded-wave":"multihead-fast");Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+FastTwin(mh_stem)+".hsaco").c_str()),"MH fast module");modules["mh_fast"]=m;}
 /* free geometry: the per-slot tile counters (PDL_SLOTS, one per 16 C64 tokens) cover processing surfaces up to 4.19M pixels; larger free sizes run without PDL */
 PreflightPdl();
if(opt.fast_prefix){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/prefix_fast.hsaco").c_str()),"prefix fast module");modules["prefix_fast"]=m;}
if(opt.fused_c32){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/c32_fused_attention.hsaco").c_str()),"fused attention module");modules["c32_fused"]=m;}
if(opt.fast_c32){const char*f[][2]={{"c32_fast_ffn","c32_fast.hsaco"},{"c32_fast_attention","c32_fast_attention.hsaco"},{"boundary_fast","boundary-fast.hsaco"}};for(auto&v:f){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+v[1]).c_str()),v[1]);modules[v[0]]=m;}}if(opt.tiled){const char*t[][2]={{"c32_tiled","c32_tiled.hsaco"},{"mh_tiled","multihead-tiled.hsaco"}};for(auto&v:t){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+v[1]).c_str()),v[1]);modules[v[0]]=m;}}if(opt.wave){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/wave-pointwise.hsaco").c_str()),"wave module");modules["wave"]=m;}if(HIP_POOL64_BYTE_EDGE&&wave_owned_active&&opt.mh_byte_stream&&opt.mh_project_crop&&opt.mh_input_mapped&&opt.elide_identity_shift&&opt.grouped_mh_contract&&opt.packed_weights&&opt.mh_proj_diag_fb&&opt.pool32_h16w&&opt.fast_mh&&!opt.skip_blocks.count(5)&&!observer&&opt.dump_dir.empty()){
 const char*consumer=HIP_SMALL_FFN_W16&&HasFn("c64_wave2","c64_wave2_bi_bo_w16")?"c64_wave2_bi_bo_w16":"c64_wave2_bi_bo";
 pool64_byte_available=HasFn("mh_fast","mh_pool_project_c32_b8_out8")&&HasFn("c64_wave2",consumer);
 if(pool64_byte_available)std::fprintf(stderr,"pool64_byte_edge=1\n");}
if(opt.temporal_feature_tap){
 if(multi_pass!=1||multi_skin||opt.graph||!wave_owned_active||!opt.post_merge_fold||!opt.post_head_fused||!HIP_C32_POST_LOW_BYTE)
  throw std::runtime_error("temporal feature tap requires MP1, skin0, graph0 and byte fused post");
 if(!HasFn("c32_wave1","c32_wave1_post_b8_features"))throw std::runtime_error("temporal feature tap module export missing");
 temporal_features=New(size_t(W)*H*16); // 32 IEEE-half channels; allocate before external producer waits.
 temporal_feature_tap_active=true;
}
if(opt.experimental_temporal){
 Handle module{};api.Check(api.LoadModule(&module,(opt.modules+"/temporal-history.hsaco").c_str()),"experimental temporal module");modules["temporal_history"]=module;
 for(const char*fn:{"temporal_warp_uv","temporal_store","hip_gate_head","hip_gate_resolve"})Fn("temporal_history",fn);
 auto upload_file=[&](const char*name,size_t count){std::ifstream f(opt.assets+"/"+name,std::ios::binary|std::ios::ate);if(!f||f.tellg()!=std::streamoff(count))throw std::runtime_error("temporal asset size");std::vector<char>b(count);f.seekg(0);if(!f.read(b.data(),count))throw std::runtime_error("temporal asset read");return Upload(b.data(),count,true);};
 experimental_history=New(size_t(W)*opt.temporal_valid_height*4);experimental_prefix=New(size_t(W)*H*4);experimental_raw=New(size_t(W)*H*4);experimental_logit=New((size_t(W)*opt.temporal_valid_height+1)/2);
 experimental_weights=upload_file("post70-history-head.f16",64);experimental_sig=upload_file("native-temporal-sigmoid.f32",262144);experimental_recip=upload_file("normalized-output.f32",33554432);
 std::fprintf(stderr,"history_experiment requested=1 active=1 mode=MP1 metadata=UV_unjittered_assumed valid=%ux%u\n",W,opt.temporal_valid_height);
}

if(wave_owned_active)HasFn("c32_wave1","c32_wave1_post_b8_rgba");if(multi_predict||std::ifstream(opt.modules+"/multi-pass-predict.hsaco").good())EnsurePredictModule();if(multi_pass>1)PrepareMultiPassFeeds();if(multi_skin)PrepareSkin();}catch(...){if(!pulse_lease.Close()){std::fprintf(stderr,"submit_pulse ctor cleanup retained owner stream\n");throw;}if(pdl_flags){api.hipStreamSynchronize(stream);api.hipFree(pdl_flags);pdl_flags=nullptr;}for(auto&m:modules)api.hipModuleUnload(m.second);api.hipStreamDestroy(stream);throw;}}
 ~Network(){if(!pulse_lease.Close()){std::fprintf(stderr,"submit_pulse direct destructor retained owner stream; bridge must preclose\n");return;}std::fprintf(stderr,"submit_pulse resources create=%llu create_ok=%llu record=%llu record_ok=%llu destroy=%llu destroy_ok=%llu drain=%llu site_visits=%llu accepted=%llu reject_mask=%u lifetime_pdl_calls=%u\n",pulse_ops.create_calls,pulse_ops.create_ok,pulse_ops.record_calls,pulse_ops.record_ok,pulse_ops.destroy_calls,pulse_ops.destroy_ok,pulse_ops.drain_calls,pulse_site_visits,pulse_record_accepted,pulse_last_reject_mask,pdl_calls);api.hipSetDevice(int(opt.device));api.hipStreamSynchronize(stream);native_post_row.reset();experimental_history.reset();experimental_prefix.reset();experimental_raw.reset();experimental_logit.reset();experimental_weights.reset();experimental_sig.reset();experimental_recip.reset();temporal_features.reset();SpReport();pdl_keep.clear();if(pdl_flags){api.hipFree(pdl_flags);pdl_flags=nullptr;}if(opt.graph)std::printf("graph_stats builds=%u replays=%u\n",graph_builds,graph_replays);ClearGraph();for(auto&t:timings){api.hipEventDestroy(t.begin);api.hipEventDestroy(t.end);}adaptive_image_anchor.reset();adaptive_image_signature.reset();adaptive_image_delta.reset();adaptive_anchor_in.reset();adaptive_anchor_out.reset();adaptive_gain.reset();adaptive_stats.reset();adaptive_state.reset();device_noise.reset();gather_maps[0].clear();gather_maps[1].clear();weights.clear();pool.clear();for(auto&m:modules)api.hipModuleUnload(m.second);api.hipStreamDestroy(stream);}
 void PrintMemory(){size_t bytes=0,free=0,total=0;std::set<void*>seen;auto add=[&](const Tensor&t){if(t&&t->owned&&seen.insert(t->ptr).second)bytes+=t->capacity;};for(auto&t:pool)add(t);for(auto&w:weights)add(w.second);for(auto&maps:gather_maps)for(auto&m:maps)add(m.second);add(device_noise);api.Check(api.hipMemGetInfo(&free,&total),"memory stats");std::printf("memory owned_MiB=%.1f allocations=%zu device_free_MiB=%.1f total_MiB=%.1f\n",bytes/1048576.,seen.size(),free/1048576.,total/1048576.);}
 /* DLSS5_HIP_MEMORY=1 (diagnostic): device memory by category after the pool has warmed — weights by key, pool tensors by capacity, gather maps, noise, and the runtime's free/total. */
 void MemoryReport(FILE*f){std::set<void*>seen;size_t wsum=0,psum=0,gsum=0,nsum=0;std::vector<std::pair<size_t,std::string>>w,p;
  for(auto&kv:weights)if(kv.second&&kv.second->owned&&seen.insert(kv.second->ptr).second){wsum+=kv.second->capacity;w.emplace_back(kv.second->capacity,kv.first);}
  for(auto&t:pool)if(t&&t->owned&&seen.insert(t->ptr).second){psum+=t->capacity;p.emplace_back(t->capacity,t.use_count()==1?"free":"held");}
  for(auto&maps:gather_maps)for(auto&m:maps)if(m.second&&m.second->owned&&seen.insert(m.second->ptr).second)gsum+=m.second->capacity;
  if(device_noise&&device_noise->owned&&seen.insert(device_noise->ptr).second)nsum=device_noise->capacity;
  size_t free=0,total=0;api.Check(api.hipMemGetInfo(&free,&total),"memory stats");
  std::sort(w.rbegin(),w.rend());std::sort(p.rbegin(),p.rend());
  std::fprintf(f,"hip_memory weights_MiB=%.1f (%zu) pool_MiB=%.1f (%zu tensors) gather_maps_MiB=%.1f noise_MiB=%.1f owned_MiB=%.1f device_free_MiB=%.1f total_MiB=%.1f\n",wsum/1048576.,w.size(),psum/1048576.,p.size(),gsum/1048576.,nsum/1048576.,(wsum+psum+gsum+nsum)/1048576.,free/1048576.,total/1048576.);
  if(opt.sparse_weights)std::fprintf(f,"hip_memory sparse_weights granularity=%zu logical_MiB=%.1f backed_MiB=%.1f dense_fallbacks=%u\n",sparse_granularity,sparse_logical/1048576.,sparse_backed/1048576.,sparse_dense);
  for(auto&e:w)std::fprintf(f,"hip_memory weight %10.2f MiB %s\n",e.first/1048576.,e.second.c_str());
  for(auto&e:p)std::fprintf(f,"hip_memory pool   %10.2f MiB %s\n",e.first/1048576.,e.second.c_str());}
 void SetStageObserver(std::function<void(const std::string&,void*,size_t)>f){observer=std::move(f);}
 void SetProgress(std::function<void(const std::string&)>f){progress=std::move(f);}
 /* 2026-10-01 swin-body-gap: decoder stage g (0=C256,1=C128,2=C64) takes the fused wave-owned Up */
 bool DecoderUpBody(U g){static constexpr U ups[]={48,56,62};U ow=W/(16u>>g),oh=H/(16u>>g);const bool byte_up=opt.decoder_byte&&opt.mh_byte_stream&&opt.decoder_h16w&&opt.fast_deep;return g>0&&wave_owned_active&&byte_up&&opt.packed_weights&&opt.grouped_mh_contract&&opt.mh_proj_diag_fb&&opt.mh_project_crop&&opt.mh_input_mapped&&!opt.skip_blocks.count(ups[g])&&ow%2==0&&oh%2==0;}
 /* W2_DOWN_HALF: encoder stage g (0=C64,1=C128) tail (post 3, Hrtz = f16-exact) stored as f16 when its pool and its decoder Up both read f16 */
 bool skip_half[2]{};
 /* W2_SKIP_BYTE (c128-c64-inchain): the half tail also writes F(Hrtz(v)) as bytes for the decoder Up skip (module exports *_bi_hob and *_up_lb_sb) */
 Tensor skip_b8[2];
 bool EncoderSkipByte(U g){const std::string cs=std::to_string(g?128:64);return HasFn("c64_wave2","c"+cs+"_wave2_bi_hob")&&HasFn("c64_wave2","c"+cs+"_wave2_up_lb_sb");}
 bool EncoderHalfTail(U g){if(g>1||!opt.mh_byte_stream||!opt.pool_project_group)return false;static constexpr U tails[]={8,14};const U c=g?128:64;const std::string cs=std::to_string(c);
  return !opt.skip_blocks.count(tails[g])&&DecoderUpBody(2-g)&&HasFn("c64_wave2","c"+cs+"_wave2_bi_ho")&&HasFn("mh_fast","mh_pool_project_group_c"+cs+"_hin")&&HasFn("c64_wave2","c"+cs+"_wave2_up_sh")&&HasFn("c64_wave2","c"+cs+"_wave2_up_lb_sh");}
 Tensor RunGraph(Tensor color,Tensor noisegpu,Tensor hist,bool request_rgba=false,void*final_rgb=nullptr,bool final_pass=true){if(request_rgba&&final_rgb)throw std::runtime_error("external RGB output cannot be RGBA");graph_output_stride=3;adaptive_image=P(color);if(opt.vit_qkv_fused&&!opt.fast_deep)throw std::runtime_error("ViT QKV fusion requires fast deep kernels");if(opt.ffn_qkv&&(!opt.grouped_mh_contract||!opt.fast_mh||!opt.fused_qkv_norm||!opt.fp8_normalized))throw std::runtime_error("FFN/QKV requires grouped byte pipeline");if(opt.grouped_mh_contract&&!opt.fused_ffn_project)throw std::runtime_error("grouped contraction requires fused project");if(opt.direct_prefix_input&&(!opt.fast_prefix||!opt.prefix_fused))throw std::runtime_error("direct input requires fused fast prefix");if(opt.mh_input_mapped&&!opt.fused_ffn_project)throw std::runtime_error("mapped MH input requires fused FFN project");if(opt.mh_project_crop&&(!opt.fast_mh||!opt.fp8_av))throw std::runtime_error("MH crop requires fast byte AV pipeline");if(opt.split_ffn_fused&&(!opt.fast_deep||!opt.fp8_deep))throw std::runtime_error("split fused FFN requires fast byte deep pipeline");if(opt.fused_ffn_project&&(!opt.fused_mh_ffn||!opt.packed_weights||!opt.fp8_middle||!opt.tiled_mh_ffn||opt.tiled_ffn_min_c!=256))throw std::runtime_error("fused FFN project requires selected packed FFN pipeline");if(opt.post_merge_fold&&(!opt.pre_main8||!opt.fused_ffn||!opt.half_c32||!opt.mapped_c32))throw std::runtime_error("post merge fold requires pre-main8 and fused half mapped pipeline");if(opt.pre_main8&&(!opt.fast_c32||!opt.half_c32||observer||!opt.dump_dir.empty()))throw std::runtime_error("pre main8 needs fast half path and no stage dumps");if(opt.raw_chain&&(!opt.fused_ffn||!opt.half_c32||!opt.mapped_c32||!opt.crop_c32||observer||!opt.dump_dir.empty()))throw std::runtime_error("raw chain requires fused half mapped crop pipeline and no stage dumps");auto tiles=opt.direct_prefix_input?color:New(size_t(W)*H*4),base=opt.direct_prefix_input?color:New(size_t(W)*H*4),prefix=opt.prefix_inline?Tensor{}:New(size_t(W)*H*32);if(!opt.direct_prefix_input)Run("boundary","hip_input_reflect",size_t(W)*H,P(color),P(base),P(tiles),W,H,W,H);color=base;if(opt.prefix_inline){if(!opt.direct_prefix_input||!opt.c32_finish_fused||!opt.pre_main8||!opt.fused_ffn||!opt.half_c32)throw std::runtime_error("inline prefix requires the direct raster input and the fused block 0 finish");inline_prefix=true;inline_rgba=tiles;inline_hist=hist;inline_seed=opt.seed;inline_temporal=U(bool(hist));}else if(opt.fast_prefix&&opt.prefix_fused){Run("prefix_fast",opt.direct_prefix_input?"dlss5_prefix_fast_fused_raster":"dlss5_prefix_fast_fused",size_t(W)*H*32,P(tiles),P(hist),Weight("block0-ffn.f32"),P(prefix),W,H,opt.seed,U(bool(hist)));}else if(opt.fast_prefix){auto features=New(size_t(W)*H*32);Run("prefix_fast","dlss5_prefix_fast_features",size_t(W)*H,P(tiles),P(hist),P(features),W,H,opt.seed,U(bool(hist)));Run("prefix_fast","dlss5_prefix_fast_project",size_t(W)*H*32,P(features),Weight("block0-ffn.f32"),P(prefix),W*H);}else{Run("c32","dlss5_prefix_reference",size_t(W)*H,P(tiles),P(hist),P(noisegpu),Weight("block0-ffn.f32"),P(prefix),static_cast<void*>(nullptr),W,H,opt.seed,U(bool(hist)));}tiles.reset();noisegpu.reset();hist.reset();auto pre=C32Body(prefix,W,H,"block0-ffn.f32","block0-attention.f32");prefix.reset();pre.raw.reset();Stage("pre-down",pre.down);Tensor skip0=pre.main,source=pre.down;pre.main.reset();pre.down.reset();Stage("block0",skip0);Tensor skips[5];U shifts[]={0,3,1,2,0,3,1,2};C32Result last;
 for(U b=1;b<=4;b++){if(opt.skip_blocks.count(b)){if(!opt.raw_chain)throw std::runtime_error("C32 skip needs the raw chain");if(b==4)SkipChainFinish(last,W/2,H/2,true);if(last.main)source=last.main;continue;}last=opt.raw_chain?C32Chain(source,last.raw?&last:nullptr,W/2,H/2,shifts[b-1],Block(b,"ffn"),Block(b,"attention"),b==4,b==4):C32(source,W/2,H/2,shifts[b-1],Block(b,"ffn"),Block(b,"attention"));source=last.main;if(source)Stage("block"+std::to_string(b),source);}Stage("block4-down",last.down);skips[0]=source;Tensor poolcrop;if(last.down_cropped)poolcrop=last.down;else{poolcrop=New(size_t(W/4)*(H/4)*32);Run("mh","mh_shift_crop",size_t(W/4)*(H/4)*32,P(last.down),P(poolcrop),W/4,H/4,last.workw/2,last.sx/2,last.sy/2,U(32));}const bool pool64_byte=pool64_byte_available&&last.down_byte&&last.down_cropped;source=New(size_t(W/4)*(H/4)*64/(pool64_byte?4:1));if(last.down_byte&&!last.down_cropped)throw std::runtime_error("byte C32 down needs the crop path");if(opt.pool32_h16w&&opt.fast_mh)Run("mh_fast",pool64_byte?"mh_pool_project_c32_b8_out8":last.down_byte?"mh_pool_project_c32_b8":"mh_pool_project_production_h16w",size_t(W/4)*(H/4)*64,P(poolcrop),PackedDsWeightCast("block4-ds.f32",32),P(source),W/4,H/4,U(0),U(0),U(32));else Run(opt.fast_mh?"mh_fast":"mh",opt.fast_mh?"mh_pool_project_production":"mh_pool_project",size_t(W/4)*(H/4)*64,P(poolcrop),Weight("block4-ds.f32"),P(source),W/4,H/4,U(0),U(0),U(32));poolcrop.reset();last={};
 U starts[]={5,9,15},counts[]={4,6,8},channels[]={64,128,256};for(U g=0;g<3;g++){U w=W/(4u<<g),h=H/(4u<<g);for(U j=0;j<counts[g];j++){
 if(j==1&&SpEnabled(channels[g],false)){source=SpStage(source,w,h,channels[g],starts[g]+1,counts[g]-2);j=counts[g]-2;continue;}
 const bool half_tail=j+1==counts[g]&&j>0&&EncoderHalfTail(g);if(g<2)skip_b8[g].reset();source=Body(source,w,h,channels[g],shifts[j],starts[g]+j,j+1==counts[g],opt.mh_byte_stream&&(j>0||(g==0&&pool64_byte)),opt.mh_byte_stream&&j+1<counts[g],half_tail,half_tail&&EncoderSkipByte(g)?&skip_b8[g]:nullptr);if(g<2)skip_half[g]=half_tail;}skips[g+1]=source;source=Down(source,w,h,channels[g],Block(starts[g]+counts[g]-1,"ds"),false,g<2&&skip_half[g]);}
 if(pulse_lease.Active()){++pulse_site_visits;const unsigned reject=PulseRejectMask(true);pulse_last_reject_mask=reject;if(!reject){++pulse_record_attempts;pulse_lease.Record();if(pulse_lease.Active())++pulse_record_accepted;if(!pulse_runtime_reported){pulse_runtime_reported=true;std::fprintf(stderr,"submit_pulse requested=1 active=%u reason=%s site=C512_encoder_start preceding=ordered_Down_c256 launch_mode=hipModuleLaunchKernel mode=timed_single current_anyorder=%u record_attempts=%llu record_accepted=%llu lifetime_pdl_calls=%u frame_prefix_pdl=%u\n",unsigned(pulse_lease.Active()),pulse_lease.Active()?"eligible-first-record":"record-api-error",unsigned(pdl_anyorder),pulse_record_attempts,pulse_record_accepted,pdl_calls,pdl_calls-pulse_frame_pdl_start);}}else if(!pulse_fallback_logged){pulse_fallback_logged=true;std::fprintf(stderr,"submit_pulse requested=1 active=0 reason=frame-scope-fallback reject_mask=%u record_attempts=%llu pdl_calls=%u frame_prefix_pdl=%u pdl_anyorder=%u history=%u shape=%ux%u\n",reject,pulse_record_attempts,pdl_calls,pdl_calls-pulse_frame_pdl_start,unsigned(pdl_anyorder),unsigned(pulse_frame_history),W,H);}}for(U j=0;j<8;j++){source=Body(source,W/32,H/32,512,shifts[j],23+j,j==7);}skips[4]=source;gather_fold=GatherFoldOk();source=Down(source,W/32,H/32,512,"head-matrix.f32",true);U vw=W==1920?32:W/64,vh=W==1920?20:H/64;if(W!=1920&&(vw*vh)%16)vh++;if(free_geometry){vw=(W/64+3)/4*4;vh=(H/64+3)/4*4;}/* 2026-09-17: token grid padded to a multiple of 16 tokens by one row of zero tokens (1600x960: 25x15 real -> 25x16), the way 1920x1152 pads 30x18 -> 32x20 */U n=vw*vh;Stage("head",source);if(!gather_fold)source=Gather(source,n,false);source=AdaptiveVitGroup(source,n);if(!gather_fold)source=Gather(source,n,true);source=Up(source,skips[4],vw,vh,W/32,H/32,1024,512,"decoder39-weights.f32");gather_fold=false;skips[4].reset();Stage("block39",source);for(U b=40;b<=47;b++)source=Body(source,W/32,H/32,512,Shift(b),b,false);
 U ups[]={48,56,62},cs[]={256,128,64},ends[]={55,61,65};
 /* 2026-10-01 swin-body-gap: when the next decoder stage is the fused wave-owned Up and the module exports *_up_lb, the chain tail
    (post 0: already on the E4M3 lattice) writes bytes and the Up reads them (bit-exact; 4x less traffic both ways). */
 auto up_body=[&](U g){return DecoderUpBody(g);};
 auto tail_lb=[&](U g){if(!opt.mh_byte_stream||opt.skip_blocks.count(ends[g]))return false;if(g==2)return C32UpBodyPath()&&!opt.skip_blocks.count(66)&&HasFn("c32_wave1",c32_skip_byte?"c32_wave1_up_b8_lb":"c32_wave1_up_lb");return up_body(g+1)&&HasFn("c64_wave2","c"+std::to_string(cs[g+1])+"_wave2_up_lb");};
 bool low_bytes=false;
 for(U g=0;g<3;g++){U ow=W/(16u>>g),oh=H/(16u>>g);const bool byte_up=opt.decoder_byte&&opt.mh_byte_stream&&opt.decoder_h16w&&opt.fast_deep;U begin=ups[g];
 if(g>0&&wave_owned_active&&byte_up&&opt.packed_weights&&opt.grouped_mh_contract&&opt.mh_proj_diag_fb&&opt.mh_project_crop&&opt.mh_input_mapped&&!opt.skip_blocks.count(begin)&&ow%2==0&&oh%2==0){source=UpBody(source,skips[3-g],ow,oh,cs[g],begin,low_bytes,g>0&&skip_half[2-g],g>0?skip_b8[2-g]:Tensor{});if(g>0)skip_b8[2-g].reset();low_bytes=false;++begin;}
 else
 source=Up(source,skips[3-g],ow/2,oh/2,ow,oh,cs[g]*2,cs[g],Block(ups[g],"weights"),byte_up);skips[3-g].reset();for(U b=begin;b<=ends[g];b++){
 if(b==ups[g]+1&&SpEnabled(cs[g],true)){source=SpStage(source,ow,oh,cs[g],b,ends[g]-b);b=ends[g]-1;continue;}
 source=Body(source,ow,oh,cs[g],Shift(b),b,false,opt.mh_byte_stream&&(b>ups[g]||byte_up),opt.mh_byte_stream&&(b<ends[g]||(b==ends[g]&&tail_lb(g))));}low_bytes=tail_lb(g);}C32Result chain{};U c32_begin=66;
 if(C32UpBodyPath()){chain=C32UpBody(source,skips[0],W/2,H/2,low_bytes);low_bytes=false;c32_begin=67;source.reset();}
 else
 {if(c32_skip_byte)throw std::runtime_error("byte C32 skip needs the wave-owned up");source=Up(source,skips[0],W/4,H/4,W/2,H/2,64,32,"block66-weights.f32");}skips[0].reset();c32_skip_byte=false;for(U b=c32_begin;b<=69;b++){if(opt.skip_blocks.count(b)){if(!opt.raw_chain)throw std::runtime_error("C32 skip needs the raw chain");if(b==69)SkipChainFinish(chain,W/2,H/2,false);if(chain.main)source=chain.main;continue;}chain=opt.raw_chain?C32Chain(source,chain.raw?&chain:nullptr,W/2,H/2,Shift(b),Block(b,"ffn"),Block(b,"attention"),b==69,false):C32(source,W/2,H/2,Shift(b),Block(b,"ffn"),Block(b,"attention"));source=chain.main;if(source)Stage("block"+std::to_string(b),source);}chain={};
 C32Result post{};
 if(opt.post_merge_fold){U sx=(opt.post_shift&1)?4:0,sy=(opt.post_shift&2)?4:0,ww=W+2*sx,hh=H+2*sy,windows=ww*hh/64;if(opt.post_head_fused){const bool lowb=c32_post_low_byte;const bool rgba=request_rgba&&lowb&&HasFn("c32_wave1","c32_wave1_post_b8_rgba");graph_output_stride=rgba?4:3;Tensor out;if(rgba){out=multi_feed[multi_next];multi_next^=1;if(!out)throw std::runtime_error("RGBA outputs must be prepared before producer wait");}else out=(final_rgb?std::make_shared<Allocation>(api,final_rgb,size_t(W)*H*12):New(size_t(W)*H*3));c32_post_low_byte=false;if(native_post_output&&final_pass){
 if(rgba)throw std::runtime_error("auxiliary post requires final RGB output");
 Run("c32_wave1",lowb?"c32_wave1_post_b8_logit":"c32_wave1_post_logit",windows,P(source),P(skip0),Weight("post70-scales.f32"),PackedC32Weight("post70-ffn.f32",false),PackedC32Weight("post70-attention.f32",true),P(color),Weight("post70-head.f32"),P(out),windows,W,H,sx,sy,.03125f,P(native_post_row),native_post_output);
 }else if(temporal_feature_tap_active){
 if(!lowb||rgba)throw std::runtime_error("temporal feature tap post layout changed");
 Run("c32_wave1","c32_wave1_post_b8_features",windows,P(source),P(skip0),Weight("post70-scales.f32"),PackedC32Weight("post70-ffn.f32",false),PackedC32Weight("post70-attention.f32",true),P(color),Weight("post70-head.f32"),P(out),windows,W,H,sx,sy,.03125f,P(temporal_features));
}else Run(lowb?"c32_wave1":"c32_fused_ffn",lowb?(rgba?"c32_wave1_post_b8_rgba":"c32_wave1_post_b8"):"c32_post_merge_head_half",windows,P(source),P(skip0),Weight("post70-scales.f32"),PackedC32Weight("post70-ffn.f32",false),PackedC32Weight("post70-attention.f32",true),P(color),Weight("post70-head.f32"),P(out),windows,W,H,sx,sy,.03125f);source.reset();skip0.reset();Stage("block70",out);return out;}
  auto raw=New(size_t(ww)*hh*16);Run("c32_fused_ffn","c32_post_merge_fused_half",windows,P(source),P(skip0),Weight("post70-scales.f32"),PackedC32Weight("post70-ffn.f32",false),PackedC32Weight("post70-attention.f32",true),P(raw),windows,W,H,sx,sy);post={Tensor{},Tensor{},raw,ww,hh,sx,sy};source.reset();skip0.reset();}
 else{auto merged=New(size_t(W)*H*32);Run(opt.fast_c32?"boundary_fast":"boundary",opt.pre_main8?"hip_post_merge_fast_skip8":opt.fast_c32?"hip_post_merge_fast":"hip_post_merge",size_t(W)*H*32,P(source),P(skip0),Weight("post70-scales.f32"),P(merged),W,H);source.reset();skip0.reset();post=C32(merged,W,H,opt.post_shift,"post70-ffn.f32","post70-attention.f32");}
 auto out=(final_rgb?std::make_shared<Allocation>(api,final_rgb,size_t(W)*H*12):New(size_t(W)*H*3));Run(opt.fast_c32?"boundary_fast":"boundary",opt.half_c32?"hip_post_head_fast_half":opt.fast_c32?"hip_post_head_fast":"hip_post_head_exact",size_t(W)*H*3,P(post.raw),P(color),Weight("post70-head.f32"),P(out),W,H,post.workw,post.sx,post.sy,.03125f);Stage("block70",out);return out;}
 std::vector<float> Infer(const std::vector<float>&rgba,const std::vector<float>&noise,const std::vector<float>*history=nullptr){
  if(rgba.size()!=size_t(W)*H*4||noise.size()!=50331648||(history&&history->size()!=rgba.size()))throw std::runtime_error("network input sizes");
  if(opt.graph){Synchronize();ClearGraph();graph_warmed=false;}auto color=Upload(rgba.data(),rgba.size()*4),noisegpu=opt.fast_prefix?Tensor{}:Upload(noise.data(),noise.size()*4),hist=history?Upload(history->data(),history->size()*4):Tensor{};
  wall_timings.clear();auto out=RunGraph(color,noisegpu,hist);std::vector<float>result(size_t(W)*H*3);api.Check(api.hipStreamSynchronize(stream),"before readback");api.Check(api.hipMemcpy(result.data(),P(out),result.size()*4,2),"final readback");if(opt.wall_profile){for(auto&m:wall_timings)std::printf("serialized_kernel_ms %s %.6f calls=%u\n",m.first.c_str(),m.second.first,m.second.second);}
  if(opt.profile){bool valid=true;std::map<std::string,double>ms;for(auto&t:timings){float elapsed;api.Check(api.hipEventElapsedTime(&elapsed,t.begin,t.end),"event elapsed");if(!std::isfinite(elapsed)||elapsed<0)valid=false;ms[t.name]+=elapsed;api.hipEventDestroy(t.begin);api.hipEventDestroy(t.end);}timings.clear();double total=0;for(auto&m:ms){std::printf("kernel_ms %s %.6f\n",m.first.c_str(),m.second);total+=m.second;}if(valid)std::printf("kernel_ms TOTAL %.6f\n",total);else std::printf("PROFILE INVALID: negative/nonfinite HIP event intervals; discard this iteration\n");}return result;}
 // Device callers initialize once and use this stream for external fence waits/signals.
 void SetNoise(const std::vector<float>&noise){if(!opt.fast_prefix&&noise.size()!=50331648)throw std::runtime_error("noise size");api.Check(api.hipStreamSynchronize(stream),"set noise");ClearGraph();graph_warmed=false;device_noise=opt.fast_prefix?Tensor{}:Upload(noise.data(),noise.size()*4,true);}
 private: void ConfigureSubmitPulse(bool platform_scope,bool driver_validated)noexcept{
  pulse_platform_scope=platform_scope;pulse_driver_validated=driver_validated;
  const bool requested=opt.submit_pulse!=0;
  if(requested&&PulseProfileCompatible()&&platform_scope){try{pulse_capabilities_ok=CheckPulseCapabilities();}catch(const std::exception&e){std::fprintf(stderr,"submit_pulse capabilities unavailable (%s); old NN remains attempted\n",e.what());}}
  unsigned reject=PulseRejectMask();
  bool active=pulse_lease.Configure(requested,!reject,1,0);
  if(requested)std::fprintf(stderr,"submit_pulse requested=%s initialized=%u active=0 reason=%s init_reject_mask=%u driver=%s site=C512_encoder_start preceding=ordered_Down_c256 mode=timed_single\n",opt.submit_pulse==2?"auto":"1",unsigned(active),reject?"init-scope-reject":active?"pending-first-record":"api-init-failure",reject,driver_validated?"validated":"unvalidated-driver");
 }
 public: bool CloseSubmitPulse()noexcept{return pulse_lease.Close();}
 bool CheckPulseCapabilities(){
  if(!wave_owned_active||!c512_m32_active||!vit_proj_n64_active||vit_stream_active!=3||!vit_contract_byte_edge||!pool64_byte_available)return false;
  const char*requiredPulseC32[]={"c32_fast_ffn_attention_fused","c32_fast_ffn_attention_fused_half","c32_fast_ffn_attention_fused_half_mapped","c32_fast_ffn_attention_fused_half_chain","c32_fast_ffn_attention_fused_half_finish_main8","c32_fast_ffn_attention_fused_half_chain_finish","c32_fast_ffn_attention_fused_half_prefix_finish_main8","c32_fast_ffn_attention_fused_half_chain_finish_dcrop","c32_post_merge_head_half","c32_post_merge_fused_half","c32_wave1_chain","c32_wave1_mapped","c32_wave1_finish","c32_wave1_finish_dcrop","c32_wave1_post","c32_wave1_prefix","c32_wave1_finish_dcrop_b8","c32_wave1_finish_dcrop_b8d","c32_wave1_prefix_b8d","c32_wave1_mapped_b8","c32_wave1_finish_b8","c32_wave1_post_b8","c32_wave1_post_b8_rgba","c32_wave1_post_b8_features","c32_wave1_up_b8","c32_wave1_up"};
  for(const char*name:requiredPulseC32)if(!HasFn("c32_wave1",name)){std::fprintf(stderr,"submit_pulse unsupported-profile missing=%s:%s\n","c32_wave1",name);return false;}
  const std::pair<const char*,const char*> critical[]={
   {"c512_m32_deep","split_ffn_one_w2f8"},{"c512_m32_mh","mh_qkv_normalize_frag_c512_m32"},
   {"vit_stream","vit_stream_contract_frag_bout"},{"vit_stream","vit_stream_qkv_frag_bin_w5f8"},{"vit_stream","vit_stream_project_n64_bb"},
   {"mh_fast","mh_pool_project_group_c256"}};
  for(const auto&entry:critical)if(!HasFn(entry.first,entry.second)){std::fprintf(stderr,"submit_pulse unsupported-profile missing=%s:%s\n",entry.first,entry.second);return false;}
  if(!modules.count("sp")){Handle m{};int status=api.LoadModule(&m,(opt.modules+"/swin-persistent.hsaco").c_str());if(status){if(m)api.hipModuleUnload(m);return false;}modules["sp"]=m;}
  for(const char*name:{"sp_init","sp_run256_w16","sp_recover256_w16"})if(!HasFn("sp",name)){std::fprintf(stderr,"submit_pulse unsupported-profile missing=%s:%s\n","sp",name);return false;}
  const bool pair_export=HasFn("sp","sp_init_pair");if(opt.submit_pulse==2&&pair_export){std::fprintf(stderr,"submit_pulse unvalidated-profile SP init_pair differs from validated stock recipe\n");return false;}
  std::fprintf(stderr,"submit_pulse capabilities=1 c32_full26=1 wave_owned=%u c512_m32=%u vit_proj_n64=%u vit_stream=%u vit_byte_edge=%u pool64_byte_edge=%u sp_w16=1 sp_init_pair=%u\n",unsigned(wave_owned_active),unsigned(c512_m32_active),unsigned(vit_proj_n64_active),vit_stream_active,unsigned(vit_contract_byte_edge),unsigned(pool64_byte_available),unsigned(pair_export));
  return true;
 }
 bool PulseProfileCompatible()const noexcept{return opt.runtime==7&&opt.pooled&&opt.wmma&&opt.wave&&opt.fast_prefix&&opt.fast_c32&&opt.fast_mh&&opt.fast_deep&&opt.packed_weights&&opt.packed_c32&&opt.fp8_normalized&&opt.fp8_av&&opt.fp8_deep&&opt.raw_chain&&opt.prefix_inline&&opt.fused_qkv_norm&&opt.ffn_qkv&&opt.ffn_qkv_max_c==256&&opt.grouped_mh_contract&&opt.split_ffn_fused&&opt.split_mix_blocked&&opt.split_project_blocked&&opt.vit_qkv_fused&&opt.vit_attn_fused&&opt.vit_qkv_fp8&&opt.vit_contract_frag&&opt.vit_weight_mask==1&&opt.vit_pack_input&&opt.mh_project_crop&&opt.pool_project_group&&opt.wave_owned&&final_direct_compatible&&opt.dup_prefix.empty()&&opt.mh_byte_stream&&opt.mh_ffn_frag256&&opt.decoder_byte&&opt.c512_m32&&opt.vit_proj_n64&&opt.vit_stream==3&&opt.swin_run&&!opt.vit_byte_stream&&!opt.vit_half_stream&&!opt.vit_qkv_n4&&!opt.vit_split_k&&!opt.sparse_weights;}
 unsigned PulseRejectMask(bool at_site=false)const noexcept{const char*ae=std::getenv("DLSS5_VIT_ADAPTIVE");return (pulse_frame_history?1u:0u)|(!pulse_platform_scope?131072u:0u)|(opt.submit_pulse==2&&!pulse_driver_validated?262144u:0u)|(!PulseProfileCompatible()||!pulse_capabilities_ok?524288u:0u)|(pulse_bridge_diagnostics?2097152u:0u)|(at_site&&!SwinRunActive()?1048576u:0u)|(opt.graph?2u:0u)|(opt.profile?4u:0u)|(opt.wall_profile?8u:0u)|(opt.experimental_temporal?16u:0u)|(opt.temporal_feature_tap?32u:0u)|(!opt.skip_blocks.empty()?64u:0u)|(multi_pass!=1?128u:0u)|(multi_pass==3&&multi_predict?256u:0u)|(multi_skin?512u:0u)|((ae&&*ae&&std::strcmp(ae,"0"))?1024u:0u)|(pdl_anyorder?2048u:0u)|(at_site&&!pulse_ordered_down_c256?65536u:0u)|(!opt.pool_project_group?32768u:0u)|(!fast_numeric?8192u:0u)|(!((W==1600&&H==960)||(W==1920&&H==1152))?16384u:0u);}
 bool PulseEligible()const noexcept{return PulseRejectMask()==0;}
 // Optional raster auxiliary output: two floats per processing pixel, FP32 logit
 // followed by the selected module's Hrtz diagnostic value (identity in FAST1). Caller owns the device buffer until
 // all enqueued work completes; configuration is serialized before warm-up.
 // Normal Enqueue writes this only on the final real pass; predicted MP3 uses
 // pass 2's logits. The consumer applies history after prediction/skin blending.
 struct PostAuxiliary {void* data=nullptr;size_t bytes=0;U width=0,height=0;U pixel_stride=8;};
 bool NativeHistorySupported()const{
#ifdef HIP_MP_RAW_EXPORT
  // This diagnostic path runs a separate per-pass export schedule. Do not admit
  // auxiliary history until that schedule has the same final-pass contract.
  return false;
#else
  return wave_owned_active&&opt.post_merge_fold&&opt.post_head_fused&&!opt.graph&&!opt.experimental_temporal&&!opt.temporal_feature_tap;
#endif
 }
 void EnableNativePostHistory(const std::vector<float>&row,const PostAuxiliary&aux){
  if(row.size()!=32||!aux.data||aux.bytes<size_t(W)*H*8||aux.width!=W||aux.height!=H||aux.pixel_stride!=8||native_post_row||!NativeHistorySupported())throw std::runtime_error("auxiliary post layout or network unsupported");
  for(float v:row)if(!std::isfinite(v))throw std::runtime_error("auxiliary post nonfinite weight");
  // Fn follows the actual selected module, including norm900. Never silently
  // replace an optimized upstream module with a different numerical route.
  Fn("c32_wave1","c32_wave1_post_logit");Fn("c32_wave1","c32_wave1_post_b8_logit");
  native_post_row=Upload(row.data(),128,true);native_post_output=aux.data;
 }
 bool GraphEnabled()const{return opt.graph;}
 bool WaveOwnedActive()const{return wave_owned_active;}
 bool SwinRunActive()const{return SwinRunCompatible(opt)&&!sp_disabled&&(!sp_error_host||!sp_error_host[0].load(std::memory_order_acquire));}
 bool C512M32Active()const{return c512_m32_active;}
 unsigned VitStreamActive()const{return vit_stream_active;}
 bool VitProjN64Active()const{return vit_proj_n64_active;}
 void* TemporalFeatureBuffer()const{return temporal_feature_tap_active&&temporal_features?temporal_features->ptr:nullptr;}
 Handle Stream()const{return stream;}
 Api& Runtime(){return api;}
 void Synchronize(){api.Check(api.hipStreamSynchronize(stream),"network completion");}
 // Serialize with Enqueue on this instance. Does not change environment/hotkey
 // preferences; callers opt out when their temporal policy requires full ViT.
 void SetAdaptiveReuseAllowed(bool allowed){if(adaptive_allowed!=allowed){adaptive_allowed=allowed;adaptive_dirty=true;}}

 void Enqueue(void*rgba,void*history,void*rgb_output,U seed){pulse_ordered_down_c256=false;pulse_frame_pdl_start=pdl_calls;pulse_frame_history=history!=nullptr;if(adaptive_prev_history!=history||adaptive_prev_seed!=seed||adaptive_prev_input!=rgba){adaptive_dirty=true;adaptive_prev_history=history;adaptive_prev_seed=seed;adaptive_prev_input=rgba;}
  if(!rgba||!rgb_output||(!device_noise&&!opt.fast_prefix)||!opt.pooled||opt.profile||opt.wall_profile||observer||!opt.dump_dir.empty())throw std::runtime_error("device graph requires initialized noise, pooled mode and no diagnostic readbacks");
  if(opt.graph){
   if(!api.hipGraphLaunch)api.EnableGraphs();
   bool same=graph_warmed&&graph_input==rgba&&graph_history==history&&graph_output==rgb_output&&graph_seed==seed;
   if(!same){Synchronize();ClearGraph();graph_warmed=false;graph_input=rgba;graph_history=history;graph_output=rgb_output;graph_seed=seed;}
   if(graph_exec){api.Check(api.hipGraphLaunch(graph_exec,stream),"graph replay");++graph_replays;return;}
   if(graph_warmed){
    Synchronize();api.Check(api.hipStreamBeginCapture(stream,1),"graph begin");
    bool active=true;graph_capturing=true;try{
     EnqueueRaw(rgba,history,rgb_output,seed);
     graph_capturing=false;auto status=api.hipStreamEndCapture(stream,&captured_graph);active=false;api.Check(status,"graph end");
     api.Check(api.hipGraphInstantiate(&graph_exec,captured_graph,nullptr,nullptr,0),"graph instantiate");
     api.Check(api.hipGraphLaunch(graph_exec,stream),"graph first replay");++graph_builds;++graph_replays;return;
    }catch(...){graph_capturing=false;if(active){Handle abandoned{};api.hipStreamEndCapture(stream,&abandoned);if(abandoned)api.hipGraphDestroy(abandoned);}ClearGraph();graph_warmed=false;throw;}
   }
   EnqueueRaw(rgba,history,rgb_output,seed);graph_warmed=true;return;
  }
  if(opt.experimental_temporal){
   const bool active=ExperimentalTemporalActive();temporal_feature_tap_active=active;
   if(!active){experimental_ready=false;EnqueueRaw(rgba,nullptr,rgb_output,0);return;}
   const bool use=history&&experimental_ready;const U valid=opt.temporal_valid_height;
   if(use)Run("temporal_history","temporal_warp_uv",W*H,P(experimental_history),history,P(experimental_prefix),P(experimental_raw),P(experimental_recip),W,valid,H);
   EnqueueRaw(rgba,use?P(experimental_prefix):nullptr,rgb_output,seed);
   if(use){U count=W*valid;float blend=.73974609375f;U enabled=1;
    Run("temporal_history","hip_gate_head",count,P(temporal_features),P(experimental_weights),P(experimental_logit),count);
    Run("temporal_history","hip_gate_resolve",count,rgb_output,P(experimental_raw),P(experimental_logit),P(experimental_sig),rgb_output,count,blend,enabled);
   }
   Run("temporal_history","temporal_store",W*valid,rgb_output,P(experimental_history),W,valid);
   experimental_ready=true;++experimental_frames;if(experimental_frames<=3||experimental_frames%120==0)std::fprintf(stderr,"history_experiment active=1 frame=%u use_history=%u seed=%u valid=%ux%u\n",experimental_frames,unsigned(use),seed,W,valid);
   return;
  }
  EnqueueRaw(rgba,history,rgb_output,seed);
 }
 public:
 void InvalidateExperimentalHistory(){experimental_ready=false;experimental_frames=0;}
 bool ExperimentalTemporalConfigured()const{return opt.experimental_temporal;}
 bool ExperimentalTemporalActive()const{return opt.experimental_temporal&&multi_pass==1&&!multi_skin&&!opt.graph&&NativeExperimentalTemporalCompatible();}
 private:
 void EnqueueRaw(void*rgba,void*history,void*rgb_output,U seed){
  opt.seed=seed;auto color=std::make_shared<Allocation>(api,rgba,size_t(W)*H*16);auto hist=history?std::make_shared<Allocation>(api,history,size_t(W)*H*16):Tensor{};
#ifdef HIP_MP_RAW_EXPORT
  const char*export_dir=std::getenv("DLSS5_MP_RAW_EXPORT");bool export_raw=export_dir&&*export_dir;
  auto save_raw=[&](const char*name,const Tensor&t,size_t channels){if(!export_raw)return;Synchronize();std::vector<float>v(size_t(W)*H*channels);api.Check(api.hipMemcpy(v.data(),P(t),v.size()*4,2),"raw export");std::string path=std::string(export_dir)+"/"+name+".f32";FILE*f=fopen(path.c_str(),"wb");if(!f)throw std::runtime_error("raw export open");fwrite(v.data(),4,v.size(),f);fclose(f);};
  save_raw("x",color,4);auto out=RunGraph(color,device_noise,multi_pass==1||!native_post_output?hist:Tensor{},false,nullptr,multi_pass==1);save_raw("y1",out,3);if(export_raw&&multi_pass==3&&!multi_predict&&!multi_skin){auto f=MultiPassFeed(out);out=RunGraph(f,device_noise,native_post_output?Tensor{}:hist,false,nullptr,false);save_raw("y2",out,3);f=MultiPassFeed(out);out=RunGraph(f,device_noise,hist);save_raw("y3",out,3);}else if(multi_pass>1){out=MultiPassRest(out,hist,color);color.reset();}
  save_raw("final",out,3);
#else
  // Borrow the bridge-owned final RGB sink. Intermediate multi-pass tensors stay private.
  // Overlap sessions and aliased input/history preserve the original copy path.
  // Allocation(api, pointer, bytes) has owned=false; it never frees or synchronizes this pointer.
  auto overlaps_output=[&](void*input){if(!input)return false;const auto a=reinterpret_cast<uintptr_t>(input),b=reinterpret_cast<uintptr_t>(rgb_output);return a<b+size_t(W)*H*12&&b<a+size_t(W)*H*16;};
  void*final_rgb=final_direct_compatible&&!overlaps_output(rgba)&&!overlaps_output(history)?rgb_output:nullptr;
  auto out=RunGraph(color,device_noise,multi_pass==1||!native_post_output?hist:Tensor{},multi_pass>1&&DirectRgbaAllowed(),multi_pass==1?final_rgb:nullptr,multi_pass==1);const U first_stride=graph_output_stride;if(multi_pass>1){out=MultiPassRest(out,hist,color,first_stride,final_rgb);color.reset();}
#endif
  for(unsigned repeat=0;P(out)!=rgb_output&&repeat<HIP_FINAL_COPY_REPEAT;repeat++)
   api.Check(api.hipMemcpyAsync(rgb_output,P(out),size_t(W)*H*12,3,stream),"device output copy");
 }
 /* DLSS5_MULTI_PASS feed: RGB (12 B/px) -> RGBA (16 B/px) with strided device copies into a persistent buffer whose alpha lanes were
    set to 1.0 once. Two buffers alternate so pass 3 never overwrites the input pass 2 is still reading on the stream (stream order already
    serialises them; the second buffer only removes the aliasing with the tensor held by RunGraph). Two buffers of W*H*16 bytes are prepared before the producer wait, also for two-pass prediction. */
 U multi_pass=1;Tensor multi_feed[2];U multi_next=0;
 using Memcpy2DFn=int(*)(void*,size_t,const void*,size_t,size_t,size_t,int,Handle);Memcpy2DFn memcpy2d{};
 std::set<U> multi_skip;
 U graph_output_stride=3,skin_first_stride=3;bool multi_predict=true,multi_skin=false;Tensor predict_gain,skin_first,skin_mask,skin_result;
 #if defined(_MSC_VER)
 __declspec(noinline)
 #else
 __attribute__((noinline))
 #endif
 Tensor MultiPassRest(Tensor out,const Tensor&hist,const Tensor&original,U stride=3,void*final_rgb=nullptr){
  if(multi_skin){if(!skin_first)throw std::runtime_error("skin resources must be prepared before producer wait");skin_first_stride=stride;api.Check(api.hipMemcpyAsync(P(skin_first),P(out),size_t(W)*H*stride*4,3,stream),"save first pass for skin");}
  /* passes 2..N: DLSS5_MULTI_PASS_SKIP_BLOCKS added to the configured skip set (restored after, also on a throw) */
  std::set<U> saved;const bool swap=!multi_skip.empty();if(swap){saved=opt.skip_blocks;opt.skip_blocks.insert(multi_skip.begin(),multi_skip.end());}
  struct Restore{Options&o;std::set<U>&s;bool on;~Restore(){if(on)o.skip_blocks.swap(s);}}restore{opt,saved,swap};
  if(multi_pass==3&&multi_predict){
   if(!modules.count("mp_predict"))throw std::runtime_error("predictor must be prepared before producer wait");
   auto first=out;auto feed=MultiPassFeed(first,stride);auto second=RunGraph(feed,device_noise,hist);
   if(!predict_gain)predict_gain=New(size_t((W+15)/16)*((H+15)/16));auto predicted=final_rgb&&!multi_skin?std::make_shared<Allocation>(api,final_rgb,size_t(W)*H*12):New(size_t(W)*H*3);
   if(stride==4){Run("mp_predict","mp_predict_gain_stride",size_t((W+15)/16)*((H+15)/16)*32,P(original),P(first),P(second),P(predict_gain),W,H,stride);Run("mp_predict","mp_predict_apply_stride",size_t(W)*H,P(first),P(second),P(predict_gain),P(predicted),W,H,stride);}else{Run("mp_predict","mp_predict_gain",size_t((W+15)/16)*((H+15)/16)*32,P(original),P(first),P(second),P(predict_gain),W,H);
   Run("mp_predict","mp_predict_apply",size_t(W)*H,P(first),P(second),P(predict_gain),P(predicted),W,H);}
   return multi_skin?BlendSkin(original,predicted,final_rgb):predicted;
  }
  for(U pass=1;pass<multi_pass;pass++){auto feed=MultiPassFeed(out,stride);out.reset();out=RunGraph(feed,device_noise,pass+1==multi_pass||!native_post_output?hist:Tensor{},pass+1<multi_pass&&DirectRgbaAllowed(),pass+1==multi_pass&&!multi_skin?final_rgb:nullptr,pass+1==multi_pass);stride=graph_output_stride;}return multi_skin?BlendSkin(original,out,final_rgb):out;}
 public:
 /* Pass count changed at run time (add-on hot reload / hotkey, 2026-10-03). Same value = no-op. A graph is rebuilt; feed buffers
    are allocated on the next multi-pass frame (outside capture: the warm frame). */
 void SetMultiPass(U n){if(temporal_feature_tap_active&&!opt.experimental_temporal&&n!=1)throw std::runtime_error("temporal feature tap is MP1-only");if(n<1||n>3||n==multi_pass)return;if(opt.experimental_temporal){experimental_ready=false;temporal_feature_tap_active=n==1;}if(n>1)PrepareMultiPassFeeds();multi_pass=n;adaptive_dirty=true;if(opt.graph){Synchronize();ClearGraph();graph_warmed=false;}}
 U MultiPass()const{return multi_pass;}
 void SetMultiPassPredict(bool enabled){if(enabled==multi_predict)return;if(enabled){EnsurePredictModule();if(multi_pass>1)PrepareMultiPassFeeds();}multi_predict=enabled;std::fprintf(stderr,"multi_pass_predict=%u requested_passes=%u actual_network_passes=%u\n",unsigned(multi_predict),multi_pass,multi_predict&&multi_pass==3?2:multi_pass);if(opt.graph){Synchronize();ClearGraph();graph_warmed=false;}}
 bool MultiPassPredict()const{return multi_predict;}
 void SetMultiPassSkinProtect(bool enabled){if(enabled==multi_skin)return;if(enabled)PrepareSkin();multi_skin=enabled;if(opt.graph){Synchronize();ClearGraph();graph_warmed=false;}}
 bool MultiPassSkinProtect()const{return multi_skin;}
 private:
 // Initialization and hot setters run before the bridge queues this frame's producer wait.
 // Preparing only one feed during the two-pass warm-up leaves the alternating slot uninitialized.
 void PrepareSkin(){
  if(!modules.count("mp_skin")){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/multi-pass-skin.hsaco").c_str()),"skin module");modules["mp_skin"]=m;Fn("mp_skin","mp_skin_mask");Fn("mp_skin","mp_skin_blend");HasFn("mp_skin","mp_skin_blend_stride");}
  if(!skin_first){skin_first=New(size_t(W)*H*4);skin_mask=New(size_t(W)*H);skin_result=New(size_t(W)*H*3);}
 }
 Tensor BlendSkin(const Tensor&original,const Tensor&many,void*final_rgb=nullptr){
  Tensor result=final_rgb?std::make_shared<Allocation>(api,final_rgb,size_t(W)*H*12):skin_result;
  Run("mp_skin","mp_skin_mask",size_t(W)*H,P(original),P(skin_mask),W,H);
  if(skin_first_stride==4)Run("mp_skin","mp_skin_blend_stride",size_t(W)*H,P(skin_first),P(many),P(skin_mask),P(result),U(W*H),skin_first_stride);else Run("mp_skin","mp_skin_blend",size_t(W)*H,P(skin_first),P(many),P(skin_mask),P(result),U(W*H));
#ifdef HIP_MP_RAW_EXPORT
  if(const char*dir=std::getenv("DLSS5_MP_RAW_EXPORT");dir&&*dir){Synchronize();std::vector<float>v(size_t(W)*H);api.Check(api.hipMemcpy(v.data(),P(skin_mask),v.size()*4,2),"skin mask export");FILE*f=fopen((std::string(dir)+"/mask.f32").c_str(),"wb");if(f){fwrite(v.data(),4,v.size(),f);fclose(f);}}
#endif
  return result;
 }
 void PrepareMultiPassFeeds(){
  if(multi_feed[0]&&multi_feed[1])return;
  std::vector<float>ones(size_t(W)*H*4,1.f);
  for(auto&feed:multi_feed)if(!feed)feed=Upload(ones.data(),ones.size()*4,true);
 }
 void EnsurePredictModule(){
  if(!modules.count("mp_predict")){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/multi-pass-predict.hsaco").c_str()),"multi-pass predictor module");modules["mp_predict"]=m;}
  Fn("mp_predict","mp_predict_gain");Fn("mp_predict","mp_predict_apply");HasFn("mp_predict","mp_predict_gain_stride");HasFn("mp_predict","mp_predict_apply_stride");
 }
 bool DirectRgbaAllowed(){const char*v=std::getenv("DLSS5_LAB_DIRECT_RGBA");if(v&&!std::strcmp(v,"0"))return false;
  if(!wave_owned_active||!HIP_C32_POST_LOW_BYTE||!opt.raw_chain||!opt.c32_finish_fused||!opt.post_merge_fold||!opt.post_head_fused||!HasFn("c32_wave1","c32_wave1_post_b8_rgba"))return false;
  if(multi_predict&&(!HasFn("mp_predict","mp_predict_gain_stride")||!HasFn("mp_predict","mp_predict_apply_stride")))return false;
  if(multi_skin&&!HasFn("mp_skin","mp_skin_blend_stride"))return false;return true;}
 Tensor MultiPassFeed(const Tensor&rgb,U stride=3){
  if(stride==4)return rgb;
  if(!memcpy2d){memcpy2d=reinterpret_cast<Memcpy2DFn>(GetProcAddress(api.dll,"hipMemcpy2DAsync"));if(!memcpy2d)throw std::runtime_error("DLSS5_MULTI_PASS needs hipMemcpy2DAsync");}
  auto&feed=multi_feed[multi_next];multi_next^=1;
  if(!feed)throw std::runtime_error("multi-pass feeds must be prepared before producer wait");
  /* hipMemcpy2DAsync on Windows silently copies at most 2^20 rows (1080/900 tiers have 2.2M/1.5M pixels): chunk at 2^19 rows. */
  const size_t n=size_t(W)*H,chunk=size_t(1)<<19;
  const char*dup_text=std::getenv("DLSS5_LAB_FEED_DUP");const U dup=dup_text?U(std::stoul(dup_text)):1u;
  for(U d=0;d<dup;d++)for(size_t o=0;o<n;o+=chunk)api.Check(memcpy2d(static_cast<char*>(P(feed))+o*16,16,static_cast<const char*>(P(rgb))+o*12,12,12,std::min(chunk,n-o),3,stream),"multi-pass feed");
  return feed;
 }
};
}
