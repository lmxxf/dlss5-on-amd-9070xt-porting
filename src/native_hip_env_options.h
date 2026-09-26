#pragma once
// DLSS5_HIP_* environment overrides applied on top of the fast production Options. Moved verbatim out of
// NativeHipNetwork::Create (2026-09-26) so the standalone LmxxfNrRuntime.dll (RE9 package) reads the same keys with
// the same parsing and statement order as the regular / Magpie add-on. `fast` is FastPrefixFromEnvironment() there.
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include "../Development/HIP/hip_reference_network.h"

inline void NativeApplyHipEnvironment(hip_reference::Options&o,bool fast){
  if(const char*v=std::getenv("DLSS5_HIP_GRAPH")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("DLSS5_HIP_GRAPH must be 0 or 1");o.graph=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_PREFIX_FUSED")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("prefix fused must be 0 or 1");o.prefix_fused=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_DIRECT_INPUT")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("direct input must be 0 or 1");o.direct_prefix_input=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_GROUPED_CONTRACT")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("grouped contract flag");o.grouped_mh_contract=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_ATTN_FUSED")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT attention fused flag");o.vit_attn_fused=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_EXPAND_FRAG")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT expand fragment flag");o.vit_expand_frag=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_EXPAND_M4")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT expand M4 flag");o.vit_expand_m4=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_WAVE_OWNED")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("HIP wave-owned flag must be 0 or 1");o.wave_owned=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_C512_M32")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("HIP C512 M32 flag must be 0 or 1");o.c512_m32=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_PROJ_N64")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("HIP ViT projection N64 flag must be 0 or 1");o.vit_proj_n64=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_PDL")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("HIP PDL flag");o.pdl=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_MH_BYTE_STREAM")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("MH byte stream flag");o.mh_byte_stream=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_BYTE_STREAM")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT byte stream flag");o.vit_byte_stream=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_HALF_STREAM")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT half stream flag");o.vit_half_stream=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_QKV_N4")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT QKV N4 flag");o.vit_qkv_n4=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_QKV_FP8")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT byte QKV flag");o.vit_qkv_fp8=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_QKV_FUSED")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT QKV fused flag");o.vit_qkv_fused=!strcmp(v,"1");}
  o.ffn_qkv=fast&&o.grouped_mh_contract;
  if(const char*v=std::getenv("DLSS5_HIP_FFN_QKV")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("FFN/QKV flag");o.ffn_qkv=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_DUP_PREFIX"))o.dup_prefix=v;
  if(const char*v=std::getenv("DLSS5_HIP_DUP_COUNT")){o.dup_count=unsigned(strtoul(v,nullptr,10));if(o.dup_count<1||o.dup_count>8)throw std::runtime_error("dup count 1..8");}
  if(const char*v=std::getenv("DLSS5_HIP_MH_FFN_FRAG256")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("C256 FFN fragment flag");o.mh_ffn_frag256=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_MH_FFN_FRAG")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("MH FFN frag flag");o.mh_ffn_frag=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_CONTRACT_FRAG")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT contract frag flag");o.vit_contract_frag=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_PROJ_FRAG")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT projection frag flag");o.vit_proj_frag=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_QKV_FRAG")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT QKV frag flag");o.vit_qkv_frag=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_MH_WINDOW_FUSED")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("MH window fused flag");o.mh_window_fused=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_MH_PROJ_DIAG_FB")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("MH projection diagonal (byte feature) flag");o.mh_proj_diag_fb=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_SPLIT_K")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT split-K flag");o.vit_split_k=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_SPARSE_WEIGHTS")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("sparse weights flag");o.sparse_weights=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_PREFIX_INLINE")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("prefix inline flag");o.prefix_inline=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_POOL_PROJECT_GROUP")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("pool project group flag");o.pool_project_group=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_TILED_FFN_SMALL")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("tiled FFN small flag");o.tiled_ffn_small=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_DOWN_CROP_FUSED")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("down crop fused flag");o.down_crop_fused=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_POOL32_H16W")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("pool32 h16w flag");o.pool32_h16w=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_C32_FINISH_FUSED")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("C32 finish fused flag");o.c32_finish_fused=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_POST_HEAD_FUSED")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("post head fused flag");o.post_head_fused=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_MH_PROJ_DIAG")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("MH projection diagonal flag");o.mh_proj_diag=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_MH_FEATURE_BYTE")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("MH feature byte flag");o.mh_feature_byte=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_C512_PROJ_TILES")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("C512 tiled projection flag");o.c512_proj_tiles=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_C512_PROJ_FRAG")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("C512 fragment projection flag");o.c512_proj_frag=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_C512_QKV_FRAG")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("C512 fragment QKV flag");o.c512_qkv_frag=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_MH_ATTN_W16")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("MH attention w16 flag");o.mh_attn_w16=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_DECODER_H16W")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("decoder half weight flag");o.decoder_h16w=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_POOL_PROJECT_H16W")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("pool project half weight flag");o.pool_project_h16w=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_SPLIT_MIX_H16W")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("split mix half weight flag");o.split_mix_h16w=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_SPLIT_MIX_FUSED")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("split mix fused flag");o.split_mix_fused=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_FFN_FUSED")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT fused FFN flag");o.vit_ffn_fused=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_INPUT_TILED")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT tiled input flag");o.vit_input_tiled=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_VIT_EXPAND_M2")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("ViT expand M2 flag");o.vit_expand_m2=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_QKV_WAVE_C512")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("C512 wave QKV flag");o.qkv_norm_wave_c512=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_POOL_PROJECT_FUSED")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("pool project fused flag");o.pool_project_fused=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_FFN_QKV_BN")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("FFN/QKV batched norm flag");o.ffn_qkv_batched_norm=!strcmp(v,"1");}
  if(const char*v=std::getenv("DLSS5_HIP_FFN_QKV_MAX_C")){char*end=nullptr;auto c=strtoul(v,&end,10);if(*end||(c!=64&&c!=128&&c!=256))throw std::runtime_error("FFN/QKV channel limit");o.ffn_qkv_max_c=unsigned(c);}
  if(const char*v=std::getenv("DLSS5_HIP_DECODER_BYTE")){if(strcmp(v,"0")&&strcmp(v,"1"))throw std::runtime_error("decoder byte flag");o.decoder_byte=!strcmp(v,"1");}
}
