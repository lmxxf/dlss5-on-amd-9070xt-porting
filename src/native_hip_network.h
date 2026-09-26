#pragma once
#include "../Development/HIP/hip_d3d12_bridge.h"
#include "native_network_geometry.h"
#include "native_lab_paths.h"
#include "native_hip_env_options.h"
// Experimental compile-time backend. Ordinary D3D12 codec and temporal passes stay in NativeGameFrame.
class NativeHipNetwork {
 hip_reference::D3D12Bridge bridge;ID3D12Resource*color{};ID3D12Resource*history{};
 static std::string Utf8(const std::wstring&s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);if(!n&&!s.empty())throw std::runtime_error("HIP path encoding");std::string r(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;}
public:
 NativeHipNetwork()=default;NativeHipNetwork(const NativeHipNetwork&)=delete;
 ~NativeHipNetwork(){if(!bridge.WaitForSubmittedWork())return;if(color)color->Release();if(history)history->Release();}
 void Create(ID3D12CommandQueue*q,ID3D12Resource*rgb,const std::vector<float>&noise,const std::wstring&directory,ID3D12Resource*temporal,UINT post_shift){
  if(color||!rgb)throw std::runtime_error("HIP network initialization");auto g=NativeCurrentNetworkGeometry();hip_reference::Options o;o.width=g.processing_width;o.height=g.processing_height;o.post_shift=post_shift;o.fast_vit=true;o.wmma=o.wave=o.tiled=o.pooled=true;o.assets=Utf8(directory);if(const char*skip=std::getenv("DLSS5_SKIP_BLOCKS"))o.skip_blocks=hip_reference::ParseSkipBlocks(skip);
  const bool fast=hip_reference::FastPrefixFromEnvironment();
  if(fast)o.fast_c32=o.fused_c32=o.fused_ffn=o.fast_mh=o.fused_mh=o.mh_wave=o.fast_deep=o.fast_prefix=o.packed_weights=o.packed_c32=o.fp8_normalized=o.fp8_ffn=o.fp8_av=o.fp8_deep=o.fp8_middle=o.half_c32=o.crop_c32=o.fused_qkv_norm=o.fused_mh_ffn=o.tiled_mh_ffn=o.mapped_c32=o.vit_blocked=o.vit_contract_blocked=true;
  o.tiled_ffn_min_c=256;
  if(fast){o.vit_weight_mask=1;o.vit_pack_input=true;o.elide_identity_shift=true;o.raw_chain=true;o.pre_main8=true;o.post_merge_fold=true;o.fused_ffn_project=true;o.split_ffn_fused=true;o.split_mix_blocked=true;o.split_project_blocked=true;o.vit_qkv_blocked=true;o.vit_qkv_fused=true;o.mh_project_crop=true;o.mh_input_mapped=true;o.prefix_fused=true;o.direct_prefix_input=true;o.grouped_mh_contract=true;o.ffn_qkv=true;o.ffn_qkv_max_c=256;o.vit_attn_fused=true;o.vit_qkv_fp8=true;o.vit_expand_frag=true;/* 2026-09-16: fused ViT attention, byte ViT QKV, fragment-native expand weights (-0.23ms, bit-exact) */o.split_mix_h16w=true;/* 2026-09-16 21:50: C512 mix weights prepacked half (-0.12ms, bit-exact) */o.pool_project_h16w=true;/* 2026-09-16 22:20: downsample projection weights prepacked half/E4M3 (-0.21ms, bit-exact) */o.decoder_h16w=true;/* 2026-09-16 22:40: decoder up-projection weights prepacked half (-0.07ms, bit-exact) */o.c512_qkv_frag=true;/* 2026-09-16 20:45: C512 QKV from projection E4M3 tiles + fragment weights, no LDS (-0.37ms, bit-exact) */o.c512_proj_frag=true;o.c512_proj_tiles=true;o.mh_proj_diag=true;o.post_head_fused=true;o.c32_finish_fused=true;o.down_crop_fused=true;o.pool32_h16w=true;o.pool_project_group=true;o.vit_proj_frag=true;o.vit_qkv_frag=true;o.vit_contract_frag=true;/* 2026-09-16 21:55: C512 attention projection + split projection in the same shape (-0.31ms together, bit-exact) */o.prefix_inline=true;/* 2026-09-17 02:25: null on 09-17 00:21 while the block-0 kernel was issue-bound; -0.27ms once its F()/load serialization was fixed, bit-exact */}
  NativeApplyHipEnvironment(o,fast);
  const wchar_t*modules=_wgetenv(L"DLSS5_HIP_MODULES");o.modules=modules&&*modules?Utf8(modules):Utf8(directory+L"\\HIP");try{bridge.Create(q,o,noise);}catch(...){LogDevice();throw;}LogDevice();
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-hip.txt").c_str(),L"ab")){fprintf(f,"pid=%lu runtime=7 fast=%u packed_weights=%u packed_c32=%u fp8_normalized=%u fp8_ffn=%u fp8_av=%u fp8_deep=%u fp8_middle=%u half_c32=%u crop_c32=%u fused_qkv_norm=%u fused_mh_ffn=%u tiled_mh_ffn=%u tiled_ffn_min_c=%u mapped_c32=%u vit_blocked=%u vit_contract_blocked=%u vit_split_k=%u vit_weight_mask=%u vit_pack_input=%u elide_identity_shift=%u vit_qkv_fused=%u ffn_qkv=%u ffn_qkv_max_c=%u grouped_mh_contract=%u direct_prefix_input=%u prefix_fused=%u mh_input_mapped=%u mh_project_crop=%u vit_qkv_blocked=%u split_project_blocked=%u split_mix_blocked=%u split_ffn_fused=%u fused_ffn_project=%u post_merge_fold=%u pre_main8=%u raw_chain=%u graph=%u sparse_weights=%u processing=%ux%u modules=%s\n",GetCurrentProcessId(),unsigned(fast),unsigned(o.packed_weights),unsigned(o.packed_c32),unsigned(o.fp8_normalized),unsigned(o.fp8_ffn),unsigned(o.fp8_av),unsigned(o.fp8_deep),unsigned(o.fp8_middle),unsigned(o.half_c32),unsigned(o.crop_c32),unsigned(o.fused_qkv_norm),unsigned(o.fused_mh_ffn),unsigned(o.tiled_mh_ffn),o.tiled_ffn_min_c,unsigned(o.mapped_c32),unsigned(o.vit_blocked),unsigned(o.vit_contract_blocked),unsigned(o.vit_split_k),o.vit_weight_mask,unsigned(o.vit_pack_input),unsigned(o.elide_identity_shift),unsigned(o.vit_qkv_fused),unsigned(o.ffn_qkv),o.ffn_qkv_max_c,unsigned(o.grouped_mh_contract),unsigned(o.direct_prefix_input),unsigned(o.prefix_fused),unsigned(o.mh_input_mapped),unsigned(o.mh_project_crop),unsigned(o.vit_qkv_blocked),unsigned(o.split_project_blocked),unsigned(o.split_mix_blocked),unsigned(o.split_ffn_fused),unsigned(o.fused_ffn_project),unsigned(o.post_merge_fold),unsigned(o.pre_main8),unsigned(o.raw_chain),unsigned(o.graph),unsigned(o.sparse_weights),o.width,o.height,o.modules.c_str());fprintf(f,"pid=%lu wave_owned_requested=%u wave_owned_active=%u\n",GetCurrentProcessId(),unsigned(o.wave_owned),unsigned(hip_reference::WaveOwnedCompatible(o)));if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-hip.txt").c_str(),L"ab")){fprintf(f,"pid=%lu c512_m32_requested=%u c512_m32_active=%u\n",GetCurrentProcessId(),unsigned(o.c512_m32),unsigned(hip_reference::C512M32Compatible(o)));fprintf(f,"pid=%lu vit_proj_n64_requested=%u vit_proj_n64_active=%u\n",GetCurrentProcessId(),unsigned(o.vit_proj_n64),unsigned(hip_reference::VitProjN64Compatible(o)));fclose(f);}fprintf(f,"pid=%lu hip_device=%d\n",GetCurrentProcessId(),bridge.hip_device);fclose(f);}
color=rgb;color->AddRef();history=temporal;if(history)history->AddRef();
 }
 // The host submits both sides on the same queue. See D3D12Bridge's stage contract.
 void RecordInputCopy(ID3D12GraphicsCommandList*c,bool use_history=false){if(use_history&&!history)throw std::runtime_error("HIP history not bound");bridge.RecordInputCopy(c,color,use_history?history:nullptr);}
 void EnqueueAfterProducer(ID3D12CommandQueue*q,UINT seed,bool use_history=false){if(use_history&&!history)throw std::runtime_error("HIP history not bound");bridge.EnqueueAfterProducer(q,seed,use_history);}
 void RecordOutputReadable(ID3D12GraphicsCommandList*c){bridge.RecordOutputReadable(c);}
 void NotifyOutputSubmitted(ID3D12CommandQueue*q){bridge.NotifyOutputSubmitted(q);FrameSubmitted();}
 template<class Submission>void Run(Submission&submit,UINT seed,bool use_history=false){if(use_history&&!history)throw std::runtime_error("HIP history not bound");bridge.Run(submit,color,use_history?history:nullptr,seed);FrameSubmitted();}
private:
 void FrameSubmitted(){
  if(++frames==3){if(const char*v=std::getenv("DLSS5_HIP_MEMORY");v&&!strcmp(v,"1"))if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-hip.txt").c_str(),L"ab")){bridge.MemoryReport(f);fclose(f);}}
 }
public:
 void LogDevice(){if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-hip-device.txt").c_str(),L"ab")){fprintf(f,"pid=%lu device=%s arch=%s modules=%s match=%s runtime=%d\n",GetCurrentProcessId(),bridge.adapter_name.c_str(),bridge.architecture.c_str(),bridge.module_directory.c_str(),bridge.device_match.c_str(),bridge.runtime_version);fclose(f);}}
 unsigned frames{};
 ID3D12Resource*Output()const{return bridge.Output();}
#ifdef DLSS5_BENCH_BRIDGE_ISOLATE
 hip_reference::Network& DiagnosticNetwork(){return bridge.DiagnosticNetwork();}
#endif
};
