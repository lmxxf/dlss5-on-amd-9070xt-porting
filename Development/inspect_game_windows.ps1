param([switch]$Schedule)
$ErrorActionPreference='Stop'
if ($Schedule) {
 $Action=New-ScheduledTaskAction -Execute 'powershell.exe' -Argument '-NoProfile -ExecutionPolicy Bypass -File D:\DLSSNR-Lab\inspect_game_windows.ps1'
 $Principal=New-ScheduledTaskPrincipal -UserId 'lmxxf' -LogonType Interactive
 Register-ScheduledTask -TaskName 'DLSS5WindowDiagnostic' -Action $Action -Principal $Principal -Force | Out-Null
 Start-ScheduledTask -TaskName 'DLSS5WindowDiagnostic'
 exit
}
$Game=Get-Process SB-Win64-Shipping -ErrorAction Stop
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class GameWindows {
 public delegate bool Callback(IntPtr hwnd,IntPtr param);
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback callback,IntPtr param);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd,StringBuilder text,int count);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd,StringBuilder text,int count);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd,int command);
 public static void Focus(uint target){EnumWindows((hwnd,param)=>{uint pid;GetWindowThreadProcessId(hwnd,out pid);var cls=new StringBuilder(256);GetClassName(hwnd,cls,cls.Capacity);if(pid==target && cls.ToString()=="UnrealWindow"){ShowWindow(hwnd,9);SetForegroundWindow(hwnd);}return true;},IntPtr.Zero);}
 public static string Dump(uint target){var result=new StringBuilder();EnumWindows((hwnd,param)=>{uint pid;GetWindowThreadProcessId(hwnd,out pid);if(pid==target){var title=new StringBuilder(512);var cls=new StringBuilder(256);GetWindowText(hwnd,title,title.Capacity);GetClassName(hwnd,cls,cls.Capacity);result.AppendFormat("hwnd={0:X} visible={1} iconic={2} class={3} title={4}\r\n",hwnd.ToInt64(),IsWindowVisible(hwnd),IsIconic(hwnd),cls,title);}return true;},IntPtr.Zero);return result.ToString();}
}
'@
[GameWindows]::Dump([uint32]$Game.Id) | Set-Content D:\DLSSNR-Lab\logs\game-windows.txt
[GameWindows]::Focus([uint32]$Game.Id)
