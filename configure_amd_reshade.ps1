$ErrorActionPreference = 'Stop'
$Win64 = 'C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
$Ini = Join-Path $Win64 'ReShade.ini'
$Text = Get-Content -LiteralPath $Ini -Raw
$Text = $Text -replace '(?m)^EffectSearchPaths=.*$', ('EffectSearchPaths=' + (Join-Path $Win64 'amd-shaders'))
$Text = $Text -replace '(?m)^TextureSearchPaths=.*$', ('TextureSearchPaths=' + (Join-Path $Win64 'reshade-shaders\Textures'))
$Text = $Text -replace '(?m)^PresetPath=.*$', ('PresetPath=' + (Join-Path $Win64 'ReShadePreset.ini'))
$Text = $Text -replace '(?m)^NoReloadOnInit=.*$', 'NoReloadOnInit=1'
$Text = $Text -replace '(?m)^PreprocessorDefinitions=.*$', 'PreprocessorDefinitions='
$Text = $Text -replace '(?m)^SkipLoadingDisabledEffects=.*$', 'SkipLoadingDisabledEffects=1'
Set-Content -LiteralPath $Ini -Value $Text -Encoding ASCII
