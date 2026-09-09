$source = @'
using System;
using System.Runtime.InteropServices;
public static class NativeCuda {
    [DllImport("kernel32", CharSet=CharSet.Unicode)]
    public static extern IntPtr LoadLibrary(string name);
    [DllImport("kernel32", CharSet=CharSet.Ansi)]
    public static extern IntPtr GetProcAddress(IntPtr module, string name);
}
'@
Add-Type $source
$module = [NativeCuda]::LoadLibrary('nvcuda.dll')
foreach ($name in @(
    'cuLaunchKernel',
    'cuLaunchKernel_ptsz',
    'cuLaunchKernelEx',
    'cuLaunchKernelEx_ptsz',
    'cuLaunchCooperativeKernel',
    'cuLaunchCooperativeKernel_ptsz'
)) {
    $address = [NativeCuda]::GetProcAddress($module, $name)
    Write-Output "$name=$address"
}
