param([string]$Output = 'D:\DLSSNR-Lab\logs\runtime-error-screen.png')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
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
