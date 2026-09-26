#pragma once
#include "hip_reference_network.h"
#include <string>

/* First-version production HIP flags (HIP_FAST=1, graph off, skip 42,43,46). Instance, not getenv. */
inline hip_reference::Options LmxxfProductionOptions(unsigned processing_w, unsigned processing_h,
                                                     const std::string &modules, const std::string &assets)
{
    hip_reference::Options o;
    o.width = processing_w;
    o.height = processing_h;
    o.post_shift = 3;
    o.fast_vit = true;
    o.wmma = o.wave = o.tiled = o.pooled = true;
    o.graph = false;
    o.skip_blocks = hip_reference::ParseSkipBlocks("42,43,46");
    o.modules = modules;
    o.assets = assets;
    o.fast_c32 = o.fused_c32 = o.fused_ffn = o.fast_mh = o.fused_mh = o.mh_wave = o.fast_deep =
        o.fast_prefix = o.packed_weights = o.packed_c32 = o.fp8_normalized = o.fp8_ffn = o.fp8_av =
            o.fp8_deep = o.fp8_middle = o.half_c32 = o.crop_c32 = o.fused_qkv_norm = o.fused_mh_ffn =
                o.tiled_mh_ffn = o.mapped_c32 = o.vit_blocked = o.vit_contract_blocked = true;
    o.tiled_ffn_min_c = 256;
    o.vit_weight_mask = 1;
    o.vit_pack_input = true;
    o.elide_identity_shift = true;
    o.raw_chain = true;
    o.pre_main8 = true;
    o.post_merge_fold = true;
    o.fused_ffn_project = true;
    o.split_ffn_fused = true;
    o.split_mix_blocked = true;
    o.split_project_blocked = true;
    o.vit_qkv_blocked = true;
    o.vit_qkv_fused = true;
    o.mh_project_crop = true;
    o.mh_input_mapped = true;
    o.prefix_fused = true;
    o.direct_prefix_input = true;
    o.grouped_mh_contract = true;
    o.ffn_qkv = true;
    o.ffn_qkv_max_c = 256;
    o.vit_attn_fused = true;
    o.vit_qkv_fp8 = true;
    o.vit_expand_frag = true;
    o.split_mix_h16w = true;
    o.pool_project_h16w = true;
    o.decoder_h16w = true;
    o.c512_qkv_frag = true;
    o.c512_proj_frag = true;
    o.c512_proj_tiles = true;
    o.mh_proj_diag = true;
    o.post_head_fused = true;
    o.c32_finish_fused = true;
    o.down_crop_fused = true;
    o.pool32_h16w = true;
    o.pool_project_group = true;
    o.vit_proj_frag = true;
    o.vit_qkv_frag = true;
    o.vit_contract_frag = true;
    o.prefix_inline = true;
    // Byte-packed multihead / decoder paths. The addon route already turns these on through
    // DLSS5_HIP_* in scripts/hip-game-flags.txt and scripts/hip-re9-flags.txt.
    o.mh_feature_byte = o.mh_proj_diag_fb = o.mh_byte_stream = o.decoder_byte = o.mh_ffn_frag256 = true;
    return o;
}
