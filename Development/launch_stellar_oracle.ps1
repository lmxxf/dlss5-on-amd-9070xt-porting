param([string]$Action)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Kbd {
  [DllImport("user32.dll")] public static extern void keybd_event(byte k, byte s, uint f, UIntPtr e);
}
'@
[Kbd]::keybd_event(0x5B,0,0,[UIntPtr]::Zero)
[Kbd]::keybd_event(0x52,0,0,[UIntPtr]::Zero)
[Kbd]::keybd_event(0x52,0,2,[UIntPtr]::Zero)
[Kbd]::keybd_event(0x5B,0,2,[UIntPtr]::Zero)
Start-Sleep -Milliseconds 400
Set-Clipboard 'steam://rungameid/3489700'
$shell = New-Object -ComObject WScript.Shell
$shell.SendKeys('^v')
$shell.SendKeys('{ENTER}')
