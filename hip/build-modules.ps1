param(
    [string]$OutputDir = (Join-Path $PSScriptRoot 'modules'),
    [string]$Compiler = (Join-Path $PSScriptRoot 'rtc_compile.exe'),
    [string]$SourceDir = $PSScriptRoot,
    [string]$Only = '',
    [string[]]$ExtraDefines = @(),
    [ValidateSet('gfx1200','gfx1201')][string[]]$Targets = @('gfx1200','gfx1201')
)
# Builds 31 modules per target (24 legacy, two opt-in wave-owned modules, two opt-in C512 32-token modules, one opt-in ViT
# projection 64-column module). 2026-09-26: the mh_fast row now spells out HIP_FFN_LINE_STORES 1 -- prod7/prod8 were built
# with it (deployments/stellar-prod7-20260924, prod8/mhfast.generated.hip) but the row lacked it, so a recipe rebuild silently
# dropped the prod7 full-line stores (bit-exact either way, -0.6/-0.7%).; by default gfx1200 and gfx1201 go into architecture subdirectories.
# 2026-09-28 numerical baseline: fast activations explicitly fuse both polynomial multiply-adds as float FMA.
# This intentionally replaces the separate-rounding fast baseline; half reference kernels remain unchanged.
# DX12 precise fast shaders retain the old baseline; see README.md (Numerical baseline).
# One row per module: output name, extra #defines, source files (concatenated in order). Every row prepends HIP_ISA_HALF 1;
# names ending in -packed also prepend HIP_PREPACKED_WEIGHTS 1. The extra defines below are the production selections of
# 2026-09-17 (0.20); they coincide with the sources' defaults and are spelled out so the recipe does not depend on them.
# -ExtraDefines 'CW_PACK8 1',...: prepended to every module (experiments; macros a module does not use are inert); a macro
# the recipe also defines takes the -ExtraDefines value.
# Compiler: rtc_compile.exe built from rtc_compile.cpp (see README.md); it uses the driver's amd_comgr_3.dll, no SDK needed.
$ErrorActionPreference = 'Stop'
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force $OutputDir | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$modules = @(
    @{ name = 'c32_prefix_reference';               defines = @();                        sources = @('c32_reference.hip', 'prefix_reference.hip') },
    @{ name = 'multihead-reference';                defines = @();                        sources = @('multihead_reference.hip') },
    @{ name = 'deep_reference';                     defines = @();                        sources = @('deep_reference.hip') },
    @{ name = 'boundary_reference';                 defines = @();                        sources = @('boundary_reference.hip') },
    @{ name = 'c32_wmma';                           defines = @();                        sources = @('c32_wmma.hip') },
    @{ name = 'multihead-wmma';                     defines = @();                        sources = @('multihead_wmma.hip') },
    @{ name = 'deep_wmma';                          defines = @();                        sources = @('deep_wmma.hip') },
    @{ name = 'wave-pointwise';                     defines = @();                        sources = @('c32_reference.hip', 'wave_pointwise.hip') },
    @{ name = 'c32_tiled';                          defines = @();                        sources = @('c32_tiled.hip') },
    @{ name = 'multihead-tiled';                    defines = @();                        sources = @('multihead_tiled.hip') },
    @{ name = 'c32_fast';                           defines = @();                        sources = @('c32_fast.hip') },
    @{ name = 'c32_fast_attention';                 defines = @();                        sources = @('c32_fast_attention.hip') },
    @{ name = 'boundary-fast';                      defines = @();                        sources = @('c32_fast_attention.hip', 'boundary_fast.hip') },
    @{ name = 'c32_fused_attention';                defines = @();                        sources = @('c32_fused_attention_packed.hip') },
    @{ name = 'c32_fused_ffn_attention';            defines = @('HIP_FP8_SAT_MODE 3');                        sources = @('c32_fused_ffn_attention.hip') },
    @{ name = 'c32_fused_ffn_attention-packed';     defines = @('HIP_C32_DIAG_WEIGHTS 1','HIP_FP8_SAT_MODE 3'); sources = @('c32_fused_ffn_attention.hip') },
    @{ name = 'prefix_fast';                        defines = @();                        sources = @('prefix_fast.hip') },
    @{ name = 'multihead-fast';                     defines = @();                        sources = @('multihead_fast.hip') },
    @{ name = 'multihead-fast-padded-wave';         defines = @('HIP_FMED3_CLAMP 1');                        sources = @('multihead_fast_padded.hip') },
    @{ name = 'multihead_fused_attention';          defines = @('HIP_MH_RTZ_ISA 1');      sources = @('multihead_fused_attention.hip') },
    @{ name = 'deep_fast';                          defines = @();                        sources = @('deep_fast.hip') },
    @{ name = 'deep_fast-packed';                   defines = @('HIP_VIT_ATTN_NATIVE_HALF 1','HIP_VIT_ATTN_PROB_PAIR 1','HIP_VIT_ATTN_TRANSPOSED_AV 1','HIP_VIT_ATTN_TRANSPOSED_SCORE 1','HIP_BRANCHLESS_F 1','C512_F_MASK 1','C512_T8_TAIL_VEC 1','HIP_BYTE_F_ADD0 1','HIP_VIT_ATTN_RCP 1');    sources = @('deep_fast.hip') },
    @{ name = 'multihead-fast-packed';              defines = @();                        sources = @('multihead_fast.hip') },
    @{ name = 'multihead-fast-padded-wave-packed';  defines = @('C512_HEAD_GROUP 1','HIP_C512_HOIST_RES 1','HIP_FFN_HOIST_RES 2','HIP_FFN_LINE_STORES 1','HIP_FMED3_CLAMP 1','HIP_POOL32_B8 1'); sources = @('multihead_fast_padded.hip','c512_head_group.inc') },
    @{ name = 'c32-wave1'; defines = @('CW_UP_FUSED 1','CW_ACT_FMED3 1','HIP_PREPACKED_WEIGHTS 1','CW_ROLL_HIDDEN 1','CW_ROLL_WINDOW 1','CW_VEC_INPUT 1','CW_PREFIX_SPLIT 1','CW_PACK8 1','HIP_FP8_SAT_MODE 3','CW_DIRECT_OUT 1','CW_RTZ_PAIR 1','CW_PACK_MODE_MASK 127','CW_PREFIX_DIRECT_OUT 1','CW_PREFIX_FULL_TILE 1','CW_FINISH_FULL_TILE 1','CW_SKIP_BYTE 1','CW_DIAG_ONLY 1','CW_INPUT_HALF 7','CW_PREFIX_HALF_SOURCE 1','CW_PREPOST_BYTE 1','CW_PREFIX_TAIL_VEC 1','CW_FINISH_TAIL_VEC 1','CW_HOIST_UP 3'); sources = @('c32_fused_ffn_attention.hip','wave_owned_c32.inc') },
    @{ name = 'c64-wave2'; defines = @('W2_UP_FUSED 1','W2_FFN_QT_SMALL_MASK 3','W2_FFN_QT_BATCH 2','W2_BOUNDED_RCP 1','HIP_PREPACKED_WEIGHTS 1','HIP_FFN_HOIST_RES 2','HIP_PDL_KERNELS 0','W2_FRAGMENT_WEIGHTS 1','W2_LAUNDER_QKV 1','W2_SCHED_FENCE 1','W2_ROLL_QUERY 1','W2_HIDDEN_TILES 2','W2_PACK8 6','HIP_FMED3_CLAMP 1','W2_BYTE_INPUT_LOADS 1','W2_RTZ_PAIR 1','W2_DIRECT_COORDS 1','W2_Q8_MASK 1','W2_FFN_W16 1','W2_HOIST_LOADS 15','W2_UP_LOW_BYTES 1'); sources = @('multihead_fast_padded.hip','wave_owned_mh.inc','wave_owned_attention_setup.inc','@wave-owned-attention-body','wave_owned_attention_exports.inc') },
    @{ name = 'c512-m32-mh'; defines = @('C512_COMPACT_QKV_ATTN 1','C512_FUSED_QKV_ATTN 1','HIP_PREPACKED_WEIGHTS 1','HIP_FFN_HOIST_RES 2','HIP_PDL_KERNELS 0','HIP_FMED3_CLAMP 1','C512_COMPACT_NOF 1','C512_COMPACT_RCP 1','C512_COMPACT_FUSEQKV 1'); sources = @('multihead_fast_padded.hip','c512_m32_mh.inc','c512_qkv_attention_fused.inc','c512_qkv_attention_compact.inc') },
    @{ name = 'c512-m32-deep'; defines = @('HIP_PREPACKED_WEIGHTS 1','HIP_BRANCHLESS_F 1','C512_MIX_OCC_LDS 4096','C512_F_MASK 1','HIP_BYTE_F_ADD0 1','C512_FFN_ONE 1'); sources = @('deep_fast.hip','c512_m32_deep.inc') },
    @{ name = 'vit-stream'; defines = @('HIP_PREPACKED_WEIGHTS 1','HIP_BRANCHLESS_F 1','HIP_VIT_STREAM_KERNELS 1','HIP_VIT_QKV_W5 1','VIT_CONTRACT_OCC_LDS 4096'); sources = @('deep_fast.hip','vit_stream.inc') },
    @{ name = 'vit-wide-deep'; defines = @('HIP_PREPACKED_WEIGHTS 1','HIP_BRANCHLESS_F 1'); sources = @('deep_fast.hip','vit_wide_deep.inc') },
    @{ name = 'swin-persistent'; defines = @('W2_UP_FUSED 1','W2_FFN_QT_SMALL_MASK 3','W2_FFN_QT_BATCH 2','W2_BOUNDED_RCP 1','HIP_PREPACKED_WEIGHTS 1','HIP_FFN_HOIST_RES 2','HIP_PDL_KERNELS 0','W2_FRAGMENT_WEIGHTS 1','W2_LAUNDER_QKV 1','W2_SCHED_FENCE 1','W2_ROLL_QUERY 1','W2_HIDDEN_TILES 2','W2_PACK8 6','HIP_FMED3_CLAMP 1','W2_BYTE_INPUT_LOADS 1','W2_RTZ_PAIR 1','W2_DIRECT_COORDS 1','W2_Q8_MASK 1','W2_FFN_W16 1','W2_EXPLICIT_WINDOW 1','W2_NO_EXPORTS 1','HIP_SWIN_PERSISTENT_KERNELS 1','SP_ENDS 1','W2_SHARED_PLANES 1'); sources = @('multihead_fast_padded.hip','wave_owned_mh.inc','@swin-persistent-types','swin_persistent.inc') }
)
$outputRoot=$OutputDir
foreach($target in $Targets){
$OutputDir=if($Targets.Count -gt 1){Join-Path $outputRoot $target}else{$outputRoot}
New-Item -ItemType Directory -Force $OutputDir|Out-Null
$manifest = @()
foreach ($m in $modules) {
    if ($Only -and $m.name -ne $Only) { continue }
    $text = "#define HIP_ISA_HALF 1`n"
    if ($m.name -like '*-packed') { $text += "#define HIP_PREPACKED_WEIGHTS 1`n" }
    # an -ExtraDefines macro overrides the recipe's own value of the same macro
    $extraNames = @($ExtraDefines | ForEach-Object { ($_ -split '\s+')[0] })
    $recipe = @($m.defines | Where-Object { ($_ -split '\s+')[0] -notin $extraNames })
    foreach ($d in @($ExtraDefines)+$recipe) { $text += "#define $d`n" }
    foreach ($part in $m.sources) {
        if ($part -eq '@swin-persistent-types') {
            $types=Join-Path $SourceDir 'swin_persistent_types.h'
            if(!(Test-Path $types)){$types=Join-Path $SourceDir '../Development/HIP/swin_persistent_types.h'}
            if(!(Test-Path $types)){throw 'Copy canonical Development/HIP/swin_persistent_types.h beside the HIP sources'}
            $text += ([IO.File]::ReadAllText($types) -replace '#pragma once', '') + "`n"
        } elseif ($part -eq '@wave-owned-attention-body') {
            $core=[IO.File]::ReadAllText((Join-Path $SourceDir 'wave_owned_mh.inc'))
            $start=$core.IndexOf(' // One wave owns all keys');$end=$core.IndexOf('#define W2_KERNEL')
            if($start -lt 0 -or $end -le $start){throw 'Wave-owned attention extraction anchors missing'}
            $text += $core.Substring($start,$end-$start)+"`n"
        } else { $text += [IO.File]::ReadAllText((Join-Path $SourceDir $part)) + "`n" }
    }
    $generated = Join-Path $OutputDir ($m.name + '.generated.hip')
    $hsaco = Join-Path $OutputDir ($m.name + '.hsaco')
    [IO.File]::WriteAllText($generated, $text, $utf8)
    & $Compiler $hsaco $generated comgr $target | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "COMGR failed: $($m.name)" }
    $manifest += [pscustomobject]@{ target = $target; module = $m.name; defines = (@('HIP_ISA_HALF 1') + $(if ($m.name -like '*-packed') { @('HIP_PREPACKED_WEIGHTS 1') } else { @() }) + @($ExtraDefines) + $recipe) -join '; '; sources = $m.sources -join '+'; sha256 = (Get-FileHash $hsaco).Hash }
    Write-Output ("{0} {1,-40} {2}" -f $target,$m.name, $manifest[-1].sha256)
}
[IO.File]::WriteAllText((Join-Path $OutputDir 'modules.json'), ($manifest | ConvertTo-Json -Depth 3), $utf8)
$sums = $manifest | ForEach-Object { $_.sha256.ToLower() + '  ' + $_.module + '.hsaco' }
[IO.File]::WriteAllText((Join-Path $OutputDir 'SHA256SUMS'), (($sums -join "`n") + "`n"), $utf8)

}

if($Targets.Count -gt 1){
 $all=@(foreach($target in $Targets){foreach($line in [IO.File]::ReadAllLines((Join-Path (Join-Path $outputRoot $target) 'SHA256SUMS'))){$line.Substring(0,66)+$target+'/'+$line.Substring(66)}})
 [IO.File]::WriteAllLines((Join-Path $outputRoot 'SHA256SUMS'),$all,$utf8)
}
