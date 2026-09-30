#pragma once
// SDKless HIP host graph. Scalar numerical validation path, not a realtime backend.
#include "hip_api.h"
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
struct Allocation {Api*api;void*ptr{};size_t bytes,capacity;bool owned=true;
 // Sparse weights (Options::sparse_weights): ptr is a reserved address range of `bytes`; only `sparse_maps` (offset,size,handle) are backed. capacity = backed bytes.
 size_t sparse_reserved{};struct SparseMap{size_t offset,size;void*handle;};std::vector<SparseMap>sparse_maps;
 Allocation(Api&a,size_t n):api(&a),bytes(n),capacity(n){if(!n)throw std::runtime_error("zero HIP allocation");a.Check(a.hipMalloc(&ptr,n),"hipMalloc");}Allocation(Api&a,void*p,size_t n):api(&a),ptr(p),bytes(n),capacity(n),owned(false){}
 Allocation(Api&a,size_t n,size_t granularity,const std::vector<std::pair<size_t,size_t>>&live):api(&a),bytes(n),capacity(0){
  sparse_reserved=(n+granularity-1)/granularity*granularity;a.Check(a.hipMemAddressReserve(&ptr,sparse_reserved,granularity,nullptr,0),"hipMemAddressReserve");
  try{hip_probe::MemAllocationProp prop{};prop.type=1;prop.location.type=1;hip_probe::MemAccessDesc acc{};acc.location.type=1;acc.flags=3;
   for(auto&r:live){SparseMap m{r.first,r.second,nullptr};a.Check(a.hipMemCreate(&m.handle,m.size,&prop,0),"hipMemCreate");
    if(int e=a.hipMemMap(static_cast<char*>(ptr)+m.offset,m.size,0,m.handle,0)){a.hipMemRelease(m.handle);a.Check(e,"hipMemMap");}
    sparse_maps.push_back(m);if(int e=a.hipMemSetAccess(static_cast<char*>(ptr)+m.offset,m.size,&acc,1))throw std::runtime_error("hipMemSetAccess rc="+std::to_string(e)+" offset="+std::to_string(m.offset)+" size="+std::to_string(m.size)+" reserved="+std::to_string(sparse_reserved)+" bytes="+std::to_string(n)+" maps="+std::to_string(sparse_maps.size()));capacity+=m.size;}
  }catch(...){ReleaseSparse();throw;}}
 void ReleaseSparse(){for(auto&m:sparse_maps){api->hipMemUnmap(static_cast<char*>(ptr)+m.offset,m.size);api->hipMemRelease(m.handle);}sparse_maps.clear();if(ptr)api->hipMemAddressFree(ptr,sparse_reserved);ptr=nullptr;}
 ~Allocation(){if(ptr&&owned){api->hipDeviceSynchronize();if(sparse_reserved)ReleaseSparse();else api->hipFree(ptr);}}};
using Tensor=std::shared_ptr<Allocation>;
#ifndef HIP_C512_PAD16
#define HIP_C512_PAD16 1 /* 2026-09-30: see NewPad16; 0 = old mh_shift_pack path (results/shift-pack-900-20260930) */
#endif
#ifndef HIP_C32_SKIP_BYTE
#define HIP_C32_SKIP_BYTE 1 /* 2026-09-30: block4 main (C32 skip, read only by the block66 up) as E4M3 bytes when c32-wave1 exports the _b8 pair; 0 or older modules = f32 (results/c32-align-20260930) */
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
inline bool FastPrefixFromEnvironment(){const char*v=std::getenv("DLSS5_HIP_FAST");if(!v)return false;if(std::strcmp(v,"0")&&std::strcmp(v,"1"))throw std::runtime_error("DLSS5_HIP_FAST must be 0 or 1");return !std::strcmp(v,"1");}
struct Options {bool swin_run=false;unsigned vit_stream=0; /* bit0: AV FP8, bit1: contract F16; explicit paired dispatch */bool wave_owned=false;bool c512_m32=false; /* 2026-09-26: C512 QKV+mix 32 tokens per wave (results/c512-ffn-20260926) */bool vit_proj_n64=false; /* 2026-09-26: ViT projection 16 tokens x 64 columns per wave (results/m32-sweep-20260926) */bool pdl=false; /* 2026-09-25: chain launches any-order + tile flags (results/pdl-chain-20260925) */ bool mh_ffn_frag256=false;bool decoder_byte=false;std::set<U>skip_blocks;U width=512,height=512,seed=0,post_shift=0;std::string assets,modules,dump_dir,dump_only;unsigned ffn_qkv_max_c=64,runtime=7,device=0/* HIP device index; the bridge picks the one matching the D3D12 adapter (multi-GPU / iGPU hosts) */,tiled_ffn_min_c=64,vit_weight_mask=0;bool mh_window_fused=false;/* 2026-09-17 experiment: C64 blocks as one kernel per 8x8 window (FFN+QKV+attention+projection, Development/HIP/c64_window_fused.hip, module c64-window-fused.hsaco) */bool mh_proj_diag_fb=false;/* 2026-09-17: residual scales as diagonal MMAs in the byte-feature attention-project kernels (c64/c128/c256 *_fb_diag) */bool sparse_weights=false;std::string sparse_filter;/* diagnostic: only keys ending with this go sparse */bool sparse_full=false;/* diagnostic: VMM path with every page backed (isolates mapping from dead-range accounting) *//* 2026-09-17 experiment, DO NOT SHIP: in-place packing leaves 3/4 of every E4M3 region (1/2 of every f16 region) dead in the f32 layout; this maps only the live 64 KiB pages of each weight through the VMM API (addresses unchanged; weights 607 -> 236 MiB). Copies work, but on driver 32.0.31007.2048 kernels hang (64 KiB chunks) or fail (2 MiB chunks) on any reservation backed by more than one physical chunk, and hipMemMap refuses partial mappings of one chunk; only reservation == one chunk is kernel-visible, which cannot skip holes. All three hashes change. Kept for re-testing on newer drivers (vmm_probe / vmm_kernel_probe). */bool vit_qkv_fused=false,ffn_qkv=false,grouped_mh_contract=false,direct_prefix_input=false,prefix_fused=false,mh_input_mapped=false,mh_project_crop=false,vit_qkv_blocked=false,split_project_blocked=false,split_mix_blocked=false,split_ffn_fused=false,fused_ffn_project=false,post_merge_fold=false,pre_main8=false,raw_chain=false,elide_identity_shift=false,fast_vit=false,wmma=false,pooled=false,profile=false,wall_profile=false,wave=false,tiled=false,fast_c32=false,fused_c32=false,fast_mh=false,fast_deep=false,fast_prefix=false,mh_wave=false,fused_ffn=false,fused_mh=false,packed_weights=false,packed_c32=false,fp8_normalized=false,fp8_ffn=false,fp8_av=false,fp8_deep=false,fp8_middle=false,half_c32=false,crop_c32=false,fused_qkv_norm=false,vit_pack_input=false,vit_contract_blocked=false,vit_blocked=false,vit_split_k=false,mapped_c32=false,fused_mh_ffn=false,tiled_mh_ffn=false,graph=false,vit_attn_fused=false,vit_qkv_fp8=false,mh_byte_stream=false,vit_expand_m4=false,vit_expand_frag=false,vit_byte_stream=false,vit_half_stream=false,vit_qkv_n4=false,ffn_qkv_batched_norm=false,pool_project_fused=false,qkv_norm_wave_c512=false,vit_expand_m2=false,vit_input_tiled=false,vit_ffn_fused=false,split_mix_fused=false,split_mix_h16w=false,pool_project_h16w=false,decoder_h16w=false,mh_attn_w16=false,c512_qkv_frag=false,c512_proj_frag=false,c512_proj_tiles=false,mh_feature_byte=false,mh_proj_diag=false,post_head_fused=false,c32_finish_fused=false,down_crop_fused=false,pool32_h16w=false,tiled_ffn_small=false,pool_project_group=false,prefix_inline=false,vit_proj_frag=false,vit_qkv_frag=false,vit_contract_frag=false,mh_ffn_frag=false;unsigned dup_count=2;std::string dup_prefix;/* diagnostic: launch matching kernels twice (pure kernels: identical output, frame delta = in-frame marginal cost) */};
// 2026-09-26: C512 QKV/mix 32-token kernels (DLSS5_HIP_C512_M32) replace only the production h16w-mix + frag-QKV configuration.
// 2026-09-26: the ViT projection 64-column kernel (DLSS5_HIP_VIT_PROJ_N64) replaces only the production fragment projection.
inline bool VitProjN64Compatible(const Options&opt){return opt.vit_proj_n64&&opt.fast_deep&&opt.packed_weights&&opt.vit_proj_frag;}
inline bool VitStreamCompatible(const Options&o){return o.vit_stream&&o.vit_stream<=3&&VitProjN64Compatible(o)&&o.fast_vit&&o.vit_attn_fused&&o.vit_qkv_fp8&&o.vit_qkv_frag&&o.vit_contract_frag&&o.vit_contract_blocked&&!o.vit_byte_stream&&!o.vit_ffn_fused&&!o.vit_split_k;}
inline bool C512M32Compatible(const Options&opt){return opt.c512_m32&&opt.fast_deep&&opt.packed_weights&&opt.split_mix_h16w&&!opt.split_mix_fused&&opt.split_ffn_fused&&opt.c512_proj_tiles&&opt.c512_qkv_frag&&opt.fused_qkv_norm;}
// Opt-in recipe validated against prod8; incompatible diagnostic layouts retain the original path.
inline bool WaveOwnedCompatible(const Options&o){
 return o.wave_owned&&!o.graph&&std::none_of(o.skip_blocks.begin(),o.skip_blocks.end(),[](U b){return b<=22||(b>=48&&b<=70);})&&o.fast_c32&&o.fused_ffn&&o.packed_c32&&o.half_c32&&o.raw_chain&&o.pre_main8&&o.c32_finish_fused&&o.prefix_inline&&o.post_head_fused&&o.post_merge_fold&&o.mapped_c32&&o.fast_mh&&o.fused_mh&&o.grouped_mh_contract&&o.packed_weights&&o.mh_byte_stream&&o.mh_proj_diag_fb&&o.fp8_av&&o.fp8_normalized&&o.ffn_qkv&&o.ffn_qkv_max_c>=256&&o.mh_input_mapped&&o.mh_project_crop&&o.elide_identity_shift;
}
inline bool SwinRunCompatible(const Options&o){
 return o.swin_run&&o.pooled&&WaveOwnedCompatible(o)&&
  ((o.width==1600&&o.height==960)||(o.width==1920&&o.height==1152));
}
inline std::atomic<int> AdaptivePreviewState{0};
class Network {
 bool wave_owned_active=false;bool c32_skip_byte=false;bool c512_m32_active=false;bool vit_proj_n64_active=false;unsigned vit_stream_active=0;
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
 Api api;Handle stream{};std::map<std::string,Handle>modules,functions;std::map<std::string,Tensor>weights;Options opt;
 struct Timing{std::string name;Handle begin{},end{};};std::vector<Timing>timings;
 std::map<std::string,std::pair<double,unsigned>>wall_timings;
 Handle captured_graph{},graph_exec{};void*graph_input{},*graph_history{},*graph_output{};U graph_seed{};bool graph_warmed{},graph_capturing{};unsigned graph_builds{},graph_replays{};
 void ClearGraph(){if(graph_exec){api.hipGraphExecDestroy(graph_exec);graph_exec=nullptr;}if(captured_graph){api.hipGraphDestroy(captured_graph);captured_graph=nullptr;}}
 std::vector<Tensor>pool;Tensor device_noise;std::map<U,Tensor>gather_maps[2];
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
 Tensor Upload(const void*p,size_t bytes,bool persistent=false){if(!bytes||bytes%4)throw std::runtime_error("upload size");auto t=persistent?std::make_shared<Allocation>(api,bytes):New(bytes/4);api.Check(api.hipStreamSynchronize(stream),"before upload");api.Check(api.hipMemcpy(t->ptr,p,bytes,1),"upload");return t;}
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
 void* PackedVitQkvWeightFrag(const std::string&name){auto key=name+"@qkv-f16-frag";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=3*1048576+32)throw std::runtime_error("ViT QKV compact shape");std::vector<float>scales(v.begin()+3*1048576,v.end());std::vector<uint16_t>t(size_t(3)*1048576);for(size_t part=0;part<3;part++)for(size_t nt=0;nt<64;nt++)for(size_t kt=0;kt<64;kt++)for(unsigned g=0;g<2;g++)for(unsigned rc=0;rc<16;rc++)for(unsigned e=0;e<8;e++)t[part*1048576+(nt*64+kt)*256+(g*16+rc)*8+e]=ExactWeightHalf(v[part*1048576+(nt*16+rc)*1024+kt*16+g*8+e]);v.resize(3*1048576/2+32);std::memcpy(v.data(),t.data(),t.size()*2);std::memcpy(v.data()+3*1048576/2,scales.data(),32*sizeof(float));it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 void* PackedVitQkvWeight(const std::string&name){auto key=name+"@qkv-f16-compact";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("ViT QKV shape");if(v.size()!=3*1048576+32)throw std::runtime_error("ViT QKV compact shape");PackHalfMatrix(v,3*1048576);std::memmove(v.data()+3*1048576/2,v.data()+3*1048576,32*sizeof(float));v.resize(3*1048576/2+32);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 void* PackedVitWeight(const std::string&name,U rows,U cols,bool tiled,bool frag=false){
  if(!tiled)return PackedDeepWeight(name,size_t(rows)*cols);
  auto key=name+(frag?"@vit-frag":"@vit-tiled");auto it=weights.find(key);
  if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("ViT tiled weight shape");Fp8(v,{{0,size_t(rows)*cols}});if(frag)FragmentPackedMatrix(v,0,rows,cols);else TilePackedMatrix(v,0,rows,cols);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);
 }
 /* Optional export probe: modules without the symbol keep the old launch shape. */
 bool HasFn(const std::string&m,const std::string&name){std::string key=m+":"+name;if(functions.count(key))return true;auto mi=modules.find(m);if(mi==modules.end())return false;Handle f{};if(api.hipModuleGetFunction(&f,mi->second,name.c_str()))return false;functions.emplace(key,f);return true;}
 Handle Fn(const std::string&m,const std::string&name){std::string key=m+":"+name;auto it=functions.find(key);if(it!=functions.end())return it->second;Handle f{};api.Check(api.hipModuleGetFunction(&f,modules.at(m),name.c_str()),name.c_str());functions.emplace(key,f);return f;}
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
   if(kernel=="split_mix_blocked"||kernel=="split_mix_blocked_h16w"||kernel=="split_projection_blocked"||kernel=="split_projection_blocked_t8"||kernel=="split_projection_frag")groups=count/1024;
   if(kernel=="split_ffn_fused"||kernel=="split_ffn_fused_fp8"||kernel=="split_ffn_fused_fp8_mix"||kernel=="split_ffn_fused_fp8_t8"){threads=128;groups=count/1024;}
   if(kernel=="vit_ffn_fused"){threads=512;groups=count/16384;}
   if(kernel=="vit_contract_combine"||kernel=="vit_pack_input"||kernel=="vit_pack_input_tiled")threads=256;
   if(kernel=="vit_qkv_normalize")count=Count(size_t(count)*32);
   if(kernel=="vit_attention_inverse_fast")count=Count(size_t(count)*16);
  }
  if(module=="mh_fast"){
   if(kernel.rfind("mh_pool_project_fused_c",0)==0){threads=32;groups=count/1024;}
   else if(kernel.rfind("mh_pool_project_group_c",0)==0){U c=U(std::stoul(kernel.substr(23)));threads=c;groups=(count/(2*c)+15)/16;}
   else if(kernel=="mh_qkv_normalize_wave_c512"){threads=128;groups=count/4096;}
   else if(kernel=="mh_qkv_normalize_frag_c512"){threads=32;groups=count/1024;}
   else if(kernel=="mh_attention_project_frag_c512"){threads=32;groups=count/1024;}
   else if(kernel.rfind("mh_ffn_fused_c",0)==0){U c=U(std::stoul(kernel.substr(14)));if((c!=64&&c!=128&&c!=256)||count%(c*16))throw std::runtime_error("fused FFN dispatch shape");threads=c*2;groups=count/(c*16);}
   else if(kernel=="mh_qkv_normalize_fast"||kernel=="mh_qkv_normalize_fast_wave"||kernel=="mh_qkv_normalize_fast_wave_fp8"){threads=256;if(kernel!="mh_qkv_normalize_fast")count=Count(size_t(count)*32);}
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
  if(module=="c32_wave1"&&(kernel=="c32_wave1_up"||kernel=="c32_wave1_up_b8"||kernel=="c32_wave1_finish_dcrop_b8"||kernel=="c32_wave1_finish_dcrop_b8d")){groups=count;threads=32;}
  if(module=="c512_m32_mh"||module=="c512_m32_deep"){groups=count/1024;threads=32;}
  if(module=="c512_m32_mh"&&kernel=="c512_qkv_attention_fused"){groups=count;threads=64;}
  if(module=="c512_m32_mh"&&kernel=="c512_qkv_attention_compact"){groups=count;threads=64;}
  if(vit_proj_n64_active&&module=="deep_fast"&&kernel=="vit_project_frag"&&count%1024==0){module="vit_wide_deep";kernel="vit_project_frag_n64";groups=unsigned(((count/1024+15)/16)*16);threads=32;}
  if(kernel=="vit_stream_contract_frag_hout"){module="vit_stream";groups=count/1024;threads=32;}
  if(kernel=="vit_stream_qkv_frag_hin"){module="vit_stream";groups=count/512;threads=32;if(count%3072==0&&HasFn(module,"vit_stream_qkv_frag_hin_w5")){kernel="vit_stream_qkv_frag_hin_w5";groups=96*((count/3072/16+4)/5);threads=160;}} /* w5: five waves share one head's weights through LDS; same per-wave math */
  if(vit_stream_active&&kernel=="vit_project_frag_n64"){module="vit_stream";kernel=vit_stream_active==1?"vit_stream_project_n64_b":vit_stream_active==2?"vit_stream_project_n64_h":"vit_stream_project_n64_bh";}
  if(module=="mh_window"){groups=count;threads=256;} /* one 8x8 window per 256-thread group (c64_window_fused*) */
  if(kernel.rfind("c64_attention_project",0)==0||kernel.rfind("c128_attention_project",0)==0||kernel.rfind("c256_attention_project",0)==0){groups=count;threads=(kernel.rfind("c64_attention_project",0)==0&&kernel.compare(kernel.size()-4,4,"_w16")!=0)?256:512;}
  if(module=="prefix_fast"){threads=(kernel=="dlss5_prefix_fast_project"||kernel=="dlss5_prefix_fast_fused"||kernel=="dlss5_prefix_fast_fused_raster")?32:256;if(kernel=="dlss5_prefix_fast_fused"||kernel=="dlss5_prefix_fast_fused_raster")groups=count/512;}
  if(module=="c32_fast_ffn"){bool expand=kernel=="c32_ffn_expand_fast";U tokens=count/(expand?128:32);threads=expand?512:256;groups=((tokens+63)/64)*(expand?2:1);}
  if(module=="c32_fast_attention")threads=32;
  if(opt.wave&&(kernel=="c32_attn_normalize"||kernel=="c32_attn_probabilities"||kernel=="mh_normalize"||kernel=="mh_probabilities")){module="wave";kernel+="_wave";count=Count(size_t(count)*32);}
  if((kernel=="decoder_project2x"||kernel=="decoder_project2x_h16w"||kernel=="decoder_project2x_h16w_byteout")&&(module=="deep_fast"||module=="deep_wmma")){
   if constexpr(sizeof...(A)==10){auto tuple=std::make_tuple(args...);auto integer=[](auto x)->U{if constexpr(std::is_integral_v<decltype(x)>)return U(x);else return 0;};
    const U columns=integer(std::get<9>(tuple));if(!columns||columns%16||count%columns)throw std::runtime_error("decoder tile ABI");
    groups=Count(((size_t(count)/columns+15)/16)*(columns/16));
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
 C32Result C32UpBody(Tensor low,Tensor skip,U w,U h){
  U shift=Shift(66),sx=(shift&1)?4:0,sy=(shift&2)?4:0,ww=w+2*sx,hh=h+2*sy,windows=ww*hh/64;
  auto raw=New(size_t(ww)*hh*16);
  Run("c32_wave1",c32_skip_byte?"c32_wave1_up_b8":"c32_wave1_up",windows,P(low),PackedDecoderHalf("block66-weights.f32",size_t(64)*32),P(skip),PackedC32Weight("block66-ffn.f32",false),PackedC32Weight("block66-attention.f32",true),P(raw),windows,w,h,sx,sy);
  return {Tensor{},Tensor{},raw,ww,hh,sx,sy};
 }
 C32Result C32Chain(Tensor input,const C32Result*prev,U w,U h,U shift,const std::string&fw,const std::string&aw,bool finish,bool down){
  U sx=(shift&1)?4:0,sy=(shift&2)?4:0,ww=w+2*sx,hh=h+2*sy,n=ww*hh,windows=n/64;
  if(opt.c32_finish_fused&&prev&&finish&&down&&opt.down_crop_fused&&C32SkipByte()){c32_skip_byte=true;bool db=HIP_C32_DOWN_BYTE&&opt.pool32_h16w&&opt.fast_mh&&HasFn("c32_wave1","c32_wave1_finish_dcrop_b8d")&&HasFn("mh_fast","mh_pool_project_c32_b8");auto main=New(size_t(w)*h*8),pooled=New(size_t(w/2)*(h/2)*(db?8:32));Run("c32_wave1",db?"c32_wave1_finish_dcrop_b8d":"c32_wave1_finish_dcrop_b8",windows,P(prev->raw),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(main),P(pooled),windows,U(3),U(1),w,h,sx,sy,prev->workw,prev->sx,prev->sy);C32Result r{main,pooled,Tensor{},ww,hh,sx,sy,true};r.down_byte=db;return r;}
  if(opt.c32_finish_fused&&prev&&(finish||down)){if(finish&&down)c32_skip_byte=false;bool dcrop=opt.down_crop_fused&&down;auto main=finish?New(size_t(w)*h*32):Tensor{},pooled=down?New(dcrop?size_t(w/2)*(h/2)*32:size_t(n/4)*32):Tensor{};Run("c32_fused_ffn",dcrop?"c32_fast_ffn_attention_fused_half_chain_finish_dcrop":"c32_fast_ffn_attention_fused_half_chain_finish",windows,P(prev->raw),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(main),P(pooled),windows,U(3),U(1),w,h,sx,sy,prev->workw,prev->sx,prev->sy);return {main,pooled,Tensor{},ww,hh,sx,sy,dcrop};}
  auto raw=New(size_t(n)*16);
  if(prev)Run("c32_fused_ffn","c32_fast_ffn_attention_fused_half_chain",windows,P(prev->raw),PackedC32Weight(fw,false),PackedC32Weight(aw,true),P(raw),windows,U(3),U(1),w,h,sx,sy,prev->workw,prev->sx,prev->sy);
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
  Run("c512_m32_deep","split_mix_blocked_h16w_m32",size_t((n+31)/32*32)*256,P(compact),PackedSplitFfnWeightMixHalf(Block(block,"ffwd")),P(mixed),n);
  Run("deep","split_ffn_fused_fp8_t8",size_t(n)*512,P(mixed),PackedSplitFfnWeightMixHalf(Block(block,"ffwd")),P(contract),P(contract8),n);
  mixed.reset();
  Run("deep","split_projection_frag",size_t(n)*512,P(contract8),PackedSplitProjectionFrag(Block(block,"ffwd-projection")),P(compact),P(ffn),P(ffn8),n);
  compact.reset();contract.reset();contract8.reset();
  auto av=New(size_t(n)*128);
  Run("c512_m32_mh","c512_qkv_attention_compact",size_t(ww)*hh/64*16,P(ffn8),PackedMhWeightQkvFrag(Block(block,"attention"),512),P(av),w,h,ww,hh,sx,sy);
  ffn8.reset();
  auto result=HIP_C512_PAD16?NewPad16(valid,512):New(size_t(valid)*512);
  Run("mh_fast","mh_attention_project_frag_c512",size_t(n)*512,P(av),P(ffn),PackedMhWeightQkvFrag(Block(block,"attention"),512),P(result),n,U(raw?3:0),w,h,w,U(0),U(0));
  Stage("block"+std::to_string(block),result);return result;
 }
#include "swin_persistent_network.h"
 Tensor Body(Tensor input,U w,U h,U c,U shift,U block,bool raw,bool byte_in=false,bool byte_out=false){if(PdlChainHead(block))pdl_prev={};pdl_ffn_flags=nullptr;if(opt.skip_blocks.count(block)){Stage("block"+std::to_string(block),input);return input;}U sx=(shift&1)?4:0,sy=(shift&2)?4:0,ww=(w+sx+7)&~7u,hh=(h+sy+7)&~7u,n=ww*hh;
 if(c==512&&c512_m32_active&&opt.fast_deep&&opt.packed_weights&&opt.split_mix_h16w&&!opt.split_mix_fused&&opt.split_ffn_fused&&opt.c512_proj_tiles&&opt.c512_qkv_frag&&opt.c512_proj_frag&&opt.fp8_av&&opt.fused_mh&&opt.fp8_normalized&&!byte_in&&!byte_out&&ww%8==0&&hh%8==0)return CompactC512Body(input,w,h,ww,hh,sx,sy,block,raw);
 const bool identity=opt.elide_identity_shift&&!sx&&!sy&&ww==w&&hh==h;bool mapped=opt.mh_input_mapped&&c!=512&&!identity;const bool byte_feature=(opt.mh_byte_stream||opt.mh_feature_byte)&&c!=512;if((byte_in||byte_out)&&!byte_feature)throw std::runtime_error("byte residual stream requires the mh_byte_stream C64/128/256 path");if(byte_in&&!(identity||mapped))throw std::runtime_error("byte block input requires mapped/identity FFN input");if(byte_out&&(raw||!(identity||opt.mh_project_crop)))throw std::runtime_error("byte block output requires a lattice output on the crop/identity path");if(wave_owned_active&&(c==64||c==128||(c==256&&opt.width==1920&&opt.height==1152))){
  // C256 whole-block fusion wins at 1080; retain the split PDL path at smaller tiers.
  if(!(identity||mapped)||!opt.grouped_mh_contract||!opt.packed_weights||!byte_feature||!opt.mh_proj_diag_fb)throw std::runtime_error("wave-owned block input contract");
  pdl_prev={};pdl_ffn_flags=nullptr;pdl_anyorder=false;
  auto out=New(size_t(w)*h*c/(byte_out?4:1));
  std::string name="c"+std::to_string(c)+"_wave2"+(byte_in?"_bi":"")+(byte_out?"_bo":"");
  Run("c64_wave2",name.c_str(),n/64,P(input),PackedFusedMhWeightFrag(Block(block,"ffn"),c),WaveOwnedAttentionWeight(Block(block,"attention"),c),P(out),w,h,ww,hh,sx,sy,U(raw?3:(block==48||block==55||block==61||block==65)?0:4));
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
 Tensor Down(Tensor raw,U w,U h,U c,const std::string&file,bool head=false){U ow=w/2,oh=h/2,vw=0,vh=0;if(head){U rw=w/2,rh=h/2;if(w==60&&h==36){ow=32;oh=20;}else{ow=rw;oh=(rw*rh)%16?rh+1:rh;}if(ow*oh!=rw*rh){vw=rw;vh=rh;}}if(opt.pool_project_group&&(c==64||c==128||c==256||(head&&c==512))){auto out=(HIP_C512_PAD16&&c==256)?NewPad16(size_t(ow)*oh,512):New(size_t(ow)*oh*c*2);Run("mh_fast",("mh_pool_project_group_c"+std::to_string(c)).c_str(),size_t(ow)*oh*c*2,P(raw),PackedDsWeightFrag(file,c),P(out),ow,oh,w,vw,vh);return out;}
  if(opt.pool_project_fused&&(c==64||c==128||c==256)){if((ow*oh)%16)throw std::runtime_error("fused pool project token count");auto out=New(size_t(ow)*oh*c*2);Run("mh_fast",("mh_pool_project_fused_c"+std::to_string(c)).c_str(),size_t(ow)*oh*c*2,P(raw),PackedDsWeight(file,c),P(out),ow,oh,w,vw,vh);return out;}auto pooled=New(size_t(ow)*oh*c),out=New(size_t(ow)*oh*c*2);Run("mh","mh_pool",size_t(ow)*oh*c,P(raw),P(pooled),ow,oh,w,vw,vh,c);if(opt.pool_project_h16w&&opt.fast_mh)Run("mh_fast","mh_pool_project_production_h16w",size_t(ow)*oh*c*2,P(pooled),PackedDsWeightCast(file,c),P(out),ow,oh,vw,vh,c);else Run(opt.fast_mh?"mh_fast":"mh",opt.fast_mh?"mh_pool_project_production":"mh_pool_project",size_t(ow)*oh*c*2,P(pooled),Weight(file),P(out),ow,oh,vw,vh,c);return out;}
 Tensor adaptive_anchor_in,adaptive_anchor_out,adaptive_gain,adaptive_stats,adaptive_state;
 Tensor adaptive_image_anchor,adaptive_image_signature,adaptive_image_delta;void*adaptive_image=nullptr;
 void*adaptive_prev_history=nullptr;void*adaptive_prev_input=nullptr;U adaptive_prev_seed=0;
 bool adaptive_dirty=false,adaptive_key_down=false,adaptive_user_disabled=false;U adaptive_last_mode=0,adaptive_frame=0;
 bool adaptive_active=false;U adaptive_n=0;std::chrono::steady_clock::time_point adaptive_last{};
 Tensor AdaptiveVitGroup(Tensor input,U n){
  const char*mode_s=std::getenv("DLSS5_VIT_ADAPTIVE");U mode=mode_s?U(std::stoul(mode_s)):0;
  const char*hotkey=std::getenv("DLSS5_VIT_REUSE_HOTKEY");
  if(hotkey&&!strcmp(hotkey,"1")){bool down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;if(down&&!adaptive_key_down)adaptive_user_disabled=!adaptive_user_disabled;adaptive_key_down=down;if(adaptive_user_disabled)mode=0;}
  AdaptivePreviewState.store(mode?1:0);if(mode!=adaptive_last_mode){adaptive_dirty=true;adaptive_last_mode=mode;}
  if(!mode){Tensor full=input;for(U b=31;b<=38;b++)full=Vit(full,n,b);return full;}++adaptive_frame;
  if(mode>3||opt.graph||!opt.fast_deep||!opt.pooled||opt.vit_byte_stream||std::any_of(opt.skip_blocks.begin(),opt.skip_blocks.end(),[](U b){return b>=31&&b<=38;}))throw std::runtime_error("adaptive ViT requires fast pooled graph-off non-byte-stream unskipped network, mode1..3");
  auto param=[](const char*key,float fallback){const char*v=std::getenv(key);float f=v?std::stof(v):fallback;if(!std::isfinite(f)||f<0)throw std::runtime_error("adaptive threshold");return f;};
  float global=param("DLSS5_VIT_REUSE_GLOBAL",.22f),local=param("DLSS5_VIT_REUSE_LOCAL",1.f),image_limit=param("DLSS5_VIT_REUSE_IMAGE",.35f);
  U period=4;if(const char*v=std::getenv("DLSS5_VIT_REUSE_PERIOD"))period=U(std::stoul(v));if(period<1||period>16)throw std::runtime_error("adaptive max period1..16");
  auto now=std::chrono::steady_clock::now();bool reset=adaptive_dirty||!adaptive_state||adaptive_n!=n||(adaptive_last.time_since_epoch().count()&&now-adaptive_last>std::chrono::milliseconds(500));adaptive_last=now;adaptive_dirty=false;
  if(!adaptive_gain){
   std::vector<float>gain(1024,1.f);
   if(const char*path=std::getenv("DLSS5_VIT_REUSE_GAIN");path&&*path){auto bytes=ReadBytes(path);if(bytes.size()!=4096)throw std::runtime_error("adaptive gain shape");memcpy(gain.data(),bytes.data(),4096);}
   else{std::vector<double>product(1024,1.);for(U block=31;block<=38;block++)for(const char*part:{"contract","projection"}){auto weight=ReadWeights(opt.assets+"/block"+std::to_string(block)+"-"+part+".f32");size_t matrix=!strcmp(part,"contract")?4194304:1048576;if(weight.size()!=matrix+1024)throw std::runtime_error("adaptive gain weight shape");for(U i=0;i<1024;i++)product[i]*=double(weight[matrix+i]);}for(U i=0;i<1024;i++)gain[i]=float(product[i]);}
   for(float v:gain)if(!std::isfinite(v))throw std::runtime_error("adaptive gain nonfinite");adaptive_gain=Upload(gain.data(),4096,true);
  }
  if(reset){U zero[8]{};adaptive_state=Upload(zero,sizeof(zero),true);adaptive_anchor_in=New(size_t(n)*1024);adaptive_anchor_out=New(size_t(n)*1024);adaptive_stats=New(size_t(n)*4);adaptive_n=n;U tiles=((W+31)/32)*((H+31)/32);adaptive_image_anchor=New(size_t(tiles)*3);adaptive_image_signature=New(size_t(tiles)*3);adaptive_image_delta=New(tiles);}
  U stride=W==1920?32:W/64,tiles=((W+31)/32)*((H+31)/32);
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
  if(opt.vit_byte_stream){Tensor in8=expanded_input;vit_in8.reset();if(n%16||n>640)throw std::runtime_error("ViT byte stream token count");
   auto hidden=New(size_t(n)*1024),contract8=New(size_t(n)*(opt.vit_half_stream?512:256));
   Run("deep",opt.vit_expand_frag?(opt.vit_expand_m2?"vit_expand_blocked_fp8_frag_bytein_m2":"vit_expand_blocked_fp8_frag_bytein"):"vit_expand_blocked_fp8_tiled_bytein",size_t(n)*4096,P(in8),PackedVitWeight(Block(block,"expand"),4096,1024,true,opt.vit_expand_frag),P(hidden),n,U(1024),U(4096));
   Run("deep",opt.vit_half_stream?(opt.vit_weight_mask&2?"vit_contract_blocked_fp8_tiled_hstream":"vit_contract_blocked_fp8_hstream"):(opt.vit_weight_mask&2?"vit_contract_blocked_fp8_tiled_bstream":"vit_contract_blocked_fp8_bstream"),size_t(n)*1024,P(hidden),PackedVitWeight(Block(block,"contract"),1024,4096,opt.vit_weight_mask&2),P(in8),P(contract8),n,U(4096),U(1024));hidden.reset();
   auto norm=New(size_t(n)*768);Run("deep",opt.vit_half_stream?(opt.vit_qkv_n4?"vit_qkv_project_normalize_fused_f16compact_fp8_h16in_n4":"vit_qkv_project_normalize_fused_f16compact_fp8_h16in"):"vit_qkv_project_normalize_fused_f16compact_fp8_bytein",size_t(n)*3072,P(contract8),PackedVitQkvWeight(Block(block,"qkv")),P(norm),n);
   auto av=New(size_t(n)*256);std::string fused=n<=256?"vit_attention_fused_256":n<=400?"vit_attention_fused_400":"vit_attention_fused_640";fused+="_bytein_bout";Run("deep",fused.c_str(),size_t(n)*512,P(norm),P(av),n);norm.reset();
   auto out=New(size_t(n)*1024);Tensor out8;if(block<38)out8=New(size_t(n)*256);
   Run("deep",opt.vit_half_stream?"vit_project_bytein_bout_hskip":"vit_project_bytein_bout",size_t(n)*1024,P(av),PackedDeepWeight(Block(block,"projection"),1048576),P(contract8),P(out),P(out8),n);
   vit_in8=out8;Stage("block"+std::to_string(block),out);return out;}std::string expand_name="vit_expand_blocked_fp8";if(opt.vit_weight_mask&1)expand_name+="_tiled";if(opt.vit_pack_input)expand_name+="_bytein";if(opt.vit_expand_m4)expand_name+="_m4";if(opt.vit_expand_frag)expand_name=opt.vit_expand_m4?"vit_expand_blocked_fp8_frag_bytein_m4":opt.vit_expand_m2?"vit_expand_blocked_fp8_frag_bytein_m2":"vit_expand_blocked_fp8_frag_bytein";if(opt.vit_input_tiled)expand_name="vit_expand_blocked_fp8_frag_tiledin";auto hidden=opt.vit_ffn_fused?Tensor{}:New(size_t(n)*(opt.fp8_deep?1024:4096)),contract=New(size_t(n)*((vit_stream_active&2)?512:1024));if(opt.vit_ffn_fused){if(n%16)throw std::runtime_error("fused ViT FFN token count");Run("deep","vit_ffn_fused",size_t(n)*1024,P(expanded_input),PackedVitWeight(Block(block,"expand"),4096,1024,true,true),PackedVitWeight(Block(block,"contract"),1024,4096,false),P(input),P(contract),n);expanded_input.reset();}else{Run("deep",opt.vit_blocked?expand_name.c_str():opt.fp8_deep?"vit_expand_fp8":"vit_expand",size_t(n)*4096,P(expanded_input),PackedVitWeight(Block(block,"expand"),4096,1024,opt.vit_weight_mask&1,opt.vit_expand_frag),P(hidden),n,U(1024),U(4096));expanded_input.reset();if(opt.vit_split_k){auto parts=New(size_t(n)*4096);auto cw=PackedVitWeight(Block(block,"contract"),1024,4096,opt.vit_weight_mask&2);Run("deep",opt.vit_contract_blocked?(opt.vit_weight_mask&2?"vit_contract_parts_blocked_fp8_tiled":"vit_contract_parts_blocked_fp8"):"vit_contract_parts_fp8",size_t(n)*4096,P(hidden),cw,P(parts),n);Run("deep","vit_contract_combine",size_t(n)*1024,P(parts),cw,P(input),P(contract),n);}else{if(opt.vit_contract_frag&&opt.vit_contract_blocked)Run("deep",(vit_stream_active&2)?"vit_stream_contract_frag_hout":"vit_contract_blocked_fp8_frag",size_t(n)*1024,P(hidden),PackedVitWeight(Block(block,"contract"),1024,4096,true,true),P(input),P(contract),n,U(4096),U(1024));else Run("deep",opt.vit_contract_blocked?(opt.vit_weight_mask&2?"vit_contract_blocked_fp8_tiled":"vit_contract_blocked_fp8"):opt.fp8_deep?"vit_project_fp8":"vit_project",size_t(n)*1024,P(hidden),PackedVitWeight(Block(block,"contract"),1024,4096,opt.vit_weight_mask&2),P(input),P(contract),n,U(4096),U(1024));}hidden.reset();}auto qkv=opt.vit_qkv_fused?Tensor{}:New(size_t(n)*3072),norm=New(opt.vit_qkv_fp8?size_t(n)*768:size_t(n)*3072);auto qw=(opt.vit_qkv_fused&&opt.packed_weights)?(opt.vit_qkv_frag&&opt.vit_qkv_fp8?PackedVitQkvWeightFrag(Block(block,"qkv")):PackedVitQkvWeight(Block(block,"qkv"))):Weight(Block(block,"qkv"));if(opt.vit_qkv_fused)Run("deep",opt.vit_qkv_fp8?(opt.vit_qkv_frag?((vit_stream_active&2)?"vit_stream_qkv_frag_hin":"vit_qkv_project_normalize_fused_f16compact_fp8_frag"):"vit_qkv_project_normalize_fused_f16compact_fp8"):opt.packed_weights?"vit_qkv_project_normalize_fused_f16compact":"vit_qkv_project_normalize_fused",size_t(n)*3072,P(contract),qw,P(norm),n);else{Run("deep",opt.vit_qkv_blocked?"vit_qkv_project_blocked":"vit_qkv_project",size_t(n)*3072,P(contract),qw,P(qkv),n);Run("deep","vit_qkv_normalize",size_t(n)*96,P(qkv),qw,P(norm),n);}qkv.reset();auto av=New(size_t(n)*((vit_stream_active&1)?256:1024));if(opt.vit_attn_fused){if(n%16||n>640)throw std::runtime_error("fused ViT attention token count");std::string fused=n<=256?"vit_attention_fused_256":n<=400?"vit_attention_fused_400":"vit_attention_fused_640";if(opt.vit_qkv_fp8)fused+="_bytein";if(vit_stream_active&1)fused+="_bout";Run("deep",fused.c_str(),size_t(n)*512,P(norm),P(av),n);}else{auto ex=New(size_t(n)*32*n),inv=New(size_t(n)*32);Run("deep",opt.fast_vit?"vit_attention_scores_fast":"vit_attention_scores",size_t(n)*32*n,P(norm),P(ex),n);Run("deep",opt.fast_vit?"vit_attention_inverse_fast":"vit_attention_inverse",size_t(n)*32,P(ex),P(inv),n);Run("deep",opt.fast_vit?"vit_attention_av_fast":"vit_attention_av",size_t(n)*1024,P(norm),P(ex),P(inv),P(av),n);ex.reset();inv.reset();}norm.reset();auto out=New(size_t(n)*1024);if(opt.vit_proj_frag)Run("deep","vit_project_frag",size_t(n)*1024,P(av),PackedVitProjectionFrag(Block(block,"projection")),P(contract),P(out),n,U(1024),U(1024));else Run("deep","vit_project",size_t(n)*1024,P(av),PackedDeepWeight(Block(block,"projection"),1048576),P(contract),P(out),n,U(1024),U(1024));Stage("block"+std::to_string(block),out);return out;}
 void* PackedDecoderHalf(const std::string&name,size_t matrix){auto key=name+"@decoder-f16r";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()<matrix)throw std::runtime_error("decoder weight shape");HalfR(v,0,matrix);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
 Tensor UpBody(Tensor low,Tensor skip,U w,U h,U c,U block){
  U shift=Shift(block),sx=(shift&1)?4:0,sy=(shift&2)?4:0,ww=(w+sx+7)&~7u,hh=(h+sy+7)&~7u;
  pdl_prev={};pdl_ffn_flags=nullptr;pdl_anyorder=false;
  auto out=New(size_t(w)*h*c/4);
  std::string name="c"+std::to_string(c)+"_wave2_up";
  Run("c64_wave2",name.c_str(),size_t(ww)*hh/64,P(low),PackedFusedMhWeightFrag(Block(block,"ffn"),c),WaveOwnedAttentionWeight(Block(block,"attention"),c),P(out),w,h,ww,hh,sx,sy,U(4),PackedDecoderHalf(Block(block,"weights"),size_t(2*c)*c),P(skip));
  Stage("block"+std::to_string(block),out);return out;
 }
 Tensor Up(Tensor input,Tensor skip,U iw,U ih,U ow,U oh,U ic,U oc,const std::string&file,bool byte_out=false){if(byte_out&&(!(opt.decoder_h16w&&opt.fast_deep)||oc==32))throw std::runtime_error("decoder byte output requires quantized fast path");auto out=(HIP_C512_PAD16&&oc==512&&!byte_out)?NewPad16(size_t(ow)*oh,512):New(size_t(ow)*oh*oc/(byte_out?4:1));if(opt.decoder_h16w&&opt.fast_deep)Run("deep",byte_out?"decoder_project2x_h16w_byteout":"decoder_project2x_h16w",size_t(iw)*ih*oc,P(input),PackedDecoderHalf(file,size_t(ic)*oc),P(skip),P(out),iw,ih,ow,oh,ic,oc);else Run("deep","decoder_project2x",size_t(iw)*ih*oc,P(input),Weight(file),P(skip),P(out),iw,ih,ow,oh,ic,oc);return out;}
 static U Shift(U block){static constexpr U s[]={0,3,1,2,0,3,1,2,0,3,1,2,0,3,1,2,1,2,0,3,1,2,0,3,1,2,0,3,1,2};if(block<40||block>69)throw std::runtime_error("decoder shift");return s[block-40];}
public:
 Network(const Network&)=delete;Network&operator=(const Network&)=delete;
 explicit Network(Options o):api(o.runtime),opt(std::move(o)),W(opt.width),H(opt.height){wave_owned_active=WaveOwnedCompatible(opt);c512_m32_active=C512M32Compatible(opt);vit_proj_n64_active=VitProjN64Compatible(opt);vit_stream_active=VitStreamCompatible(opt)?opt.vit_stream:0;if(opt.sparse_weights){api.EnableVmm();hip_probe::MemAllocationProp prop{};prop.type=1;prop.location.type=1;api.Check(api.hipMemGetAllocationGranularity(&sparse_granularity,&prop,1),"VMM granularity");if(!sparse_granularity||sparse_granularity&(sparse_granularity-1))throw std::runtime_error("VMM granularity");}if(opt.tiled_mh_ffn&&!opt.fused_mh_ffn)throw std::runtime_error("tiled FFN weights require fused FFN");if(opt.fused_mh_ffn&&(!opt.fp8_middle||!opt.packed_weights||!opt.mh_wave))throw std::runtime_error("fused MH FFN requires packed weights and byte middle");if(opt.fused_qkv_norm&&!opt.fp8_normalized)throw std::runtime_error("fused QKV requires byte normalized pipeline");if((opt.vit_weight_mask||opt.vit_pack_input)&&(!opt.vit_blocked||!opt.vit_contract_blocked||opt.vit_weight_mask>3))throw std::runtime_error("ViT layout requires blocked pipeline and mask0..3");if((opt.vit_blocked||opt.vit_contract_blocked)&&(!opt.fast_deep||!opt.fp8_deep||!opt.packed_weights))throw std::runtime_error("blocked ViT requires packed byte deep pipeline");if(opt.vit_split_k&&(!opt.fast_deep||!opt.fp8_deep))throw std::runtime_error("ViT SplitK requires byte deep path");if(opt.vit_attn_fused&&(!opt.fast_deep||!opt.fast_vit))throw std::runtime_error("fused ViT attention requires fast deep/ViT pipeline");if(opt.vit_qkv_fp8&&(!opt.vit_qkv_fused||!opt.packed_weights||!opt.vit_attn_fused))throw std::runtime_error("ViT byte QKV requires fused packed QKV and fused attention");if(opt.vit_expand_frag&&(!opt.vit_blocked||!opt.vit_pack_input||!(opt.vit_weight_mask&1)))throw std::runtime_error("ViT expand fragment layout requires blocked tiled byte-input expand");if(opt.vit_expand_m4&&(!opt.vit_blocked||!opt.vit_pack_input||!(opt.vit_weight_mask&1)))throw std::runtime_error("ViT expand M4 requires blocked tiled byte-input expand");if(opt.vit_byte_stream&&(!opt.vit_qkv_fp8||!opt.vit_attn_fused||!opt.vit_contract_blocked||!opt.vit_pack_input||!(opt.vit_weight_mask&1)||opt.vit_split_k||opt.vit_expand_m4))throw std::runtime_error("ViT byte stream requires fused byte QKV/attention, blocked tiled byte-input expand, no split-K/M4");if((opt.vit_half_stream&&!opt.vit_byte_stream)||(opt.vit_qkv_n4&&!opt.vit_half_stream))throw std::runtime_error("ViT half stream requires the byte stream; QKV N4 requires the half stream");if(opt.mh_feature_byte&&(!opt.ffn_qkv||opt.ffn_qkv_max_c!=256||!opt.fused_ffn_project||!opt.mh_project_crop||!opt.fp8_av||!opt.fused_mh||!opt.packed_weights))throw std::runtime_error("MH byte feature requires the fused FFN/QKV crop pipeline");if(opt.c512_proj_tiles&&(!opt.c512_qkv_frag||!opt.split_ffn_fused||opt.split_mix_fused))throw std::runtime_error("C512 tiled projection requires fragment QKV and fused split FFN");if(opt.c512_proj_frag&&(!opt.mh_project_crop||!opt.fp8_av||!opt.packed_weights))throw std::runtime_error("C512 fragment projection requires crop byte-AV packed pipeline");if(opt.c512_qkv_frag&&(!opt.split_project_blocked||!opt.packed_weights||!opt.fused_qkv_norm||!opt.fp8_normalized||opt.qkv_norm_wave_c512))throw std::runtime_error("C512 fragment QKV requires blocked packed projection and byte normalized QKV");if(opt.split_mix_h16w&&(!opt.split_mix_blocked||!opt.packed_weights||opt.split_mix_fused))throw std::runtime_error("split mix half weights require blocked mix with packed weights");if(opt.split_mix_fused&&(!opt.split_ffn_fused||!opt.packed_weights||!opt.split_mix_blocked))throw std::runtime_error("fused split mix requires packed fused split FFN");if(opt.vit_ffn_fused&&(!opt.vit_expand_frag||!opt.vit_pack_input||(opt.vit_weight_mask&2)||opt.vit_split_k||opt.vit_byte_stream||opt.vit_input_tiled||opt.vit_expand_m2||opt.vit_expand_m4))throw std::runtime_error("fused ViT FFN requires fragment expand weights, packed input, row-major contract weights");if(opt.vit_input_tiled&&(!opt.vit_expand_frag||opt.vit_expand_m4||opt.vit_expand_m2||opt.vit_byte_stream||!opt.vit_pack_input))throw std::runtime_error("ViT tiled input requires the fragment expand, packed input, no M2/M4/byte stream");if(opt.vit_expand_m2&&(!opt.vit_expand_frag||opt.vit_expand_m4))throw std::runtime_error("ViT expand M2 requires the fragment expand and no M4");if(opt.qkv_norm_wave_c512&&(!opt.fused_qkv_norm||!opt.packed_weights))throw std::runtime_error("C512 wave QKV requires fused packed QKV normalize");if(opt.pool_project_fused&&!opt.fast_mh)throw std::runtime_error("fused pool project requires the fast MH module");if(opt.ffn_qkv_batched_norm&&(!opt.ffn_qkv||opt.mh_byte_stream))throw std::runtime_error("batched QKV normalization requires the fused FFN/QKV kernel without the byte stream");if(opt.mh_byte_stream){if(!opt.ffn_qkv||opt.ffn_qkv_max_c!=256||!opt.fused_ffn_project||!opt.mh_input_mapped||!opt.mh_project_crop||!opt.fp8_normalized||!opt.fp8_av||!opt.fused_mh||!opt.packed_weights||!opt.elide_identity_shift)throw std::runtime_error("MH byte stream requires the fused FFN/QKV, mapped input, crop project pipeline");for(U s:opt.skip_blocks)if((s>=5&&s<=22)||(s>=48&&s<=65))throw std::runtime_error("MH byte stream cannot skip a C64/128/256 block");}if(opt.mapped_c32&&!opt.crop_c32)throw std::runtime_error("mapped C32 requires half/crop pipeline");if(opt.crop_c32&&!opt.half_c32)throw std::runtime_error("fused C32 crop requires half raw");if(opt.half_c32&&(!opt.fast_c32||!opt.fused_ffn))throw std::runtime_error("half C32 requires fused fast pipeline");if(opt.fp8_middle&&!opt.fp8_ffn)throw std::runtime_error("byte middle requires byte FFN pipeline");if(opt.fp8_deep&&!opt.fast_deep)throw std::runtime_error("byte deep requires fast deep pipeline");if((opt.fp8_ffn||opt.fp8_av)&&(!opt.fast_mh||!opt.mh_wave||(opt.fp8_av&&!opt.fp8_normalized)))throw std::runtime_error("byte intermediates require compatible MH pipeline");if(opt.fp8_normalized&&(!opt.fast_mh||!opt.mh_wave||!opt.fused_mh))throw std::runtime_error("FP8 normalized requires fast MH wave/fused pipeline");if(opt.post_shift>3)throw std::runtime_error("post shift must be 0..3");if(!((W==512&&H==512)||(W==1920&&H==1152)||(W==1280&&H==768)||(W==1600&&H==1024)||(W==1600&&H==960)))throw std::runtime_error("unsupported processing geometry");api.Check(api.hipInit(0),"hipInit");api.Check(api.hipSetDevice(int(opt.device)),"device");api.Check(api.hipStreamCreate(&stream),"stream");try{const char*names[][2]={{"c32","c32_prefix_reference.hsaco"},{"mh","multihead-reference.hsaco"},{"deep","deep_reference.hsaco"},{"boundary","boundary_reference.hsaco"}};for(auto&v:names){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+v[1]).c_str()),v[1]);modules[v[0]]=m;}if(opt.wmma){const char*wm[][2]={{"c32_wmma","c32_wmma.hsaco"},{"mh_wmma","multihead-wmma.hsaco"},{"deep_wmma","deep_wmma.hsaco"}};for(auto&v:wm){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+v[1]).c_str()),v[1]);modules[v[0]]=m;}}if(opt.fused_ffn){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+(opt.packed_c32?"/c32_fused_ffn_attention-packed.hsaco":"/c32_fused_ffn_attention.hsaco")).c_str()),"fused FFN attention module");modules["c32_fused_ffn"]=m;}
if(c512_m32_active){
 const char*extra[][2]={{"c512_m32_mh","c512-m32-mh.hsaco"},{"c512_m32_deep","c512-m32-deep.hsaco"}};
 for(auto&entry:extra){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+entry[1]).c_str()),entry[1]);modules[entry[0]]=m;}}
if(vit_stream_active){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/vit-stream.hsaco").c_str()),"vit-stream.hsaco");modules["vit_stream"]=m;}
if(vit_proj_n64_active){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/vit-wide-deep.hsaco").c_str()),"vit-wide-deep.hsaco");modules["vit_wide_deep"]=m;}
if(wave_owned_active){
 const char*extra[][2]={{"c64_wave2","c64-wave2.hsaco"},{"c32_wave1","c32-wave1.hsaco"}};
 for(auto&entry:extra){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+entry[1]).c_str()),entry[1]);modules[entry[0]]=m;}}
if(SwinRunCompatible(opt)){
 std::ifstream probe(std::filesystem::u8path(opt.modules+"/swin-persistent.hsaco"),std::ios::binary);
 if(!probe){opt.swin_run=false;std::fprintf(stderr,"swin_run:nomodule\n");}
}
if(opt.fused_mh){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/multihead_fused_attention.hsaco").c_str()),"fused MH attention module");modules["mh_fused"]=m;}
if(opt.mh_window_fused){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/c64-window-fused.hsaco").c_str()),"C64 window fused module");modules["mh_window"]=m;}
if(opt.fast_deep){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+(opt.packed_weights?"/deep_fast-packed.hsaco":"/deep_fast.hsaco")).c_str()),"deep fast module");modules["deep_fast"]=m;}
if(opt.fast_mh){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+(opt.packed_weights?(opt.mh_wave?"/multihead-fast-padded-wave-packed.hsaco":"/multihead-fast-packed.hsaco"):(opt.mh_wave?"/multihead-fast-padded-wave.hsaco":"/multihead-fast.hsaco"))).c_str()),"MH fast module");modules["mh_fast"]=m;}
 PreflightPdl();
if(opt.fast_prefix){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/prefix_fast.hsaco").c_str()),"prefix fast module");modules["prefix_fast"]=m;}
if(opt.fused_c32){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/c32_fused_attention.hsaco").c_str()),"fused attention module");modules["c32_fused"]=m;}
if(opt.fast_c32){const char*f[][2]={{"c32_fast_ffn","c32_fast.hsaco"},{"c32_fast_attention","c32_fast_attention.hsaco"},{"boundary_fast","boundary-fast.hsaco"}};for(auto&v:f){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+v[1]).c_str()),v[1]);modules[v[0]]=m;}}if(opt.tiled){const char*t[][2]={{"c32_tiled","c32_tiled.hsaco"},{"mh_tiled","multihead-tiled.hsaco"}};for(auto&v:t){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/"+v[1]).c_str()),v[1]);modules[v[0]]=m;}}if(opt.wave){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/wave-pointwise.hsaco").c_str()),"wave module");modules["wave"]=m;}}catch(...){if(pdl_flags){api.hipStreamSynchronize(stream);api.hipFree(pdl_flags);pdl_flags=nullptr;}for(auto&m:modules)api.hipModuleUnload(m.second);api.hipStreamDestroy(stream);throw;}}
 ~Network(){api.hipStreamSynchronize(stream);SpReport();pdl_keep.clear();if(pdl_flags){api.hipFree(pdl_flags);pdl_flags=nullptr;}if(opt.graph)std::printf("graph_stats builds=%u replays=%u\n",graph_builds,graph_replays);ClearGraph();for(auto&t:timings){api.hipEventDestroy(t.begin);api.hipEventDestroy(t.end);}adaptive_image_anchor.reset();adaptive_image_signature.reset();adaptive_image_delta.reset();adaptive_anchor_in.reset();adaptive_anchor_out.reset();adaptive_gain.reset();adaptive_stats.reset();adaptive_state.reset();device_noise.reset();gather_maps[0].clear();gather_maps[1].clear();weights.clear();pool.clear();for(auto&m:modules)api.hipModuleUnload(m.second);api.hipStreamDestroy(stream);}
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
 Tensor RunGraph(Tensor color,Tensor noisegpu,Tensor hist){adaptive_image=P(color);if(opt.vit_qkv_fused&&!opt.fast_deep)throw std::runtime_error("ViT QKV fusion requires fast deep kernels");if(opt.ffn_qkv&&(!opt.grouped_mh_contract||!opt.fast_mh||!opt.fused_qkv_norm||!opt.fp8_normalized))throw std::runtime_error("FFN/QKV requires grouped byte pipeline");if(opt.grouped_mh_contract&&!opt.fused_ffn_project)throw std::runtime_error("grouped contraction requires fused project");if(opt.direct_prefix_input&&(!opt.fast_prefix||!opt.prefix_fused))throw std::runtime_error("direct input requires fused fast prefix");if(opt.mh_input_mapped&&!opt.fused_ffn_project)throw std::runtime_error("mapped MH input requires fused FFN project");if(opt.mh_project_crop&&(!opt.fast_mh||!opt.fp8_av))throw std::runtime_error("MH crop requires fast byte AV pipeline");if(opt.split_ffn_fused&&(!opt.fast_deep||!opt.fp8_deep))throw std::runtime_error("split fused FFN requires fast byte deep pipeline");if(opt.fused_ffn_project&&(!opt.fused_mh_ffn||!opt.packed_weights||!opt.fp8_middle||!opt.tiled_mh_ffn||opt.tiled_ffn_min_c!=256))throw std::runtime_error("fused FFN project requires selected packed FFN pipeline");if(opt.post_merge_fold&&(!opt.pre_main8||!opt.fused_ffn||!opt.half_c32||!opt.mapped_c32))throw std::runtime_error("post merge fold requires pre-main8 and fused half mapped pipeline");if(opt.pre_main8&&(!opt.fast_c32||!opt.half_c32||observer||!opt.dump_dir.empty()))throw std::runtime_error("pre main8 needs fast half path and no stage dumps");if(opt.raw_chain&&(!opt.fused_ffn||!opt.half_c32||!opt.mapped_c32||!opt.crop_c32||observer||!opt.dump_dir.empty()))throw std::runtime_error("raw chain requires fused half mapped crop pipeline and no stage dumps");auto tiles=opt.direct_prefix_input?color:New(size_t(W)*H*4),base=opt.direct_prefix_input?color:New(size_t(W)*H*4),prefix=opt.prefix_inline?Tensor{}:New(size_t(W)*H*32);if(!opt.direct_prefix_input)Run("boundary","hip_input_reflect",size_t(W)*H,P(color),P(base),P(tiles),W,H,W,H);color=base;if(opt.prefix_inline){if(!opt.direct_prefix_input||!opt.c32_finish_fused||!opt.pre_main8||!opt.fused_ffn||!opt.half_c32)throw std::runtime_error("inline prefix requires the direct raster input and the fused block 0 finish");inline_prefix=true;inline_rgba=tiles;inline_hist=hist;inline_seed=opt.seed;inline_temporal=U(bool(hist));}else if(opt.fast_prefix&&opt.prefix_fused){Run("prefix_fast",opt.direct_prefix_input?"dlss5_prefix_fast_fused_raster":"dlss5_prefix_fast_fused",size_t(W)*H*32,P(tiles),P(hist),Weight("block0-ffn.f32"),P(prefix),W,H,opt.seed,U(bool(hist)));}else if(opt.fast_prefix){auto features=New(size_t(W)*H*32);Run("prefix_fast","dlss5_prefix_fast_features",size_t(W)*H,P(tiles),P(hist),P(features),W,H,opt.seed,U(bool(hist)));Run("prefix_fast","dlss5_prefix_fast_project",size_t(W)*H*32,P(features),Weight("block0-ffn.f32"),P(prefix),W*H);}else{Run("c32","dlss5_prefix_reference",size_t(W)*H,P(tiles),P(hist),P(noisegpu),Weight("block0-ffn.f32"),P(prefix),static_cast<void*>(nullptr),W,H,opt.seed,U(bool(hist)));}tiles.reset();noisegpu.reset();hist.reset();auto pre=C32Body(prefix,W,H,"block0-ffn.f32","block0-attention.f32");prefix.reset();pre.raw.reset();Stage("pre-down",pre.down);Tensor skip0=pre.main,source=pre.down;pre.main.reset();pre.down.reset();Stage("block0",skip0);Tensor skips[5];U shifts[]={0,3,1,2,0,3,1,2};C32Result last;
 for(U b=1;b<=4;b++){if(opt.skip_blocks.count(b)){if(!opt.raw_chain)throw std::runtime_error("C32 skip needs the raw chain");if(b==4)SkipChainFinish(last,W/2,H/2,true);if(last.main)source=last.main;continue;}last=opt.raw_chain?C32Chain(source,last.raw?&last:nullptr,W/2,H/2,shifts[b-1],Block(b,"ffn"),Block(b,"attention"),b==4,b==4):C32(source,W/2,H/2,shifts[b-1],Block(b,"ffn"),Block(b,"attention"));source=last.main;if(source)Stage("block"+std::to_string(b),source);}Stage("block4-down",last.down);skips[0]=source;Tensor poolcrop;if(last.down_cropped)poolcrop=last.down;else{poolcrop=New(size_t(W/4)*(H/4)*32);Run("mh","mh_shift_crop",size_t(W/4)*(H/4)*32,P(last.down),P(poolcrop),W/4,H/4,last.workw/2,last.sx/2,last.sy/2,U(32));}source=New(size_t(W/4)*(H/4)*64);if(last.down_byte&&!last.down_cropped)throw std::runtime_error("byte C32 down needs the crop path");if(opt.pool32_h16w&&opt.fast_mh)Run("mh_fast",last.down_byte?"mh_pool_project_c32_b8":"mh_pool_project_production_h16w",size_t(W/4)*(H/4)*64,P(poolcrop),PackedDsWeightCast("block4-ds.f32",32),P(source),W/4,H/4,U(0),U(0),U(32));else Run(opt.fast_mh?"mh_fast":"mh",opt.fast_mh?"mh_pool_project_production":"mh_pool_project",size_t(W/4)*(H/4)*64,P(poolcrop),Weight("block4-ds.f32"),P(source),W/4,H/4,U(0),U(0),U(32));poolcrop.reset();last={};
 U starts[]={5,9,15},counts[]={4,6,8},channels[]={64,128,256};for(U g=0;g<3;g++){U w=W/(4u<<g),h=H/(4u<<g);for(U j=0;j<counts[g];j++){
 if(j==1&&SpEnabled(channels[g],false)){source=SpStage(source,w,h,channels[g],starts[g]+1,counts[g]-2);j=counts[g]-2;continue;}
 source=Body(source,w,h,channels[g],shifts[j],starts[g]+j,j+1==counts[g],opt.mh_byte_stream&&j>0,opt.mh_byte_stream&&j+1<counts[g]);}skips[g+1]=source;source=Down(source,w,h,channels[g],Block(starts[g]+counts[g]-1,"ds"));}
 for(U j=0;j<8;j++){source=Body(source,W/32,H/32,512,shifts[j],23+j,j==7);}skips[4]=source;source=Down(source,W/32,H/32,512,"head-matrix.f32",true);U vw=W==1920?32:W/64,vh=W==1920?20:H/64;if(W!=1920&&(vw*vh)%16)vh++;/* 2026-09-17: token grid padded to a multiple of 16 tokens by one row of zero tokens (1600x960: 25x15 real -> 25x16), the way 1920x1152 pads 30x18 -> 32x20 */U n=vw*vh;Stage("head",source);source=Gather(source,n,false);source=AdaptiveVitGroup(source,n);source=Gather(source,n,true);source=Up(source,skips[4],vw,vh,W/32,H/32,1024,512,"decoder39-weights.f32");skips[4].reset();Stage("block39",source);for(U b=40;b<=47;b++)source=Body(source,W/32,H/32,512,Shift(b),b,false);
 U ups[]={48,56,62},cs[]={256,128,64},ends[]={55,61,65};for(U g=0;g<3;g++){U ow=W/(16u>>g),oh=H/(16u>>g);const bool byte_up=opt.decoder_byte&&opt.mh_byte_stream&&opt.decoder_h16w&&opt.fast_deep;U begin=ups[g];
 if(g>0&&wave_owned_active&&byte_up&&opt.packed_weights&&opt.grouped_mh_contract&&opt.mh_proj_diag_fb&&opt.mh_project_crop&&opt.mh_input_mapped&&!opt.skip_blocks.count(begin)&&ow%2==0&&oh%2==0){source=UpBody(source,skips[3-g],ow,oh,cs[g],begin);++begin;}
 else
 source=Up(source,skips[3-g],ow/2,oh/2,ow,oh,cs[g]*2,cs[g],Block(ups[g],"weights"),byte_up);skips[3-g].reset();for(U b=begin;b<=ends[g];b++){
 if(b==ups[g]+1&&SpEnabled(cs[g],true)){source=SpStage(source,ow,oh,cs[g],b,ends[g]-b);b=ends[g]-1;continue;}
 source=Body(source,ow,oh,cs[g],Shift(b),b,false,opt.mh_byte_stream&&(b>ups[g]||byte_up),opt.mh_byte_stream&&b<ends[g]);}}C32Result chain{};U c32_begin=66;
 if(C32UpBodyPath()){chain=C32UpBody(source,skips[0],W/2,H/2);c32_begin=67;source.reset();}
 else
 {if(c32_skip_byte)throw std::runtime_error("byte C32 skip needs the wave-owned up");source=Up(source,skips[0],W/4,H/4,W/2,H/2,64,32,"block66-weights.f32");}skips[0].reset();c32_skip_byte=false;for(U b=c32_begin;b<=69;b++){if(opt.skip_blocks.count(b)){if(!opt.raw_chain)throw std::runtime_error("C32 skip needs the raw chain");if(b==69)SkipChainFinish(chain,W/2,H/2,false);if(chain.main)source=chain.main;continue;}chain=opt.raw_chain?C32Chain(source,chain.raw?&chain:nullptr,W/2,H/2,Shift(b),Block(b,"ffn"),Block(b,"attention"),b==69,false):C32(source,W/2,H/2,Shift(b),Block(b,"ffn"),Block(b,"attention"));source=chain.main;if(source)Stage("block"+std::to_string(b),source);}chain={};
 C32Result post{};
 if(opt.post_merge_fold){U sx=(opt.post_shift&1)?4:0,sy=(opt.post_shift&2)?4:0,ww=W+2*sx,hh=H+2*sy,windows=ww*hh/64;if(opt.post_head_fused){auto out=New(size_t(W)*H*3);Run("c32_fused_ffn","c32_post_merge_head_half",windows,P(source),P(skip0),Weight("post70-scales.f32"),PackedC32Weight("post70-ffn.f32",false),PackedC32Weight("post70-attention.f32",true),P(color),Weight("post70-head.f32"),P(out),windows,W,H,sx,sy,.03125f);source.reset();skip0.reset();Stage("block70",out);return out;}
  auto raw=New(size_t(ww)*hh*16);Run("c32_fused_ffn","c32_post_merge_fused_half",windows,P(source),P(skip0),Weight("post70-scales.f32"),PackedC32Weight("post70-ffn.f32",false),PackedC32Weight("post70-attention.f32",true),P(raw),windows,W,H,sx,sy);post={Tensor{},Tensor{},raw,ww,hh,sx,sy};source.reset();skip0.reset();}
 else{auto merged=New(size_t(W)*H*32);Run(opt.fast_c32?"boundary_fast":"boundary",opt.pre_main8?"hip_post_merge_fast_skip8":opt.fast_c32?"hip_post_merge_fast":"hip_post_merge",size_t(W)*H*32,P(source),P(skip0),Weight("post70-scales.f32"),P(merged),W,H);source.reset();skip0.reset();post=C32(merged,W,H,opt.post_shift,"post70-ffn.f32","post70-attention.f32");}
 auto out=New(size_t(W)*H*3);Run(opt.fast_c32?"boundary_fast":"boundary",opt.half_c32?"hip_post_head_fast_half":opt.fast_c32?"hip_post_head_fast":"hip_post_head_exact",size_t(W)*H*3,P(post.raw),P(color),Weight("post70-head.f32"),P(out),W,H,post.workw,post.sx,post.sy,.03125f);Stage("block70",out);return out;}
 std::vector<float> Infer(const std::vector<float>&rgba,const std::vector<float>&noise,const std::vector<float>*history=nullptr){
  if(rgba.size()!=size_t(W)*H*4||noise.size()!=50331648||(history&&history->size()!=rgba.size()))throw std::runtime_error("network input sizes");
  if(opt.graph){Synchronize();ClearGraph();graph_warmed=false;}auto color=Upload(rgba.data(),rgba.size()*4),noisegpu=opt.fast_prefix?Tensor{}:Upload(noise.data(),noise.size()*4),hist=history?Upload(history->data(),history->size()*4):Tensor{};
  wall_timings.clear();auto out=RunGraph(color,noisegpu,hist);std::vector<float>result(size_t(W)*H*3);api.Check(api.hipStreamSynchronize(stream),"before readback");api.Check(api.hipMemcpy(result.data(),P(out),result.size()*4,2),"final readback");if(opt.wall_profile){for(auto&m:wall_timings)std::printf("serialized_kernel_ms %s %.6f calls=%u\n",m.first.c_str(),m.second.first,m.second.second);}
  if(opt.profile){bool valid=true;std::map<std::string,double>ms;for(auto&t:timings){float elapsed;api.Check(api.hipEventElapsedTime(&elapsed,t.begin,t.end),"event elapsed");if(!std::isfinite(elapsed)||elapsed<0)valid=false;ms[t.name]+=elapsed;api.hipEventDestroy(t.begin);api.hipEventDestroy(t.end);}timings.clear();double total=0;for(auto&m:ms){std::printf("kernel_ms %s %.6f\n",m.first.c_str(),m.second);total+=m.second;}if(valid)std::printf("kernel_ms TOTAL %.6f\n",total);else std::printf("PROFILE INVALID: negative/nonfinite HIP event intervals; discard this iteration\n");}return result;}
 // Device callers initialize once and use this stream for external fence waits/signals.
 void SetNoise(const std::vector<float>&noise){if(!opt.fast_prefix&&noise.size()!=50331648)throw std::runtime_error("noise size");api.Check(api.hipStreamSynchronize(stream),"set noise");ClearGraph();graph_warmed=false;device_noise=opt.fast_prefix?Tensor{}:Upload(noise.data(),noise.size()*4,true);}
 bool GraphEnabled()const{return opt.graph;}
 bool WaveOwnedActive()const{return wave_owned_active;}
 bool SwinRunActive()const{return SwinRunCompatible(opt)&&!sp_disabled&&(!sp_error_host||!sp_error_host[0].load(std::memory_order_acquire));}
 bool C512M32Active()const{return c512_m32_active;}
 unsigned VitStreamActive()const{return vit_stream_active;}
 bool VitProjN64Active()const{return vit_proj_n64_active;}
 Handle Stream()const{return stream;}
 Api& Runtime(){return api;}
 void Synchronize(){api.Check(api.hipStreamSynchronize(stream),"network completion");}
 void Enqueue(void*rgba,void*history,void*rgb_output,U seed){if(adaptive_prev_history!=history||adaptive_prev_seed!=seed||adaptive_prev_input!=rgba){adaptive_dirty=true;adaptive_prev_history=history;adaptive_prev_seed=seed;adaptive_prev_input=rgba;}
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
  EnqueueRaw(rgba,history,rgb_output,seed);
 }
 private:
 void EnqueueRaw(void*rgba,void*history,void*rgb_output,U seed){
  opt.seed=seed;auto color=std::make_shared<Allocation>(api,rgba,size_t(W)*H*16);auto hist=history?std::make_shared<Allocation>(api,history,size_t(W)*H*16):Tensor{};
  auto out=RunGraph(color,device_noise,hist);api.Check(api.hipMemcpyAsync(rgb_output,P(out),size_t(W)*H*12,3,stream),"device output copy");
 }
};
}
