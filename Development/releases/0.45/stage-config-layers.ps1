# stage-config-layers.ps1 (2026-10-03, three config layers; src/native_config_layers.h). Called by the package script for one staged
# DLSS5-AMD folder: the flags template becomes default-config.txt (an upgrade may overwrite it), the user template ships as
# custom-config.template.txt, and native-game-flags.txt is removed from the stage -- a zip that carried custom-config.txt or
# native-game-flags.txt would overwrite the user's own file when unpacked over an old install. Prints the default-config path.
param([Parameter(Mandatory)][string]$Lab,[Parameter(Mandatory)][string]$Template,[Parameter(Mandatory)][string]$CustomTemplate)
$ErrorActionPreference='Stop'
Copy-Item $Template "$Lab\default-config.txt" -Force
Copy-Item $CustomTemplate "$Lab\custom-config.template.txt" -Force
foreach($n in 'native-game-flags.txt','custom-config.txt'){if(Test-Path "$Lab\$n"){Remove-Item "$Lab\$n" -Force}}
"$Lab\default-config.txt"
