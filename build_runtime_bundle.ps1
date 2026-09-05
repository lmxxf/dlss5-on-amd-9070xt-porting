param([string]$Lab='D:\DLSSNR-Lab')
$ErrorActionPreference='Stop'
$Log=Join-Path $Lab 'logs\dlss5-1080p-runtime.txt'
$Lines=Get-Content -LiteralPath $Log
if(-not ($Lines | Where-Object {$_ -match '^display_residual generation='})){throw 'Need a successful complete runtime trace first'}
$Names=New-Object 'System.Collections.Generic.SortedSet[string]' ([StringComparer]::Ordinal)
foreach($Line in $Lines){
    if($Line -match '^asset_(weight|shader|flag)=(.+)$'){
        $Name=$Matches[2]
        if($Name.StartsWith($Lab+'\',[StringComparison]::OrdinalIgnoreCase)){$Name=$Name.Substring($Lab.Length+1)}
        if([IO.Path]::IsPathRooted($Name) -or $Name.Contains('..')){throw "Unsafe asset name $Name"}
        [void]$Names.Add($Name.Replace('\','/'))
    }
}
if($Names.Count -lt 100){throw 'Trace is incomplete'}
$Output=Join-Path $Lab 'distribution'
[void][IO.Directory]::CreateDirectory($Output)
$Stream=[IO.File]::Create((Join-Path $Output 'runtime-assets.pack'))
$Writer=New-Object IO.BinaryWriter($Stream)
$Manifest=@()
try{
    $Writer.Write([Text.Encoding]::ASCII.GetBytes('DLSS5PK1'))
    $Writer.Write([uint32]$Names.Count)
    foreach($Name in $Names){
        $Path=Join-Path $Lab $Name.Replace('/','\')
        $File=Get-Item -LiteralPath $Path
        $Encoded=[Text.Encoding]::ASCII.GetBytes($Name)
        $Writer.Write([uint16]$Encoded.Length);$Writer.Write([uint64]$File.Length);$Writer.Write($Encoded);$Writer.Flush()
        $AssetStream=[IO.File]::OpenRead($Path)
        try{$AssetStream.CopyTo($Stream)}finally{$AssetStream.Dispose()}
        $Manifest+=[pscustomobject]@{name=$Name;bytes=$File.Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
    }
}finally{$Writer.Dispose()}
$Manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $Output 'runtime-assets-manifest.json') -Encoding UTF8
Get-Item -LiteralPath (Join-Path $Output 'runtime-assets.pack') | Select-Object FullName,Length
'asset_count='+$Names.Count
