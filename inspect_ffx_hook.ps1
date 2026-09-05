$ErrorActionPreference='Stop'
$Game=Get-Process SB-Win64-Shipping -ErrorAction Stop
$Line=Get-Content D:\DLSSNR-Lab\logs\dlss5-1080p-runtime.txt | Where-Object {$_ -match '^ffx_hook init='} | Select-Object -First 1
if($Line -notmatch 'target=([0-9a-fA-F]+)'){throw 'No logged hook address'}
$Address=[Convert]::ToInt64($Matches[1],16)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class HookMemory {
 [DllImport("kernel32.dll",SetLastError=true)] public static extern IntPtr OpenProcess(uint rights,bool inherit,int pid);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr process,IntPtr address,byte[] data,UIntPtr size,out UIntPtr read);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
$Handle=[HookMemory]::OpenProcess(0x1010,$false,$Game.Id)
if($Handle -eq [IntPtr]::Zero){throw 'OpenProcess failed'}
try{
    $Data=New-Object byte[] 32;$Read=[UIntPtr]::Zero
    if(-not[HookMemory]::ReadProcessMemory($Handle,[IntPtr]$Address,$Data,[UIntPtr]::new([UInt64]32),[ref]$Read)){throw 'ReadProcessMemory failed'}
    [pscustomobject]@{pid=$Game.Id;address=('{0:X}' -f $Address);bytes=[BitConverter]::ToString($Data);relative_jump=if($Data[0] -eq 0xE9){'{0:X}' -f ($Address+5+[BitConverter]::ToInt32($Data,1))}else{$null}} | ConvertTo-Json
    if($Data[0] -eq 0xE9){
        $Relay=$Address+5+[BitConverter]::ToInt32($Data,1)
        $RelayData=New-Object byte[] 32
        if([HookMemory]::ReadProcessMemory($Handle,[IntPtr]$Relay,$RelayData,[UIntPtr]::new([UInt64]32),[ref]$Read)){
            [pscustomobject]@{relay=('{0:X}' -f $Relay);bytes=[BitConverter]::ToString($RelayData);absolute_jump=if($RelayData[0] -eq 0xFF -and $RelayData[1] -eq 0x25 -and [BitConverter]::ToInt32($RelayData,2) -eq 0){'{0:X}' -f [BitConverter]::ToInt64($RelayData,6)}else{$null}} | ConvertTo-Json
        }
    }
}finally{[void][HookMemory]::CloseHandle($Handle)}
$Game.Modules | Where-Object {$_.ModuleName -like '*fidelityfx*' -or $_.ModuleName -like '*.addon64'} | Select-Object ModuleName,FileName,@{n='base';e={'{0:X}' -f $_.BaseAddress.ToInt64()}} | ConvertTo-Json
