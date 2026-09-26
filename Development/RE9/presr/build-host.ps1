# Builds the RE9 host (OptiScaler.dll, shipped as dxgi.dll). Source: the archive written by bundle-source.py, which every RE9
# package also carries as sources\re9-presr-source.tar.gz. Defaults are the maintainer machine's paths.
param(
 [string]$Root='D:\DLSSNR-Lab\re9-presr',
 [string]$Archive='',
 [string]$MSBuild=''
)
$ErrorActionPreference='Stop'
if(!$Archive){$Archive="$Root\host-source.tar.gz"}
if(!$MSBuild){
 $MSBuild='D:\DLSSNR-Lab\build-tools\MSBuild\Current\Bin\MSBuild.exe'
 if(!(Test-Path $MSBuild)){$vw="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe";if(Test-Path $vw){$MSBuild=& $vw -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe'|Select-Object -First 1}}
}
if(!$MSBuild -or !(Test-Path $MSBuild)){throw 'MSBuild not found: install VS 2022 Build Tools (v143 + Windows SDK 10.0.26100) or pass -MSBuild'}
$root=[IO.Path]::GetFullPath($Root);$src="$root\source";$solution="$src\OptiScaler-DLSSNR-PreSR-Multipass-main"
New-Item -ItemType Directory -Force $src,"$root\bin","$root\obj"|Out-Null
if(!(Test-Path "$solution\OptiScaler\OptiScaler.vcxproj")){
 # tar warns about files stored without a timestamp (archives up to 0.32); only a missing project counts as failure
 $ErrorActionPreference='Continue';& tar.exe -xzf $Archive -C $src 2>&1|Out-Host;$ErrorActionPreference='Stop'
 if(!(Test-Path "$solution\OptiScaler\OptiScaler.vcxproj")){throw "Source unpack failed: $Archive"}
}
[IO.File]::WriteAllText("$solution\OptiScaler\resource_build_date.h",('#define VER_BUILD_DATE "20260922_RE9_PreSR"'+"`r`n"))
[IO.File]::WriteAllText("$solution\OptiScaler\resource_build_commit.h",('#define VER_BUILD_COMMIT "8f71f73_lmxxf_staged"'+"`r`n"))
& $MSBuild "$solution\OptiScaler\OptiScaler.vcxproj" /m:4 /t:Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0.26100.0 /p:PreBuildEventUseInBuild=false /p:PostBuildEventUseInBuild=false "/p:SolutionDir=$solution/" "/p:OutDir=$root/bin/" "/p:IntDir=$root/obj/" /v:minimal /nologo /fl "/flp:logfile=$root/build-host.log;verbosity=normal" > "$root\build-console.log" 2>&1
$code=$LASTEXITCODE
Get-Content "$root\build-console.log" -Tail 35
if($code){throw "Host build failed $code"}
Get-FileHash "$root\bin\OptiScaler.dll"
