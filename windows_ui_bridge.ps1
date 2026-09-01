param(
    [ValidateSet('Screenshot', 'Click', 'Key', 'Text', 'ClickText')]
    [string]$Action = 'Screenshot',
    [int]$X = 0,
    [int]$Y = 0,
    [int]$VirtualKey = 0,
    [string]$Text = ''
)

$ErrorActionPreference = 'Stop'
$Output = 'D:\DLSSNR-Lab\logs\desktop.png'

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DesktopInput {
    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")]
    public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
}
'@

if ($Action -eq 'Screenshot') {
    $bounds = [Windows.Forms.SystemInformation]::VirtualScreen
    $bitmap = New-Object Drawing.Bitmap $bounds.Width, $bounds.Height
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($bounds.Left, $bounds.Top, 0, 0, $bounds.Size)
    $bitmap.Save($Output, [Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    exit 0
}

if ($Action -eq 'Click') {
    [void][DesktopInput]::SetCursorPos($X, $Y)
    Start-Sleep -Milliseconds 150
    [DesktopInput]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    [DesktopInput]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    exit 0
}

if ($Action -eq 'Text' -or $Action -eq 'ClickText') {
    if ($Action -eq 'ClickText') {
        [void][DesktopInput]::SetCursorPos($X, $Y)
        [DesktopInput]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        [DesktopInput]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 250
    }
    Set-Clipboard -Value $Text
    $shell = New-Object -ComObject WScript.Shell
    $shell.SendKeys('^a')
    $shell.SendKeys('^v')
    exit 0
}

[DesktopInput]::keybd_event([byte]$VirtualKey, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 100
[DesktopInput]::keybd_event([byte]$VirtualKey, 0, 2, [UIntPtr]::Zero)
