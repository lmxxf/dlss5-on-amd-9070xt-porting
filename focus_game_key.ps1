param([int]$VirtualKey = 13, [int]$HoldMilliseconds = 100)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class GameInput {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
}
'@
$Game = Get-Process SB-Win64-Shipping -ErrorAction Stop | Select-Object -First 1
[void][GameInput]::SetForegroundWindow($Game.MainWindowHandle)
Start-Sleep -Milliseconds 500
[GameInput]::keybd_event([byte]$VirtualKey, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds $HoldMilliseconds
[GameInput]::keybd_event([byte]$VirtualKey, 0, 2, [UIntPtr]::Zero)
