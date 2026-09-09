param([int]$Seconds = 20)
$Log = 'D:\DLSSNR-Lab\logs\dlss5-1080p-runtime.txt'
function Last-Submission {
    $Matches = [regex]::Matches((Get-Content -LiteralPath $Log -Raw), 'block70_submit=(\d+)')
    if (-not $Matches.Count) { return 0 }
    return [int]$Matches[$Matches.Count - 1].Groups[1].Value
}
$Start = Last-Submission
$Clock = [Diagnostics.Stopwatch]::StartNew()
Start-Sleep -Seconds $Seconds
$Finish = Last-Submission
$Clock.Stop()
[ordered]@{
    start = $Start
    finish = $Finish
    delta = $Finish - $Start
    seconds = $Clock.Elapsed.TotalSeconds
    fps = ($Finish - $Start) / $Clock.Elapsed.TotalSeconds
} | ConvertTo-Json
