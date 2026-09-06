$ErrorActionPreference='Stop'
$Source='D:\DLSSNR-Lab\matrix-probe\native-rgb512'
$Destination='D:\DLSSNR-Lab\matrix-probe\native-runtime-rgb512'
if(Test-Path -LiteralPath $Destination){throw 'Runtime fixture destination already exists; do not mix runs'}
New-Item -ItemType Directory -Path $Destination | Out-Null
$Files=@(Get-ChildItem -LiteralPath $Source -File | Where-Object {
 $_.Extension -in '.f32','.hlsl','.i32','.rgba32f' -and $_.Name -notlike 'output*' -and $_.Name -notlike 'audit*' -and $_.Name -notlike 'gpu*' -and $_.Name -notlike 'lut*'
})
foreach($File in $Files){
 Copy-Item -LiteralPath $File.FullName -Destination $Destination
 if((Get-Item -LiteralPath (Join-Path $Destination $File.Name)).Length -ne $File.Length){throw 'Staged fixture length mismatch'}
}
Write-Output "staged=$Destination files=$($Files.Count); no output/oracle pass files copied"
