# Builds every shader of the fast chain into $Folder (flattened from the 85 nested run_*_network.ps1 runners in
# Development/, in the same order, so the compiled set and the DLSS5_* runtime flags are identical to the game build)
# and then runs the bench executable (native-network70-temporal.exe, see build-bench.sh). Needs the SM6.10 preview dxc.
param([Parameter(Mandatory=$true)][string]$Folder,[string]$DxcRoot='D:\DLSSNR-Lab\matrix-probe\dxc-preview')
$ErrorActionPreference='Stop'
Set-Location $Folder
$Dxc=Join-Path $DxcRoot 'bin\x64\dxc.exe'
$Inc=Join-Path $DxcRoot 'inc\hlsl'

# ---- run_vit_tiled_network.ps1
# FAST PATH: ViT expand/reduce weights and the hidden layer as 512-byte contiguous tiles (no power-of-two row strides). Pure data movement, bit-exact.
$env:DLSS5_BUILD_VIT_TILED='1'
$env:DLSS5_VIT_TILED='1'
# ---- run_fused_ffn_proj0_network.ps1
# FAST PATH: multihead Swin blocks' FFN (expand+contract) and FFN output projection in one dispatch (26 blocks; kernels compiled in run_fp8_activations). Exact against the precise chain.
# Not in the game build (no measurable gain, see CURRENT-STATE 09-10 10:40): opt in with DLSS5_FUSED_FFN_PROJ0=1 before running.
if($env:DLSS5_FUSED_FFN_PROJ0 -eq '1'){$env:DLSS5_BUILD_FUSED_FFN_PROJ0='1'}
# ---- run_c32_fused_ffn_network.ps1
# FAST PATH: the C32 FFN runs inside the fast4 attention dispatch (pre, blocks 1-4, 67-69, post); the ffn scratch becomes the second raw buffer. Exact.
$env:DLSS5_BUILD_C32_FUSED_FFN='1'
$env:DLSS5_C32_FUSED_FFN='1'
# ---- run_c32_precise_chain_network.ps1
# Build-only reference: no FMA contraction in the C32 scalar tails (context dependent on this driver); the fused-FFN kernel is bit-exact against it.
$env:DLSS5_BUILD_C32_PRECISE_CHAIN='1'
# ---- run_c32_epilogue_network.ps1
# FAST PATH: the C32 finish (main8 + pooled down) and the post70 rgb head run in the attention epilogue; pre/post/blocks 4,69 no longer write raw. Exact.
$env:DLSS5_BUILD_C32_EPILOGUE='1'
$env:DLSS5_C32_EPILOGUE='1'
# ---- run_c32_half_stream_network.ps1
# FAST PATH: C32 ffn/raw scratch as f16 (every value on them is H()-rounded, so exact); halves the C32 blocks' memory traffic.
$env:DLSS5_BUILD_C32_HALF_STREAM='1'
$env:DLSS5_C32_HALF_STREAM='1'
# ---- run_post70_low_main8_network.ps1
# FAST PATH: block 69 finish writes E4M3 main8 (no f32 main, no crop); post70's merge fold reads the bytes as the low-res input (mode 9). Exact.
$env:DLSS5_POST70_LOW_RAW='2'
# ---- run_c32_skip8_network.ps1
# FAST PATH: block 4 finish writes E4M3 main8 (no f32 main, no crop); the block 66 upsample projection reads it as the skip residual. Exact.
$env:DLSS5_BUILD_C32_SKIP8='1'
$env:DLSS5_C32_SKIP8='1'
# ---- run_preblock_main8_network.ps1
# FAST PATH: preblock finish writes Main as E4M3 bytes; post70 merge fold reads them (mode 7). Exact (Main was F()-quantized).
$env:DLSS5_PREBLOCK_MAIN8='1'
# ---- run_vit_qkv_fused_network.ps1
# FAST PATH: ViT QKV projection + per-head normalize + E4M3 store in one kernel; the FP8 attention reads it directly
# (drops the scalar normalize dispatch and the pack8 dispatch per layer).
& $Dxc -I $Inc -T cs_6_10 -E project_fused -HV 2021 -enable-16bit-types -O3 -D NATIVE_FAST_ACCUMULATE=1 -D NATIVE_PACKED_INPUT=1 -D BLOCK_M=4 -D NATIVE_QKV_FUSED=1 native_wave_vit_qkv.hlsl -Fo native_wave_vit_qkv_fused_m4.cso
if($LASTEXITCODE -ne 0){throw 'ViT fused QKV compilation failed'}
$env:DLSS5_VIT_QKV_FUSED='1'
# ---- run_batch_submits2_network.ps1
# FAST PATH: batch level 2 -- ViT 4 layers per command list, decoder in two lists (~6 lists/frame). CPU-side only.
$env:DLSS5_BATCH_SUBMITS='2'
# ---- run_c32_attn_fast4_network.ps1
# FAST PATH: C32 attention one group = two windows (all 8 waves do QKV, per-wave softmax, 2 group syncs).
$env:DLSS5_BUILD_C32_ATTN_FAST4='1'
$env:DLSS5_C32_ATTN_FAST4='1'
# ---- run_batch_submits_network.ps1
# FAST PATH: fewer command lists per frame (ViT per layer, decoder per 3 stages). CPU-side only.
if(-not $env:DLSS5_BATCH_SUBMITS){$env:DLSS5_BATCH_SUBMITS='1'}
# ---- run_inline_prefix_network.ps1
# FAST PATH: pre-block prefix mix computed inside the FFN kernel (mapping mode 5): no prefix dispatch, no raw round trip.
$env:DLSS5_INLINE_PREFIX='1'
# ---- run_c32_attn_fast3_network.ps1
# FAST PATH: C32 QKV+normalize fused into the attention dispatch (LDS Q/K/V, no aux round trip).
$env:DLSS5_BUILD_C32_ATTN_FAST3='1'
$env:DLSS5_C32_ATTN_FAST3='1'
# ---- run_post70_merge_fold_network.ps1
# FAST PATH: post70 merge folded into the FFN gather (mapping mode 4: low-res main + skip + coefficients), no merged buffer pass.
$env:DLSS5_POST70_MERGE_FOLD='1'
# ---- run_vit_attn_fp8_network.ps1
# FAST PATH: ViT attention on E4M3 Q/K/V with register exp, hardware-cast P, MMA denominators, FP8 x FP8 PV.
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 native_wave_vit_attention_fp8.hlsl -Fo native_wave_vit_attention_fp8.cso
if($LASTEXITCODE -ne 0){throw 'ViT FP8 attention compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E pack8 -HV 2021 -enable-16bit-types -O3 native_wave_vit_attention_fp8.hlsl -Fo native_wave_vit_attention_pack8.cso
if($LASTEXITCODE -ne 0){throw 'ViT FP8 attention pack compilation failed'}
$env:DLSS5_VIT_ATTN_FP8='1'
# ---- run_attn_fast2_network.ps1
# FAST PATH: multihead (C64/128/256/512) attention fast2 - register exp, MMA row sums, hardware E4M3 P and output tile. Build-time only.
$env:DLSS5_BUILD_ATTN_FAST2='1'
# ---- run_wave_c32_ds_network.ps1
# FAST PATH: wave-matrix C32 downsample projection (ds4) replacing the scalar cs_5_1 kernel.
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 native_wave_c32_ds.hlsl -Fo native_wave_c32_ds.cso
if($LASTEXITCODE -ne 0){throw 'wave C32 DS compilation failed'}
$env:DLSS5_WAVE_C32_DS='1'
# ---- run_c32_chain_raw_network.ps1
# FAST PATH: chained C32 blocks (encoder 1-4, tail 66-69) read the previous block's raw tiles (mapping mode 3) so the
# predecessor's finish stage is skipped; residual = F(input)*scale via E4M3 diagonal MMAs. Needs C32 FFN fast3.
$env:DLSS5_C32_CHAIN_RAW='1'
# ---- run_post70_direct_network.ps1
# FAST PATH: post70 without pack/crop/finish (FFN mapped from the merged raster, rgb head indexes the raw tiles). Runtime-compiled native_post70.hlsl.
$env:DLSS5_POST70_DIRECT='1'
# ---- run_c32_attn_fast2_network.ps1
# FAST PATH: C32 attention fast2 (register exp, MMA row sums, hardware E4M3 P and attention output, FP8 x FP8 projection).
# Host DLSS5_C32_ATTN_FAST2 packs the E4M3 projection copy; kernel built with NATIVE_C32_ATTN_FAST2 via DLSS5_BUILD_C32_ATTN_FAST2.
$env:DLSS5_BUILD_C32_ATTN_FAST2='1'
$env:DLSS5_C32_ATTN_FAST2='1'
# ---- run_c32_ffn_fast3_network.ps1
# FAST PATH 3: C32 FFN input tile as accumulator loads + hardware E4M3 cast (no scalar staging), FP8 x FP8 expand.
# Host DLSS5_C32_FFN_FAST3 packs the E4M3 expand copy; kernel built with NATIVE_C32_FFN_FAST3 via DLSS5_BUILD_C32_FFN_FAST3.
$env:DLSS5_BUILD_C32_FFN_FAST3='1'
$env:DLSS5_C32_FFN_FAST3='1'
# ---- run_tiled_weights_network.ps1
# FAST PATH: multihead projection + QKV weights tile-contiguous (host DLSS5_TILED_WEIGHTS + kernel NATIVE_TILED_WEIGHTS via DLSS5_BUILD_TILED_PQ).
$env:DLSS5_BUILD_TILED_PQ='1'
$env:DLSS5_TILED_WEIGHTS='1'
# ---- run_ffn_tiled_weights_network.ps1
# FAST PATH: fused FFN weights repacked so every B tile is one contiguous 512-byte block (host DLSS5_FFN_TILED_WEIGHTS + kernel NATIVE_TILED_WEIGHTS must agree).
$env:DLSS5_BUILD_TILED_WEIGHTS='1'
$env:DLSS5_FFN_TILED_WEIGHTS='1'
# ---- run_matrix_residual_network.ps1
# FAST PATH: multihead projection residual (feature*scale) as three E4M3 MMAs instead of a per-element decode/scale/H pass.
# Build-time only for the f8in non-mapfeature variants (same cso names); host packs the diagonal matrices unconditionally.
$env:DLSS5_BUILD_MATRIX_RESIDUAL='1'
# ---- run_direct_cast_network.ps1
# FAST PATH: multihead projections with E4M3 output skip the per-element F(H()) pass; the Cast<F8> store is the quantizer.
# Build-time only (same cso names); run_fp8_stream_network.ps1 reads the variable when it compiles the f8out variants.
$env:DLSS5_BUILD_DIRECT_CAST='1'
# ---- run_split_direct_attention_network.ps1
# FAST PATH: C512 attention through the direct path (FP8 pack, fused QKV+normalize, FP8 Q/K/V attention with FP8 output, FP8-input projection).
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D MATRIX_CHANNELS=512 -D PACKED_INPUT=1 -D NATIVE_FP8_OPERANDS=1 -D MAPPED_INPUT=0 native_matrix_pack.hlsl -Fo native_matrix_pack_fp8_c512.cso
if($LASTEXITCODE -ne 0){throw 'FP8 pack C512 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D MATRIX_CHANNELS=512 -D NATIVE_FP8_QKV_OUT=1 -D "NATIVE_QKV_FAST2=$(if($env:DLSS5_BUILD_QKV_FAST2 -eq '1'){1}else{0})" native_wave_qkv_normalize.hlsl -Fo native_wave_qkv_normalize_fp8qkv_c512.cso
if($LASTEXITCODE -ne 0){throw 'fused QKV C512 compilation failed'}
& $Dxc -I $Inc -I $Folder -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D CHANNELS=512 -D DIRECT_NORMALIZE=0 -D NATIVE_FAST_ACCUMULATE=1 -D NATIVE_HW_H=1 -D NATIVE_FAST_ATTENTION=1 -D NATIVE_FP8_OUTPUT=1 -D NATIVE_FP8_QKV=1 -D "NATIVE_ATTN_FAST2=$(if($env:DLSS5_BUILD_ATTN_FAST2 -eq '1'){1}else{0})" native_wave_attention_direct.hlsl -Fo native_wave_attention_direct_fp8qkv_fp8act_c512.cso
if($LASTEXITCODE -ne 0){throw 'direct attention C512 compilation failed'}
foreach($Raw in 0,1){
 $Name=if($Raw){'native_wave_project_raw_fp8act_c512.cso'}else{'native_wave_project_fp8act_c512.cso'}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D MATRIX_CHANNELS=512 -D "RAW=$Raw" -D NATIVE_FAST_ACCUMULATE=1 -D NATIVE_HW_H=1 -D NATIVE_FP8_OPERANDS=1 -D NATIVE_FP8_INPUT=1 native_wave_project.hlsl -Fo $Name
 if($LASTEXITCODE -ne 0){throw "FP8-input projection C512 raw=$Raw compilation failed"}
}
$env:DLSS5_SPLIT_DIRECT_ATTENTION='1'
# ---- run_fp8_stream_network.ps1
# FAST PATH: E4M3 residual stream in the multihead blocks (result[0] and chained raster outputs as bytes; no QKV pack; byte-gather shift pack).
$Common=@('-I',$Inc,'-T','cs_6_10','-E','main','-HV','2021','-enable-16bit-types','-O3','-D','NATIVE_FAST_ACCUMULATE=1','-D','NATIVE_HW_H=1','-D','NATIVE_FP8_OPERANDS=1','-D','NATIVE_FP8_INPUT=1','-D',"NATIVE_DIRECT_CAST=$(if($env:DLSS5_BUILD_DIRECT_CAST -eq '1'){1}else{0})",'-D',"NATIVE_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_TILED_PQ -eq '1'){1}else{0})")
foreach($Channels in 256,128,64){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D PACKED_INPUT=1 -D NATIVE_FP8_OPERANDS=1 -D MAPPED_INPUT=1 -D NATIVE_FP8_SOURCE=1 native_matrix_pack.hlsl -Fo "native_matrix_pack_fp8_mapped_src8$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "FP8-source mapped pack C$Channels compilation failed"}
 foreach($Variant in @(
   @('native_wave_project_mapfeature_fp8act_f8out',@('-D','MAP_FEATURE=1','-D','NATIVE_FP8_STORE=1')),
   @('native_wave_project_mapfeature_fp8act_f8in_f8out',@('-D','MAP_FEATURE=1','-D','NATIVE_FP8_FEATURE=1','-D','NATIVE_FP8_STORE=1')),
   @('native_wave_project_mapoutput_fp8act_f8in_f8out',@('-D','RAW=0','-D','MAP_OUTPUT=1','-D','NATIVE_FP8_FEATURE=1','-D','NATIVE_FP8_STORE=1')),
   @('native_wave_project_mapoutput_fp8act_f8in',@('-D','RAW=0','-D','MAP_OUTPUT=1','-D','NATIVE_FP8_FEATURE=1')),
   @('native_wave_project_raw_mapoutput_fp8act_f8in',@('-D','RAW=1','-D','MAP_OUTPUT=1','-D','NATIVE_FP8_FEATURE=1')))){
  $Extra=$Variant[1]
  if($env:DLSS5_BUILD_MATRIX_RESIDUAL -eq '1' -and $Variant[0] -notlike '*mapfeature*'){$Extra=$Extra+@('-D','NATIVE_MATRIX_RESIDUAL=1')}
  & $Dxc @Common -D "MATRIX_CHANNELS=$Channels" @Extra native_wave_project.hlsl -Fo "$($Variant[0])$Suffix.cso"
  if($LASTEXITCODE -ne 0){throw "Projection $($Variant[0]) C$Channels compilation failed"}
 }
}
$env:DLSS5_FP8_STREAM='1'
# ---- run_c32_mapped_input_network.ps1
# FAST PATH: C32 stages read their input through a mapping in the FFN kernel (no pack pass; crop only where consumed).
if(-not $env:DLSS5_BUILD_C32_MAPPED_INPUT){$env:DLSS5_BUILD_C32_MAPPED_INPUT='1'}
if(-not $env:DLSS5_C32_MAPPED_INPUT){$env:DLSS5_C32_MAPPED_INPUT='1'}
# ---- run_vit_block_m_network.ps1
# FAST PATH: ViT GEMMs with four token tiles per wave (weight traffic / 4); shaders compiled by run_vit_packed_input_network.ps1.
$env:DLSS5_VIT_BLOCK_M='4'
# ---- run_vit_packed_input_network.ps1
$BlockN=4
foreach($Name in 'DLSS5_FAST_ACCUMULATE','DLSS5_FAST_EPILOGUE','DLSS5_FP8_OPERANDS','DLSS5_FP8_HIDDEN','DLSS5_FP8_LDS'){Set-Item -Path "Env:$Name" -Value '1'}
# FAST PATH: ViT operand copies (pack8/pack16) and packed-input kernels (expand, qkv, projection reduce incl. split-K).
& $Dxc -I $Inc -T cs_6_10 -E pack8 -HV 2021 -enable-16bit-types -O3 native_vit_pack.hlsl -Fo native_vit_pack8.cso
if($LASTEXITCODE -ne 0){throw 'pack8 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E pack16 -HV 2021 -enable-16bit-types -O3 native_vit_pack.hlsl -Fo native_vit_pack16.cso
if($LASTEXITCODE -ne 0){throw 'pack16 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E expand -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=1 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_expand_packed.cso
& $Dxc -I $Inc -T cs_6_10 -E expand -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=1 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 -D BLOCK_M=4 -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_expand_packed_m4.cso
if($LASTEXITCODE -ne 0){throw 'packed expand m4 compilation failed'}
if($LASTEXITCODE -ne 0){throw 'packed expand compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E reduce -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D INPUT_CHANNELS=1024 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_reduce_packed_1024.cso
if($LASTEXITCODE -ne 0){throw 'packed reduce 1024 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E reduce -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D INPUT_CHANNELS=1024 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 -D NATIVE_SPLIT_K=1 -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_reduce_splitk_packed_1024.cso
& $Dxc -I $Inc -T cs_6_10 -E reduce -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D INPUT_CHANNELS=1024 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 -D NATIVE_SPLIT_K=1 -D BLOCK_M=4 -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_reduce_splitk_packed_1024_m4.cso
if($LASTEXITCODE -ne 0){throw 'packed split-K reduce 1024 m4 compilation failed'}
if($LASTEXITCODE -ne 0){throw 'packed split-K reduce 1024 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E combine -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D INPUT_CHANNELS=1024 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 -D NATIVE_SPLIT_K=1 -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_combine_packed_1024.cso
& $Dxc -I $Inc -T cs_6_10 -E combine -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D INPUT_CHANNELS=1024 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 -D NATIVE_SPLIT_K=1 -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_combine_packed_1024_m4.cso
if($LASTEXITCODE -ne 0){throw 'packed combine 1024 m4 compilation failed'}
if($LASTEXITCODE -ne 0){throw 'packed combine 1024 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E project -HV 2021 -enable-16bit-types -O3 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 native_wave_vit_qkv.hlsl -Fo native_wave_vit_qkv_packed.cso
& $Dxc -I $Inc -T cs_6_10 -E project -HV 2021 -enable-16bit-types -O3 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D NATIVE_PACKED_INPUT=1 -D BLOCK_M=4 native_wave_vit_qkv.hlsl -Fo native_wave_vit_qkv_packed_m4.cso
if($LASTEXITCODE -ne 0){throw 'packed qkv m4 compilation failed'}
if($LASTEXITCODE -ne 0){throw 'packed qkv compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E reduce -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_SPLIT_K=$(if($env:DLSS5_VIT_SPLIT_PROBE){2}else{1})" -D BLOCK_M=4 -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_reduce_splitk_m4.cso
if($LASTEXITCODE -ne 0){throw 'split-K reduce m4 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E combine -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_SPLIT_K=$(if($env:DLSS5_VIT_SPLIT_PROBE){2}else{1})" -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_combine_m4.cso
if($LASTEXITCODE -ne 0){throw 'combine m4 compilation failed'}
$env:DLSS5_VIT_PACKED_INPUT='1'
$env:DLSS5_VIT_BLOCK_M=if($env:DLSS5_VIT_BLOCK_M){$env:DLSS5_VIT_BLOCK_M}else{'1'}
# ---- run_split_ffwd_waves4_network.ps1
# FAST PATH: C512 ffwd with four waves per 16-token group (compiled by run_parallel_split_network.ps1 with NATIVE_SPLIT_FFWD_WAVES4=1).
$env:DLSS5_BUILD_SPLIT_FFWD_WAVES4='1'
# ---- run_vit_split_k_network.ps1
$BlockN=4
# The fast chain's ViT build flags are set by runners further down the chain; set them here so the split-K variants match the packed weights.
foreach($Name in 'DLSS5_FAST_ACCUMULATE','DLSS5_FAST_EPILOGUE','DLSS5_FP8_OPERANDS','DLSS5_FP8_HIDDEN','DLSS5_FP8_LDS'){Set-Item -Path "Env:$Name" -Value '1'}
# FAST PATH: ViT contract / projection reduce with the four K partitions as parallel groups plus a combine pass.
& $Dxc -I $Inc -T cs_6_10 -E reduce -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_SPLIT_K=$(if($env:DLSS5_VIT_SPLIT_PROBE){2}else{1})" -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_reduce_splitk.cso
if($LASTEXITCODE -ne 0){throw 'split-K reduce 4096 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E combine -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_SPLIT_K=$(if($env:DLSS5_VIT_SPLIT_PROBE){2}else{1})" -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_combine.cso
if($LASTEXITCODE -ne 0){throw 'split-K combine 4096 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E reduce -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D INPUT_CHANNELS=1024 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_SPLIT_K=$(if($env:DLSS5_VIT_SPLIT_PROBE){2}else{1})" -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_reduce_splitk_1024.cso
if($LASTEXITCODE -ne 0){throw 'split-K reduce 1024 compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E combine -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D INPUT_CHANNELS=1024 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_SPLIT_K=$(if($env:DLSS5_VIT_SPLIT_PROBE){2}else{1})" -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_combine_1024.cso
if($LASTEXITCODE -ne 0){throw 'split-K combine 1024 compilation failed'}
$env:DLSS5_VIT_SPLIT_K='1'
# ---- run_fast_vit_attention_network.ps1
# FAST PATH: ViT attention without software H()/legacy F(), FP32 accumulation across keys (compiled by run_vit_attention_half_network.ps1).
$env:DLSS5_BUILD_FAST_VIT_ATTENTION='1'
# ---- run_c32_fp8_qkv_network.ps1
# FAST PATH: C32 Q/K/V as E4M3 in aux via hardware cast; attention loads FP8 (compiled by run_c32_fused_attention_network.ps1 with NATIVE_C32_FP8_QKV=1).
$env:DLSS5_BUILD_C32_FP8_QKV='1'
# ---- run_hw_h_network.ps1
# FAST PATH: software H() (f16 RNE) replaced by the hardware f32tof16/f16tof32 pair in the fast kernels.
$env:DLSS5_BUILD_HW_H='1'
# ---- run_c32_ffn_fp8_network.ps1
# FAST PATH: C32 FFN hidden layer via hardware E4M3 cast, FP8 contract weights (compiled by run_blocked_c32_ffn_network.ps1 with NATIVE_C32_FFN_FP8=1).
$env:DLSS5_BUILD_C32_FFN_FP8='1'
# ---- run_c32_ffn_fast2_network.ps1
# FAST PATH: C32 FFN with matrix-block LDS/output stores and the raw input tile in LDS for the residual
# (compiled by run_blocked_c32_ffn_network.ps1 with NATIVE_C32_FFN_FAST2=1).
$env:DLSS5_BUILD_C32_FFN_FAST2='1'
# ---- run_c32_fused_attention_network.ps1
# FAST PATH: C32 attention as two dispatches (qkv+normalize, attention+projection).
foreach($Pass in @(@(0,'qkv'),@(2,'attention'))){
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E $Pass[1] -HV 2021 -enable-16bit-types -O3 -D "NATIVE_C32_EPILOGUE=$(if($env:DLSS5_BUILD_C32_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_C32_HALF_STREAM=$(if($env:DLSS5_BUILD_C32_HALF_STREAM -eq '1'){1}else{0})" -D "PASS=$($Pass[0])" -D RAW_OUTPUT=1 -D NATIVE_FAST_ACCUMULATE=1 -D "NATIVE_HW_H=$(if($env:DLSS5_BUILD_HW_H -eq '1'){1}else{0})" -D NATIVE_FAST_ATTENTION=1 -D NATIVE_C32_FUSED=1 -D "NATIVE_C32_FP8_QKV=$(if($env:DLSS5_BUILD_C32_FP8_QKV -eq '1'){1}else{0})" -D "NATIVE_C32_ATTN_FAST2=$(if($env:DLSS5_BUILD_C32_ATTN_FAST2 -eq '1'){1}else{0})" -D "NATIVE_C32_ATTN_FAST3=$(if($env:DLSS5_BUILD_C32_ATTN_FAST3 -eq '1'){1}else{0})" -D "NATIVE_C32_ATTN_FAST4=$(if($env:DLSS5_BUILD_C32_ATTN_FAST4 -eq '1'){1}else{0})" -D "NATIVE_C32_PRECISE_CHAIN=$(if($env:DLSS5_BUILD_C32_PRECISE_CHAIN -eq '1'){1}else{0})" native_wave_c32_split_attention.hlsl -Fo "native_wave_c32_fused_$($Pass[1]).cso"
 if($LASTEXITCODE -ne 0){throw "Fused C32 attention $($Pass[1]) compilation failed"}
}
if($env:DLSS5_BUILD_C32_FUSED_FFN -eq '1'){
 # FAST PATH (DLSS5_C32_FUSED_FFN): fast4 attention with the C32 FFN in its prologue (native_c32_ffn_fused.hlsli).
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D NATIVE_C32_EPILOGUE=1 -D NATIVE_C32_HALF_STREAM=1 -D PASS=2 -D RAW_OUTPUT=1 -D NATIVE_FAST_ACCUMULATE=1 -D NATIVE_HW_H=1 -D NATIVE_FAST_ATTENTION=1 -D NATIVE_C32_FUSED=1 -D NATIVE_C32_FP8_QKV=1 -D NATIVE_C32_ATTN_FAST2=1 -D NATIVE_C32_ATTN_FAST3=1 -D NATIVE_C32_ATTN_FAST4=1 -D NATIVE_C32_FUSED_FFN=1 native_wave_c32_split_attention.hlsl -Fo native_wave_c32_fused_attention_ffn.cso
 if($LASTEXITCODE -ne 0){throw 'Fused C32 attention+FFN compilation failed'}
}
if($env:DLSS5_BUILD_C32_WAVE_ATTENTION -eq '1'){
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E attention_wave -HV 2021 -enable-16bit-types -O3 -D "NATIVE_C32_EPILOGUE=$(if($env:DLSS5_BUILD_C32_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_C32_HALF_STREAM=$(if($env:DLSS5_BUILD_C32_HALF_STREAM -eq '1'){1}else{0})" -D PASS=4 -D RAW_OUTPUT=1 -D NATIVE_FAST_ACCUMULATE=1 -D NATIVE_HW_H=1 -D NATIVE_FAST_ATTENTION=1 -D NATIVE_C32_FUSED=1 -D NATIVE_C32_FP8_QKV=1 native_wave_c32_split_attention.hlsl -Fo native_wave_c32_fused_attention_wave.cso
 if($LASTEXITCODE -ne 0){throw 'C32 wave attention compilation failed'}
 $env:DLSS5_C32_WAVE_ATTENTION='1'
}
$env:DLSS5_C32_FUSED_ATTENTION='1'
# ---- run_fast_prefix_network.ps1
# FAST PATH: preblock input-mix prefix as float dot + f16 round (cs_5_1 shader compiled at runtime; manifest hash updated by update-manifest.ps1).
$env:DLSS5_FAST_PREFIX='1'
# ---- run_fp8_qkv_norm_network.ps1
# FAST PATH step 3c: E4M3 Q/K/V between the fused QKV+normalize kernel and the direct attention kernel.
foreach($Channels in 256,128,64){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D NATIVE_FP8_QKV_OUT=1 -D "NATIVE_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_TILED_PQ -eq '1'){1}else{0})" -D "NATIVE_QKV_FAST2=$(if($env:DLSS5_BUILD_QKV_FAST2 -eq '1'){1}else{0})" native_wave_qkv_normalize.hlsl -Fo "native_wave_qkv_normalize_fp8qkv$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "FP8 QKV normalize C$Channels compilation failed"}
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D "CHANNELS=$Channels" -D DIRECT_NORMALIZE=0 -D NATIVE_FAST_ACCUMULATE=1 -D "NATIVE_HW_H=$(if($env:DLSS5_BUILD_HW_H -eq '1'){1}else{0})" -D NATIVE_FAST_ATTENTION=1 -D NATIVE_FP8_OUTPUT=1 -D NATIVE_FP8_QKV=1 -D "NATIVE_ATTN_FAST2=$(if($env:DLSS5_BUILD_ATTN_FAST2 -eq '1'){1}else{0})" native_wave_attention_direct.hlsl -Fo "native_wave_attention_direct_fp8qkv_fp8act$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "FP8 QKV attention C$Channels compilation failed"}
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D "CHANNELS=$Channels" -D "NATIVE_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_TILED_PQ -eq '1'){1}else{0})" native_wave_attention_fused_qkv.hlsl -Fo "native_wave_attention_fused_qkv$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Fused QKV attention C$Channels compilation failed"}
}
$env:DLSS5_FP8_QKV_NORM='1'
# ---- run_hw_quantize_network.ps1
# FAST PATH step 4 probe: fused FFN quantizes through the hardware E4M3 cast (no scalar Ffast/H in the epilogues).
# Overrides the fp8act fused FFN shaders in this folder; keep it in its own release directory.
foreach($Channels in 256,128,64){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D NATIVE_FP8_OUTPUT=1 -D NATIVE_HW_QUANTIZE=1 native_wave_ffn_fused.hlsl -Fo "native_wave_ffn_fused_fp8act$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "HW-quantize fused FFN C$Channels compilation failed"}
}
# ---- run_fp8_activations_network.ps1
# FAST PATH step 3: E4M3 activations between fused FFN contract / attention and their projections.
# Producers store E4M3 bytes (NATIVE_FP8_OUTPUT), the five projection variants load A tiles directly (NATIVE_FP8_INPUT).
$Common=@('-I',$Inc,'-T','cs_6_10','-E','main','-HV','2021','-enable-16bit-types','-O3','-D','NATIVE_FAST_ACCUMULATE=1','-D',"NATIVE_HW_H=$(if($env:DLSS5_BUILD_HW_H -eq '1'){1}else{0})",'-D','NATIVE_FP8_OPERANDS=1')
foreach($Channels in 256,128,64){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc @Common -D "MATRIX_CHANNELS=$Channels" -D NATIVE_FP8_OUTPUT=1 -D "NATIVE_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_TILED_WEIGHTS -eq '1'){1}else{0})" -D "NATIVE_PRECISE_CHAIN=$(if($env:DLSS5_BUILD_C32_PRECISE_CHAIN -eq '1'){1}else{0})" native_wave_ffn_fused.hlsl -Fo "native_wave_ffn_fused_fp8act$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Fused FFN fp8act C$Channels compilation failed"}
 if($env:DLSS5_BUILD_FUSED_FFN_PROJ0 -eq '1'){
  # FAST PATH (DLSS5_FUSED_FFN_PROJ0): FFN + output projection in one dispatch (native_wave_ffn_proj0_fused.hlsl), f32 and E4M3 feature variants.
  foreach($Feat in @(@('',@()),@('_f8in',@('-D','NATIVE_FP8_FEATURE=1')))){
   $FExtra=$Feat[1]
   & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D "NATIVE_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_TILED_WEIGHTS -eq '1'){1}else{0})" -D "NATIVE_TILED_PQ=$(if($env:DLSS5_BUILD_TILED_PQ -eq '1'){1}else{0})" -D NATIVE_PRECISE_CHAIN=1 @FExtra native_wave_ffn_proj0_fused.hlsl -Fo "native_wave_ffn_proj0_fused$($Feat[0])$Suffix.cso"
   if($LASTEXITCODE -ne 0){throw "Fused FFN+proj0 C$Channels$($Feat[0]) compilation failed"}
  }
 }
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D "CHANNELS=$Channels" -D DIRECT_NORMALIZE=0 -D NATIVE_FAST_ACCUMULATE=1 -D "NATIVE_HW_H=$(if($env:DLSS5_BUILD_HW_H -eq '1'){1}else{0})" -D NATIVE_FAST_ATTENTION=1 -D NATIVE_FP8_OUTPUT=1 native_wave_attention_direct.hlsl -Fo "native_wave_attention_direct_fp8act$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Attention fp8act C$Channels compilation failed"}
 foreach($Variant in @(@('native_wave_project',@('-D','RAW=0')),@('native_wave_project_raw',@('-D','RAW=1')),@('native_wave_project_mapfeature',@('-D','MAP_FEATURE=1')),@('native_wave_project_mapoutput',@('-D','RAW=0','-D','MAP_OUTPUT=1')),@('native_wave_project_raw_mapoutput',@('-D','RAW=1','-D','MAP_OUTPUT=1')))){
  $Extra=$Variant[1]
  & $Dxc @Common -D "MATRIX_CHANNELS=$Channels" -D NATIVE_FP8_INPUT=1 -D "NATIVE_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_TILED_PQ -eq '1'){1}else{0})" @Extra native_wave_project.hlsl -Fo "$($Variant[0])_fp8act$Suffix.cso"
  if($LASTEXITCODE -ne 0){throw "Projection $($Variant[0]) fp8act C$Channels compilation failed"}
 }
}
$env:DLSS5_FP8_ACTIVATIONS='1'
# ---- run_fused_ffn_network.ps1
# FAST PATH step 2b: Swin FFN expand+contract fused, hidden block in LDS (multihead C64/C128/C256).
foreach($Channels in 256,128,64){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" native_wave_ffn_fused.hlsl -Fo "native_wave_ffn_fused$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Fused FFN C$Channels compilation failed"}
}
$env:DLSS5_FUSED_FFN='1'
# ---- run_fused_qkv_normalize_network.ps1
# FAST PATH step 2a: QKV GEMM + normalize fused into one wave kernel (multihead C64/C128/C256).
foreach($Channels in 256,128,64){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D "NATIVE_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_TILED_PQ -eq '1'){1}else{0})" native_wave_qkv_normalize.hlsl -Fo "native_wave_qkv_normalize$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Fused QKV normalize C$Channels compilation failed"}
}
$env:DLSS5_FUSED_QKV_NORMALIZE='1'
# ---- run_fast_temporal_network.ps1
# FAST PATH: float bilinear temporal coordinate/sample passes (no fixed-point/double, no reciprocal table).
$env:DLSS5_FAST_TEMPORAL='1'
# ---- run_fast_fp8_qkv_network.ps1
# FAST PATH: wave-matrix E4M3 QKV projection for the multihead blocks.
$env:DLSS5_FP8_QKV='1'
# ---- run_fast_attention_network.ps1
# FAST PATH stage 3b: attention scalar segments (normalize, exp, denominator, probability, residual) without intermediate roundings.
$env:DLSS5_FAST_ATTENTION='1'
# ---- run_fast_epilogue_network.ps1
# FAST PATH stage 3a: activation epilogue without intermediate f16 roundings (on top of FP8 operands + hardware accumulation).
$env:DLSS5_FAST_EPILOGUE='1'
# ---- run_fast_fp8_hidden_network.ps1
$Lds=[switch]$true
# FAST PATH stage 2b probe: ViT hidden layer as E4M3 (buffer-loaded operands); -Lds also packs LDS-staged operands.
$env:DLSS5_FP8_HIDDEN='1'
$env:DLSS5_FP8_LDS=[string][int]$Lds.IsPresent
# ---- run_fast_fp8_network.ps1
# FAST PATH stage 2a: E4M3 operands in the Swin FFN (on top of stage 1 hardware accumulation).
$env:DLSS5_FP8_OPERANDS='1'
# ---- run_fast_accumulate_network.ps1
# FAST PATH stage 1: hardware FP32 accumulation over the full K in every wave-matrix GEMM (inexact by design).
$env:DLSS5_FAST_ACCUMULATE='1'
$env:DLSS5_TEST_ALLOW_INEXACT='1'
# ---- run_shared_scratch_network.ps1
# Memory diet: multihead block scratch and C32 ffn/raw shared across serially executed blocks.
$env:DLSS5_TEST_SHARED_SCRATCH='1'
$env:DLSS5_TEST_SHARED_C32_SCRATCH='1'
# ---- run_c32_ffn_raw_store_network.ps1
# Blocked C32 FFN bound through root descriptors so its output uses the matrix Store on a raw UAV.
$env:DLSS5_TEST_C32_FFN_RAW_STORE='1'
# ---- run_fused_shift_network.ps1
# Shift pack/crop folded into the multihead body (raster-mapped pack, residual and output).
foreach($Channels in 64,128,256){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D PACKED_INPUT=1 -D MAPPED_INPUT=1 native_matrix_pack.hlsl -Fo "native_matrix_pack_mapped$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Mapped pack C$Channels compilation failed"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D MAP_FEATURE=1 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_OPERANDS -eq '1'){1}else{0})" native_wave_project.hlsl -Fo "native_wave_project_mapfeature$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Mapped-feature projection C$Channels compilation failed"}
 foreach($Raw in 0,1){
  $Name=if($Raw){"native_wave_project_raw_mapoutput$Suffix.cso"}else{"native_wave_project_mapoutput$Suffix.cso"}
  & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D "RAW=$Raw" -D MAP_OUTPUT=1 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_OPERANDS -eq '1'){1}else{0})" native_wave_project.hlsl -Fo $Name
  if($LASTEXITCODE -ne 0){throw "Mapped-output projection C$Channels raw=$Raw compilation failed"}
 }
}
$env:DLSS5_TEST_FUSED_SHIFT='1'
# ---- run_split_c32_attention_network.ps1
# Four-pass C32 attention: qkv / normalize / attention / projection, Q/K/V read by wave loads.
$Entries=@('qkv','normalize','attention','projection')
for($i=0;$i -lt 4;$i++){
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E $Entries[$i] -HV 2021 -enable-16bit-types -O3 -D "NATIVE_C32_EPILOGUE=$(if($env:DLSS5_BUILD_C32_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_C32_HALF_STREAM=$(if($env:DLSS5_BUILD_C32_HALF_STREAM -eq '1'){1}else{0})" -D "PASS=$i" -D RAW_OUTPUT=1 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_ATTENTION=$(if($env:DLSS5_FAST_ATTENTION -eq '1'){1}else{0})" native_wave_c32_split_attention.hlsl -Fo "native_wave_c32_split_$($Entries[$i]).cso"
 if($LASTEXITCODE -ne 0){throw "Split C32 attention pass $i compilation failed"}
}
$env:DLSS5_TEST_SPLIT_C32_ATTENTION='1'
# ---- run_direct_attention_network.ps1
# Multihead attention with normalized window-major f16 Q/K/V read by wave loads.
foreach($Channels in 64,128,256){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E normalize -HV 2021 -enable-16bit-types -O3 -D "CHANNELS=$Channels" -D DIRECT_NORMALIZE=1 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_ATTENTION=$(if($env:DLSS5_FAST_ATTENTION -eq '1'){1}else{0})" native_wave_attention_direct.hlsl -Fo "native_wave_attention_normalize$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Direct attention normalize C$Channels compilation failed"}
 & $Dxc -I $Inc -I $Folder -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D "CHANNELS=$Channels" -D DIRECT_NORMALIZE=0 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_ATTENTION=$(if($env:DLSS5_FAST_ATTENTION -eq '1'){1}else{0})" native_wave_attention_direct.hlsl -Fo "native_wave_attention_direct$Suffix.cso"
 if($LASTEXITCODE -ne 0){throw "Direct attention C$Channels compilation failed"}
}
$env:DLSS5_TEST_DIRECT_ATTENTION='1'
# ---- run_wave_split_project_network.ps1
# Wave-matrix FFWD/attention projections for the 512-channel split blocks.
foreach($Raw in 0,1){
 $Name=if($Raw){'native_wave_project_raw_c512.cso'}else{'native_wave_project_c512.cso'}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D MATRIX_CHANNELS=512 -D "RAW=$Raw" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_OPERANDS -eq '1'){1}else{0})" native_wave_project.hlsl -Fo $Name
 if($LASTEXITCODE -ne 0){throw "Wave split projection raw=$Raw compilation failed"}
}
$env:DLSS5_TEST_WAVE_SPLIT_PROJECT='1'
# ---- run_wave_decoder_network.ps1
# Wave-matrix decoder entry (1024->512) and the four 2x upsample projections.
foreach($Pair in @(@(1024,512),@(512,256),@(256,128),@(128,64),@(64,32))){
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "INPUT_CHANNELS=$($Pair[0])" -D "OUTPUT_CHANNELS=$($Pair[1])" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" native_wave_decoder_entry.hlsl -Fo "native_wave_decoder_$($Pair[0])_$($Pair[1]).cso"
 if($LASTEXITCODE -ne 0){throw "Wave decoder $($Pair[0])->$($Pair[1]) compilation failed"}
}
# FAST PATH (DLSS5_BUILD_C32_SKIP8): block 66 projection variant reading block 4's E4M3 main8 as the skip residual.
if($env:DLSS5_BUILD_C32_SKIP8 -eq '1'){
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "INPUT_CHANNELS=64" -D "OUTPUT_CHANNELS=32" -D "SKIP8=1" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" native_wave_decoder_entry.hlsl -Fo native_wave_decoder_64_32_skip8.cso
 if($LASTEXITCODE -ne 0){throw "Wave decoder 64->32 skip8 compilation failed"}
}
$env:DLSS5_TEST_WAVE_DECODER_LINEAR='1'
# ---- run_local_c32_attention_network.ps1
# C32 attention reads packed f16 weights directly with wave loads instead of staging them into LDS per window.
$Path=Join-Path $Folder 'shader-manifest.json'
$Manifest=Get-Content $Path -Raw | ConvertFrom-Json
$Entry=@($Manifest | Where-Object name -eq 'preblock_attention_four_wave.hlsl')
if($Entry.Count -eq 1){$Entry[0].sha256=(Get-FileHash (Join-Path $Folder 'preblock_attention_four_wave.hlsl') -Algorithm SHA256).Hash;$Manifest | ConvertTo-Json | Set-Content $Path}
$env:DLSS5_BUILD_C32_LOCAL_WEIGHTS='1'
$env:DLSS5_TEST_LOCAL_C32_ATTENTION='1'
# ---- run_wave_project_network.ps1
# Wave-matrix FFN/attention output projections for C64/C128/C256 (raw variant for group-final blocks).
foreach($Channels in 64,128,256){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 foreach($Raw in 0,1){
  $Name=if($Raw){"native_wave_project_raw$Suffix.cso"}else{"native_wave_project$Suffix.cso"}
  & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D "RAW=$Raw" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_OPERANDS -eq '1'){1}else{0})" native_wave_project.hlsl -Fo $Name
  if($LASTEXITCODE -ne 0){throw "Wave projection C$Channels raw=$Raw compilation failed"}
 }
}
$env:DLSS5_TEST_WAVE_PROJECT='1'
# ---- run_vit_attention_half_network.ps1
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "NATIVE_FAST_VIT_ATTENTION=$(if($env:DLSS5_BUILD_FAST_VIT_ATTENTION -eq '1'){1}else{0})" native_wave_vit_attention_half.hlsl -Fo native_wave_vit_attention_half.cso
if($LASTEXITCODE -ne 0){throw 'Half ViT attention compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E pack -HV 2021 -enable-16bit-types -O3 native_wave_vit_attention_half.hlsl -Fo native_wave_vit_attention_pack.cso
if($LASTEXITCODE -ne 0){throw 'ViT attention pack compilation failed'}
$env:DLSS5_TEST_WAVE_VIT_ATTENTION_HALF='1'
# ---- run_fused_exp_network.ps1
# Multihead attention without the f32 score array: exp written from QK registers into the f16 Q/K slots.
$env:DLSS5_BUILD_FUSED_EXP='1'
# ---- run_blocked_c32_ffn_network.ps1
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "NATIVE_C32_HALF_STREAM=$(if($env:DLSS5_BUILD_C32_HALF_STREAM -eq '1'){1}else{0})" -D "NATIVE_FAST_F=$(if($env:DLSS5_BUILD_FAST_F -eq '1'){1}else{0})" -D "NATIVE_STATIC_LENGTH=$(if($env:DLSS5_BUILD_STATIC_LENGTH -eq '1'){1}else{0})" -D "NATIVE_RAW_OUTPUT_STORE=$(if($env:DLSS5_TEST_C32_FFN_RAW_STORE -eq '1'){1}else{0})" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_C32_FFN_FAST2=$(if($env:DLSS5_BUILD_C32_FFN_FAST2 -eq '1'){1}else{0})" -D "NATIVE_C32_FFN_FP8=$(if($env:DLSS5_BUILD_C32_FFN_FP8 -eq '1'){1}else{0})" -D "NATIVE_C32_TILED_WEIGHTS=$(if($env:DLSS5_BUILD_C32_TILED_WEIGHTS -eq '1'){1}else{0})" -D "NATIVE_C32_FFN_FAST3=$(if($env:DLSS5_BUILD_C32_FFN_FAST3 -eq '1'){1}else{0})" -D "NATIVE_C32_MAPPED_INPUT=$(if($env:DLSS5_BUILD_C32_MAPPED_INPUT -eq '1'){1}else{0})" -D "NATIVE_C32_PRECISE_CHAIN=$(if($env:DLSS5_BUILD_C32_PRECISE_CHAIN -eq '1'){1}else{0})" native_wave_c32_ffn_blocked.hlsl -Fo native_wave_c32_ffn_blocked.cso
if($LASTEXITCODE -ne 0){throw 'Blocked C32 FFN compilation failed'}
$env:DLSS5_TEST_BLOCKED_C32_FFN='1'
# ---- run_split_ffwd_blocked_network.ps1
# Register-blocked split FFWD: one input staging per K step shared by four mix blocks.
$env:DLSS5_BUILD_SPLIT_FFWD_BLOCKED='1'
# ---- run_wave_vit_qkv_network.ps1
& $Dxc -I $Inc -T cs_6_10 -E project -HV 2021 -enable-16bit-types -O3 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" native_wave_vit_qkv.hlsl -Fo native_wave_vit_qkv.cso
if($LASTEXITCODE -ne 0){throw 'Wave ViT QKV compilation failed'}
$env:DLSS5_TEST_WAVE_VIT_QKV='1'
# ---- run_coalesced_shift_stack.ps1
# Re-evaluate the coalesced shift pack/crop copies on top of the resident-weights stack.
$Path=Join-Path $Folder 'shader-manifest.json'
$Manifest=Get-Content $Path -Raw | ConvertFrom-Json
$Entry=@($Manifest | Where-Object name -eq 'native_c64_shift.hlsl')
if($Entry.Count -ne 1){throw 'Expected one native_c64_shift.hlsl manifest entry'}
$Entry[0].sha256=(Get-FileHash (Join-Path $Folder 'native_c64_shift.hlsl') -Algorithm SHA256).Hash
$Manifest | ConvertTo-Json | Set-Content $Path
$env:DLSS5_TEST_COALESCED_MULTIHEAD_SHIFT='1'
# ---- run_resident_weights_network.ps1
# Copy every initialization-time weight/bias/map upload buffer to GPU-local memory.
$env:DLSS5_TEST_RESIDENT_WEIGHTS='1'
# ---- run_coalesced_qkv_network.ps1
# Multihead attention stages Q/K/V through LDS with coalesced 128-byte reads.
# Refresh the manifest hash for the edited attention source before validation.
$Path=Join-Path $Folder 'shader-manifest.json'
$Manifest=Get-Content $Path -Raw | ConvertFrom-Json
$Entry=@($Manifest | Where-Object name -eq 'native_c64.hlsl')
if($Entry.Count -ne 1){throw 'Expected one native_c64.hlsl manifest entry'}
$Entry[0].sha256=(Get-FileHash (Join-Path $Folder 'native_c64.hlsl') -Algorithm SHA256).Hash
$Manifest | ConvertTo-Json | Set-Content $Path
$env:DLSS5_BUILD_COALESCED_QKV='1'
# ---- run_blocked_vit_network.ps1
$BlockN=4
# Register-blocked ViT expand/reduce with f16 hidden layer, single dispatch per stage.
& $Dxc -I $Inc -T cs_6_10 -E expand -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=1 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_expand_blocked.cso
if($LASTEXITCODE -ne 0){throw 'Blocked ViT expand compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E reduce -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_reduce_blocked.cso
if($LASTEXITCODE -ne 0){throw 'Blocked ViT reduce compilation failed'}
& $Dxc -I $Inc -T cs_6_10 -E reduce -HV 2021 -enable-16bit-types -O3 -D VIT_EXPAND=0 -D INPUT_CHANNELS=1024 -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_LDS -eq '1'){1}else{0})" -D "NATIVE_FP8_HIDDEN=$(if($env:DLSS5_FP8_HIDDEN -eq '1'){1}else{0})" -D "NATIVE_VIT_TILED=$(if($env:DLSS5_BUILD_VIT_TILED -eq '1'){1}else{0})" native_wave_vit_blocked.hlsl -Fo native_wave_vit_reduce_blocked_1024.cso
if($LASTEXITCODE -ne 0){throw 'Blocked ViT projection compilation failed'}
$env:DLSS5_TEST_BLOCKED_VIT='1'
# ---- run_blocked_ffn_network.ps1
$BlockN=4
# Register-blocked wave FFN with f16 hidden storage for C64/C128/C256 Swin blocks.
foreach($Channels in 64,128,256){
 $Suffix=if($Channels -eq 256){''}else{"_c$Channels"}
 if($env:DLSS5_FP8_OPERANDS -eq '1'){
  foreach($Mapped in 0,1){
   $PackName=if($Mapped){"native_matrix_pack_fp8_mapped$Suffix.cso"}else{"native_matrix_pack_fp8$Suffix.cso"}
   & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D PACKED_INPUT=1 -D NATIVE_FP8_OPERANDS=1 -D "MAPPED_INPUT=$Mapped" native_matrix_pack.hlsl -Fo $PackName
   if($LASTEXITCODE -ne 0){throw "FP8 pack C$Channels mapped=$Mapped compilation failed"}
  }
 }
 foreach($Entry in 'expand','contract'){
  & $Dxc -I $Inc -T cs_6_10 -E $Entry -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D "BLOCK_N=$BlockN" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_FP8_OPERANDS=$(if($env:DLSS5_FP8_OPERANDS -eq '1'){1}else{0})" native_wave_ffn_blocked.hlsl -Fo "native_wave_ffn_${Entry}_blocked$Suffix.cso"
  if($LASTEXITCODE -ne 0){throw "Blocked FFN $Entry C$Channels compilation failed"}
 }
}
$env:DLSS5_TEST_BLOCKED_FFN='1'
# ---- run_async_submit_network.ps1
$Sync=[switch]$false
# Deferred submission: same command lists in the same queue order, but the CPU
# no longer waits on a fence after each of the ~512 per-frame submissions.
# -Sync reruns the identical binary in the old wait-per-submit mode as control.
if($Sync -or $env:DLSS5_TEST_FORCE_SYNC -eq "1"){$env:DLSS5_TEST_ASYNC_SUBMIT='0'}else{$env:DLSS5_TEST_ASYNC_SUBMIT='1'}
# ---- run_vit_expand_chunk2_network.ps1
$env:DLSS5_TEST_VIT_EXPAND_CHUNK2='1'
# ---- run_multihead_four_wave_network.ps1
$ParallelSoftmax=[switch]$true
$EightWaves=[switch]$true
$ParallelNorm=[switch]$false
$env:DLSS5_TEST_MULTIHEAD_FOUR_WAVES='1'
$env:DLSS5_TEST_PARALLEL_MULTIHEAD_NORM=[string][int]$ParallelNorm.IsPresent
$env:DLSS5_TEST_MULTIHEAD_EIGHT_WAVES=[string][int]$EightWaves.IsPresent
$env:DLSS5_TEST_PARALLEL_MULTIHEAD_SOFTMAX=[string][int]$ParallelSoftmax.IsPresent
# ---- run_c32_four_wave_network.ps1
$EightWaves=[switch]$true
$ParallelExp=[switch]$true
$ParallelProb=[switch]$true
$ParallelOutput=[switch]$false
$ParallelNorm=[switch]$true
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "NATIVE_C32_EIGHT_WAVES=$([int]$EightWaves.IsPresent)" -D "NATIVE_PARALLEL_C32_EXP=$([int]$ParallelExp.IsPresent)" -D "NATIVE_PARALLEL_C32_PROB=$([int]$ParallelProb.IsPresent)" -D "NATIVE_PARALLEL_C32_OUTPUT=$([int]$ParallelOutput.IsPresent)" -D "NATIVE_PARALLEL_C32_NORM=$([int]$ParallelNorm.IsPresent)" -D "NATIVE_C32_LOCAL_WEIGHTS=$(if($env:DLSS5_BUILD_C32_LOCAL_WEIGHTS -eq '1'){1}else{0})" -D RAW_OUTPUT=1 -D NATIVE_WAVE_C32_SCORES=1 -D NATIVE_WAVE_C32_QKV=1 -D NATIVE_WAVE_C32_AV=1 -D NATIVE_WAVE_C32_PROJECTION=1 preblock_attention_four_wave.hlsl -Fo native_wave_c32_full_attention.cso
if($LASTEXITCODE -ne 0){throw 'Four-wave C32 compilation failed'}
# ---- run_multihead_av_network.ps1
$SharedProb=[switch]$false
$FourWaves=if($env:DLSS5_TEST_MULTIHEAD_FOUR_WAVES -eq '1'){1}else{0}
$ParallelSoftmax=if($env:DLSS5_TEST_PARALLEL_MULTIHEAD_SOFTMAX -eq '1'){1}else{0}
$EightWaves=if($env:DLSS5_TEST_MULTIHEAD_EIGHT_WAVES -eq '1'){1}else{0}
$Coalesced=if($env:DLSS5_BUILD_COALESCED_QKV -eq '1'){1}else{0}
$FusedExp=if($env:DLSS5_BUILD_FUSED_EXP -eq '1'){1}else{0}
$ParallelNorm=if($env:DLSS5_TEST_PARALLEL_MULTIHEAD_NORM -eq '1'){1}else{0}
foreach($Channels in 64,128,256){
 $Name=if($Channels -eq 256){'native_wave_av.cso'}else{"native_wave_av_c$Channels.cso"}
 & $Dxc -I $Inc -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D "CHANNELS=$Channels" -D NATIVE_PRECOMPUTED_QKV=1 -D NATIVE_WAVE_SCORES=1 -D "NATIVE_PARALLEL_MULTIHEAD_SOFTMAX=$ParallelSoftmax" -D "NATIVE_MULTIHEAD_EIGHT_WAVES=$EightWaves" -D "NATIVE_PARALLEL_MULTIHEAD_NORM=$ParallelNorm" -D NATIVE_WAVE_AV=1 -D "NATIVE_MULTIHEAD_FOUR_WAVES=$FourWaves" -D "NATIVE_SHARED_PROB=$([int]$SharedProb.IsPresent)" -D "NATIVE_COALESCED_QKV_LOAD=$Coalesced" -D "NATIVE_FUSED_EXP=$FusedExp" native_c64.hlsl -Fo $Name
 if($LASTEXITCODE -ne 0){throw 'Multihead AV compilation failed'}
}
$env:DLSS5_TEST_WAVE_MULTIHEAD_AV='1'
# ---- run_parallel_split_network.ps1
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "NATIVE_SPLIT_FFWD_BLOCKED=$(if($env:DLSS5_BUILD_SPLIT_FFWD_BLOCKED -eq '1'){1}else{0})" -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" -D "NATIVE_FAST_EPILOGUE=$(if($env:DLSS5_FAST_EPILOGUE -eq '1'){1}else{0})" -D "NATIVE_SPLIT_FFWD_WAVES4=$(if($env:DLSS5_BUILD_SPLIT_FFWD_WAVES4 -eq '1'){1}else{0})" -D "NATIVE_HW_H=$(if($env:DLSS5_BUILD_HW_H -eq '1'){1}else{0})" native_wave_split_ffwd_parallel.hlsl -Fo native_wave_split_ffwd_parallel.cso
if($LASTEXITCODE -ne 0){throw 'Parallel split FFWD compilation failed'}
$env:DLSS5_TEST_PARALLEL_SPLIT_FFWD='1'
# ---- run_wave_vit_attention_network.ps1
& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 native_wave_vit_attention.hlsl -Fo native_wave_vit_attention.cso
if($LASTEXITCODE -ne 0){throw 'Wave ViT attention compilation failed'}
$env:DLSS5_TEST_WAVE_VIT_ATTENTION='1'
# ---- run_coalesced_finish_network.ps1
$env:DLSS5_TEST_COALESCED_FINISH='1'
# ---- run_wave_downsample_network.ps1
foreach($Channels in 64,128,256){foreach($Name in 'native_head_pool','native_wave_head_project'){
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" "$Name.hlsl" -Fo "${Name}_c$Channels.cso"
 if($LASTEXITCODE -ne 0){throw "Compile failed: $Name $Channels"}
}}
foreach($Name in 'WAVE_C32_FFN','WAVE_C32_FFN_LOCAL','WAVE_C32_AV','WAVE_C32_PROJECTION','SPLIT_PREBLOCK_FFN','WAVE_HEAD','WAVE_DOWNSAMPLE'){
 [Environment]::SetEnvironmentVariable("DLSS5_TEST_$Name",'1','Process')
}
$env:DLSS5_TEST_WAVE_SPLIT_FFWD='0'
# ---- run_matrix_split_network.ps1
foreach($Name in 'pack','qkv'){
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D MATRIX_CHANNELS=512 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" "native_matrix_$Name.hlsl" -Fo "native_matrix_${Name}_c512.cso"
 if($LASTEXITCODE -ne 0){throw "Compile failed: $Name"}
}
& $Dxc -I $Inc -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D CHANNELS=512 -D NATIVE_PRECOMPUTED_QKV=1 -D NATIVE_WAVE_SCORES=1 -D "NATIVE_HW_H=$(if($env:DLSS5_BUILD_HW_H -eq '1'){1}else{0})" -D "NATIVE_FAST_FP8=$(if($env:DLSS5_BUILD_HW_H -eq '1'){1}else{0})" native_c64.hlsl -Fo native_wave_split_attention.cso
if($LASTEXITCODE -ne 0){throw 'Split attention compile failed'}
foreach($Name in 'WAVE_C32_SCORES','WAVE_C32_QKV','WAVE_VIT_EXPAND','RESIDENT_WAVE_VIT_EXPAND','WAVE_VIT_REDUCE','MATRIX_SPLIT_ATTENTION'){
 [Environment]::SetEnvironmentVariable("DLSS5_TEST_$Name",'1','Process')
}
# ---- run_wave_c128_network.ps1
$IncludeC64=[switch]$true
foreach($Channels in $(if($IncludeC64){128,64}else{128})){
foreach($Name in 'expand','contract'){
 $Preload=if($Name -eq 'contract' -and $Channels -eq 64 -and $env:DLSS5_TEST_PRELOAD_C64_CONTRACT -eq '1'){1}else{0}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D "NATIVE_PRELOAD_CONTRACT=$Preload" "native_wave_$Name.hlsl" -Fo "native_wave_${Name}_c$Channels.cso"
 if($LASTEXITCODE -ne 0){throw "Wave compile failed: $Name"}
}
& $Dxc -I $Inc -T cs_6_10 -E attention -HV 2021 -enable-16bit-types -O3 -D "CHANNELS=$Channels" -D NATIVE_PRECOMPUTED_QKV=1 -D NATIVE_WAVE_SCORES=1 native_c64.hlsl -Fo "native_wave_scores_c$Channels.cso"
if($LASTEXITCODE -ne 0){throw 'Wave scores compile failed'}
}
$env:DLSS5_TEST_WAVE_C64=if($IncludeC64){'1'}else{'0'}
$env:DLSS5_TEST_WAVE_C256='1'
$env:DLSS5_TEST_WAVE_C128='1'
$env:DLSS5_TEST_SHARED_MATRIX_WORKSPACE='1'
$env:DLSS5_TEST_MEMORY_BUDGET='1'
$env:DLSS5_TEST_FRAME_COUNT='15'
# ---- run_matrix_c128_network.ps1
$IncludeC64=$IncludeC64
foreach($Channels in $(if($IncludeC64){128,64}else{128})){foreach($Name in 'pack','qkv','expand'){
 $Output=if($Name -eq 'expand'){"native_matrix_expand_packed_c$Channels.cso"}else{"native_matrix_${Name}_c$Channels.cso"}
 & $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" -D PACKED_INPUT=1 -D "NATIVE_FAST_ACCUMULATE=$(if($env:DLSS5_FAST_ACCUMULATE -eq '1'){1}else{0})" "native_matrix_$Name.hlsl" -Fo $Output
 if($LASTEXITCODE -ne 0){throw "Compile failed: $Name"}
}}
if($env:DLSS5_FP8_OPERANDS -eq '1'){foreach($Channels in 256,128,64){$Suffix=if($Channels -eq 256){''}else{"_c$Channels"};& $Dxc -I $Inc -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 -D "MATRIX_CHANNELS=$Channels" native_wave_qkv.hlsl -Fo "native_wave_qkv$Suffix.cso";if($LASTEXITCODE -ne 0){throw "Wave QKV C$Channels compilation failed"}}}
[Environment]::SetEnvironmentVariable('DLSS5_TEST_MATRIX_C64',$(if($IncludeC64){'1'}else{'0'}),'Process')
foreach($Name in 'TILED_C64','SPLIT_PROJECTION','SPLIT_FFWD','TILED_QKV','SHARED_C32','RESIDENT_NOISE','CACHE_C32_INPUT','PAD_C32_LDS','PAD_MULTIHEAD_LDS','MATRIX_C256','MATRIX_C128'){
 [Environment]::SetEnvironmentVariable("DLSS5_TEST_$Name",'1','Process')
}
# ---- run_native_temporal_network70.ps1
$PostShift=3
$SingleList=[switch]$false
$GpuProfile=[switch]$true
if($SingleList -and $GpuProfile){throw 'Timestamp reporting requires completed segmented submissions'}
if($GpuProfile){$env:DLSS5_NETWORK_GPU_PROFILE='1'}else{Remove-Item Env:DLSS5_NETWORK_GPU_PROFILE -ErrorAction SilentlyContinue}
$Exe=Join-Path $Folder 'native-network70-temporal.exe'
foreach($Map in 'hwc-to-vit.i32','vit-to-hwc.i32'){
 if(!(Test-Path (Join-Path $Folder $Map) -PathType Leaf)){throw "Missing layout map: $Map"}
}
if(Get-Process native-network70-temporal -ErrorAction SilentlyContinue){throw 'Existing full-network test; inspect it instead of restarting'}
if($env:DLSS5_TEST_MATRIX_C256 -eq '1' -or $env:DLSS5_TEST_MATRIX_C128 -eq '1' -or $env:DLSS5_TEST_MATRIX_C64 -eq '1'){
 $Probe='D:\DLSSNR-Lab\matrix-probe\matrix_probe.exe'
 $Capability=& $Probe --experimental 2>&1
 $ProbeExit=$LASTEXITCODE
 $Capability | Write-Output
 $Tier=[regex]::Match(($Capability -join "`n"),'tier=(?:0x)?([0-9a-fA-F]+)')
 if($ProbeExit -ne 0 -or !($Capability -match 'returned=6a') -or !$Tier.Success -or [Convert]::ToInt32($Tier.Groups[1].Value,16) -eq 0){
  throw 'Matrix capability unavailable; inspect current driver before treating this as a shader regression'
 }
}
$Manifest=Get-Content (Join-Path $Folder 'shader-manifest.json') -Raw | ConvertFrom-Json
if($Manifest.Count -ne 19){throw 'Incomplete shader manifest'}
foreach($Entry in $Manifest){
 if([IO.Path]::GetFileName($Entry.name) -ne $Entry.name){throw 'Manifest must contain basenames'}
 if((Get-FileHash (Join-Path $Folder $Entry.name) -Algorithm SHA256).Hash -ne $Entry.sha256){throw "Shader mismatch: $($Entry.name)"}
}
if(!(Select-String -Quiet -Path (Join-Path $Folder 'preblock_input_mix.hlsl') -Pattern 'NATIVE_TEMPORAL_RGB')){throw 'Temporal input shader missing'}
$Noise='D:\DLSSNR-Lab\matrix-probe\native-runtime-rgb512\functions.f32'
foreach($Name in 'DLSS5_POST_BASE_ONLY','DLSS5_ALTERNATE_RGB'){Remove-Item "Env:$Name" -ErrorAction SilentlyContinue}
$env:DLSS5_TEST_TEMPORAL_HISTORY=Join-Path $Folder 'history.f32'
$env:DLSS5_TEST_TEMPORAL_MOTION=Join-Path $Folder 'motion.f32'
$env:DLSS5_TEST_RECIPROCAL_TABLE=Join-Path $Folder 'normalized-output.f32'
$env:DLSS5_TEST_TEMPORAL_ORACLE=Join-Path $Folder 'oracle-temporal.f32'
$env:DLSS5_SHADER_PROGRESS='1'
$env:DLSS5_TEST_POST_SHIFT=[string]$PostShift
if($SingleList){$env:DLSS5_TEST_SINGLE_LIST='1'}else{Remove-Item Env:DLSS5_TEST_SINGLE_LIST -ErrorAction SilentlyContinue}
foreach($Item in @(@($Noise,201326592),@($env:DLSS5_TEST_TEMPORAL_HISTORY,33177600),@($env:DLSS5_TEST_TEMPORAL_MOTION,33177600),@($env:DLSS5_TEST_RECIPROCAL_TABLE,33554432),@($env:DLSS5_TEST_TEMPORAL_ORACLE,26542080))){
 if((Get-Item $Item[0]).Length -ne $Item[1]){throw "Fixture size mismatch: $($Item[0])"}
}
$Process=Start-Process -FilePath $Exe -ArgumentList @($Folder,$Noise) -WorkingDirectory $Folder -PassThru -RedirectStandardOutput (Join-Path $Folder 'network.stdout.log') -RedirectStandardError (Join-Path $Folder 'network.stderr.log')
$null=$Process.Handle
@{pid=$Process.Id;started=$Process.StartTime.ToString('o');executable=$Exe;post_shift=$PostShift;scope='controlled full network off/on/reset test, not game acceptance'} | ConvertTo-Json | Set-Content (Join-Path $Folder 'run.json')
Write-Output "Started PID=$($Process.Id); inspect this process and network logs, do not restart while alive."
$Process.WaitForExit()
if($null -eq $Process.ExitCode){throw 'Exit code unavailable; inspect test logs, do not assume success'}
Write-Output "Finished PID=$($Process.Id) exit=$($Process.ExitCode)"
exit $Process.ExitCode
