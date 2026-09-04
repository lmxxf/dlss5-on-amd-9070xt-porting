param([string]$Output = 'D:\DLSSNR-Lab\logs\runtime-error-screen.png')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ForegroundWindow {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int command);
}
'@
$Game = Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue
if ($Game -and $Game.MainWindowHandle -ne [IntPtr]::Zero) {
    [ForegroundWindow]::ShowWindow($Game.MainWindowHandle, 9) | Out-Null
    [ForegroundWindow]::SetForegroundWindow($Game.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 750
}
$Bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
$Bitmap = New-Object System.Drawing.Bitmap $Bounds.Width, $Bounds.Height
$Graphics = [System.Drawing.Graphics]::FromImage($Bitmap)
try {
    $Graphics.CopyFromScreen($Bounds.Left, $Bounds.Top, 0, 0, $Bitmap.Size)
    $Bitmap.Save($Output, [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
    $Graphics.Dispose()
    $Bitmap.Dispose()
}
