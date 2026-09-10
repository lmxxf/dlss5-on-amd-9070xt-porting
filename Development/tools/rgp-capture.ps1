# usage: rgp-capture.ps1 -Folder D:\DLSSNR-Lab\<chain> [-Frame 8] [-Dispatch -Start 1 -Count 400] [-Out D:\DLSSNR-Lab\logs\x.rgp] [-Instr]
# Frame mode (default) needs the bench built with DLSS5_TEST_PRESENT support (hidden swapchain, one Present per network frame).
# Starts RadeonDeveloperPanelCLI (headless RGP capture, dispatch mode) and then the bench (bench-norebuild.ps1, 30 frames); the CLI
# auto-captures Count consecutive dispatches starting at dispatch Start of the bench process. Needs the Radeon Developer Tool Suite unzipped.
param([Parameter(Mandatory=$true)][string]$Folder,[int]$Frame=8,[int]$Start=1,[int]$Count=400,[switch]$Dispatch,[string]$Out='',[switch]$Instr,[string]$Rdts='C:\Users\lmxxf\Downloads\RadeonDeveloperToolSuite-2026-05-28-1806\RadeonDeveloperToolSuite-2026-05-28-1806')
$ErrorActionPreference='Stop'
if(-not $Out){$Out="D:\DLSSNR-Lab\logs\rgp-$(Get-Date -Format yyyyMMdd-HHmmss).rgp"}
if($Dispatch){$Args=@('-m','profiling','-p','native-network70','-o',$Out,'--rgp-capture-mode','dispatch',"--rgp-auto-capture=dispatch:${Start}:${Count}",'--verbose')}
else{$Args=@('-m','profiling','-p','native-network70','-o',$Out,'--rgp-capture-mode','frame',"--rgp-auto-capture=frame:${Frame}",'--verbose')}
if($Instr){$Args+='--rgp-instruction-tracing'}
$Cli=Start-Process -FilePath (Join-Path $Rdts 'RadeonDeveloperPanelCLI.exe') -ArgumentList $Args -WorkingDirectory $Rdts -PassThru -RedirectStandardOutput 'D:\DLSSNR-Lab\logs\rgp-cli.log' -RedirectStandardError 'D:\DLSSNR-Lab\logs\rgp-cli.err' -RedirectStandardInput 'D:\DLSSNR-Lab\logs\rgp-stdin.txt'
Start-Sleep -Seconds 4
$env:DLSS5_TEST_FRAME_COUNT='30';$env:DLSS5_TEST_PRESENT='1'
& (Join-Path $Folder 'bench-norebuild.ps1') -Folder $Folder | Out-Null
for($i=0;$i -lt 90;$i++){if(Test-Path $Out){break};Start-Sleep -Seconds 1}
Start-Sleep -Seconds 3
if(-not $Cli.HasExited){Stop-Process -Id $Cli.Id -Force}
if(Test-Path $Out){Write-Output "captured $Out $((Get-Item $Out).Length) bytes"}else{Write-Output "no capture"}
Get-Content 'D:\DLSSNR-Lab\logs\rgp-cli.log' | Select-Object -Last 15
