param([int]$VirtualKey = 13, [int]$HoldMilliseconds = 100, [switch]$Alt)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class GameInput {
    public delegate bool Callback(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Callback cb, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr window, System.Text.StringBuilder name, int count);
    public static IntPtr Find(uint pid) { IntPtr found=IntPtr.Zero; EnumWindows((w,d)=>{uint owner;GetWindowThreadProcessId(w,out owner);var name=new System.Text.StringBuilder(128);GetClassName(w,name,128);if(owner==pid && name.ToString()=="UnrealWindow")found=w;return true;},IntPtr.Zero);return found; }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
}
'@
$Game = Get-Process SB-Win64-Shipping -ErrorAction Stop | Select-Object -First 1
$Window=[GameInput]::Find([uint32]$Game.Id)
if($Window -eq [IntPtr]::Zero){throw 'Unreal game window not found; no input sent'}
[void][GameInput]::SetForegroundWindow($Window)
Start-Sleep -Milliseconds 500
[uint32]$Owner=0
[void][GameInput]::GetWindowThreadProcessId([GameInput]::GetForegroundWindow(),[ref]$Owner)
if($Owner -ne $Game.Id){throw 'Game is not foreground; no input sent'}
try {
 if($Alt){[GameInput]::keybd_event(18,0,0,[UIntPtr]::Zero)}
 [GameInput]::keybd_event([byte]$VirtualKey,0,0,[UIntPtr]::Zero)
 Start-Sleep -Milliseconds $HoldMilliseconds
} finally {
 [GameInput]::keybd_event([byte]$VirtualKey,0,2,[UIntPtr]::Zero)
 if($Alt){[GameInput]::keybd_event(18,0,2,[UIntPtr]::Zero)}
}
