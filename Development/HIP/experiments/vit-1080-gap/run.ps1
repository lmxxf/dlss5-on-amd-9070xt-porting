# vit-1080-gap-20261001: -Name X -Module m -Defs 'MACRO 1'; builds prod (recipe as committed, must equal installed) and X, then
# bit-exact + rollover + ABBA rounds against flat-A (installed 31 modules), host unchanged. -Builds a,b to combine existing builds.
param([string]$Name,[string]$Module='',[string]$Defs='',[string]$Builds='',[int]$Rounds=3,[switch]$NoBuild)
$ErrorActionPreference='Stop';$root='D:\DLSSNR-Lab\hip-backend\vit-1080-gap-20261001'
if(!$NoBuild -and $Module){& "$root\build.ps1" -Name "prod-$Module" -Module $Module;& "$root\build.ps1" -Name $Name -Module $Module -Defs $Defs}
if(!$Builds){$Builds=$Name}
& "$root\mkset.ps1" -Name $Name -Builds $Builds
try{& "$root\full.ps1" -Set $Name -Cand P -RollHost Proll -Rounds $Rounds *> "$root\full-$Name.log";"$Name OK"}catch{"$Name FAIL $_"|Tee-Object -Append "$root\full-$Name.log"}
& "$root\summarize.ps1" -Sets $Name
'RUN_DONE'
