param(
    [Parameter(Mandatory=$true)][int]$ProcessId,
    [Parameter(Mandatory=$true)][string]$Address,
    [int]$TableCount = 0
)

$source = @'
using System;
using System.Runtime.InteropServices;
public static class ProcessMemory {
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenProcess(uint access, bool inherit, int processId);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool ReadProcessMemory(
        IntPtr process, UInt64 address, byte[] buffer, UInt64 size, out UInt64 read);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
}
'@
Add-Type $source

$base = [Convert]::ToUInt64($Address.Replace('0x',''), 16)
$handle = [ProcessMemory]::OpenProcess(0x0010, $false, $ProcessId)
if ($handle -eq [IntPtr]::Zero) { throw "OpenProcess failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }

function Read-U64([UInt64]$where) {
    $bytes = New-Object byte[] 8
    [UInt64]$read = 0
    if (-not [ProcessMemory]::ReadProcessMemory($handle, $where, $bytes, 8, [ref]$read)) {
        throw ('ReadProcessMemory failed at 0x{0:x}: {1}' -f $where, [Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
    [BitConverter]::ToUInt64($bytes, 0)
}

$vtable = Read-U64 $base
if ($TableCount -gt 0) {
    $entries = for ($index = 0; $index -lt $TableCount; $index++) {
        $target = Read-U64 ($base + [UInt64]($index * 8))
        [ordered]@{ index = $index; address = ('0x{0:x}' -f $target) }
    }
    $entries | ConvertTo-Json
    [void][ProcessMemory]::CloseHandle($handle)
    exit 0
}
$launch = Read-U64 ($vtable + 0x28)
$process = Get-Process -Id $ProcessId
$owner = $process.Modules | Where-Object {
    $moduleBase = [UInt64]$_.BaseAddress.ToInt64()
    $launch -ge $moduleBase -and $launch -lt ($moduleBase + [UInt64]$_.ModuleMemorySize)
} | Select-Object -First 1

[ordered]@{
    object = ('0x{0:x}' -f $base)
    vtable = ('0x{0:x}' -f $vtable)
    launch = ('0x{0:x}' -f $launch)
    module = if ($owner) { $owner.ModuleName } else { $null }
    module_base = if ($owner) { '0x{0:x}' -f [UInt64]$owner.BaseAddress.ToInt64() } else { $null }
    rva = if ($owner) { '0x{0:x}' -f ($launch - [UInt64]$owner.BaseAddress.ToInt64()) } else { $null }
} | ConvertTo-Json

[void][ProcessMemory]::CloseHandle($handle)
