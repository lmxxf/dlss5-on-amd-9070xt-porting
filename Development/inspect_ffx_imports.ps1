$ErrorActionPreference='Stop'
$Game=Get-Process SB-Win64-Shipping -ErrorAction Stop
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ImportMemory {
 [DllImport("kernel32.dll",SetLastError=true)] public static extern IntPtr OpenProcess(uint r,bool inherit,int pid);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool ReadProcessMemory(IntPtr p,IntPtr a,byte[] b,UIntPtr s,out UIntPtr n);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr p);
 public static byte[] Read(IntPtr p,long a,int n){var b=new byte[n];UIntPtr count;if(!ReadProcessMemory(p,new IntPtr(a),b,new UIntPtr((uint)n),out count))throw new Exception("Read failed at "+a.ToString("X"));return b;}
}
'@
$Handle=[ImportMemory]::OpenProcess(0x1010,$false,$Game.Id)
function Read-Bytes([long]$Address,[int]$Count){,[ImportMemory]::Read($Handle,$Address,$Count)}
function Read-Text([long]$Address){$Bytes=Read-Bytes $Address 256;[Text.Encoding]::ASCII.GetString($Bytes).Split([char]0)[0]}
function Owner([long]$Address){foreach($Module in $Game.Modules){if($Address -ge $Module.BaseAddress.ToInt64() -and $Address -lt ($Module.BaseAddress.ToInt64()+$Module.ModuleMemorySize)){return ($Module.ModuleName+'+'+('{0:X}' -f ($Address-$Module.BaseAddress.ToInt64())))}};return 'private/unknown'}
try {
 # Read-only addresses reverse-mapped for Stellar Blade 1.4.1, not a portable patch.
 if($Game.MainModule.BaseAddress.ToInt64() -ne 0x140000000 -or [BitConverter]::ToString((Read-Bytes 0x14159e606 7)) -ne '48-89-05-13-BD-7C-05'){throw 'Unknown game build; do not use these diagnostic RVAs'}
 "resolve_code="+[BitConverter]::ToString((Read-Bytes 0x14159e580 192))
 $Dispatch=[BitConverter]::ToInt64((Read-Bytes 0x146d6a320 8),0)
 "cached_dispatch=$('{0:X}' -f $Dispatch) owner=$(Owner $Dispatch) bytes=$([BitConverter]::ToString((Read-Bytes $Dispatch 32)))"
 $Base=$Game.MainModule.BaseAddress.ToInt64()
 foreach($Cvar in @(@('Enabled',0x146f37ed0),@('UseRHI',0x146f38290),@('UseNativeDX12',0x146f382c0),@('AAQuality',0x146f896d8),@('AAMethod',0x146ff8000),@('TemporalUpsampling',0x146ff8150))){
  $Data=[BitConverter]::ToInt64((Read-Bytes $Cvar[1] 8),0)
  "cvar=$($Cvar[0]) data=$('{0:X}' -f $Data) bytes=$([BitConverter]::ToString((Read-Bytes $Data 16)))"
 }
 $Header=Read-Bytes $Base 4096
 $Pe=[BitConverter]::ToInt32($Header,60)
 $Imports=[BitConverter]::ToUInt32($Header,$Pe+24+120)
 for($Index=0;$Index -lt 512;$Index++){
  $Entry=Read-Bytes ($Base+$Imports+$Index*20) 20
  $NameRva=[BitConverter]::ToUInt32($Entry,12);if(-not $NameRva){break}
  $Name=Read-Text ($Base+$NameRva)
  if($Name -notlike '*fidelity*'){continue}
  "import_module=$Name"
  $Names=[BitConverter]::ToUInt32($Entry,0);$Slots=[BitConverter]::ToUInt32($Entry,16)
  for($Slot=0;$Slot -lt 256;$Slot++){
   $Symbol=[BitConverter]::ToInt64((Read-Bytes ($Base+$Names+$Slot*8) 8),0);if(-not $Symbol){break}
   $SymbolName=if($Symbol -gt 0){Read-Text ($Base+$Symbol+2)}else{'ordinal'}
   $Address=[BitConverter]::ToInt64((Read-Bytes ($Base+$Slots+$Slot*8) 8),0)
   "symbol=$SymbolName slot=$('{0:X}' -f ($Base+$Slots+$Slot*8)) address=$('{0:X}' -f $Address) owner=$(Owner $Address)"
  }
 }
}finally{[void][ImportMemory]::CloseHandle($Handle)}
