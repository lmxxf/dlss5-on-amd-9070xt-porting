$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend';$a='D:\DLSSNR-Lab\Magpie-DLSS5-AMD-0.23\DLSS5-AMD\native-game-tiled-assets';$out="$r\vit-residual-scales";New-Item -ItemType Directory -Force $out|Out-Null
$meta=@()
foreach($block in 31..38){foreach($part in 'contract','projection'){
 $matrix=if($part -eq 'contract'){4194304}else{1048576};$p="$a\block$block-$part.f32";$bytes=4
 if(!(Test-Path $p)){$p="$a\block$block-$part.f16";$bytes=2}
 if((Get-Item $p).Length -ne ($matrix+1024)*$bytes){throw "Unexpected weight layout $p"}
 $stream=[IO.File]::OpenRead($p);$buffer=New-Object byte[] (1024*$bytes);try{[void]$stream.Seek($matrix*$bytes,[IO.SeekOrigin]::Begin);if($stream.Read($buffer,0,$buffer.Length) -ne $buffer.Length){throw 'Short scale read'}}finally{$stream.Dispose()}
 $name="block$block-$part"+[IO.Path]::GetExtension($p);[IO.File]::WriteAllBytes("$out\$name",$buffer);$meta+=[pscustomobject]@{block=$block;part=$part;name=$name;source_sha256=(Get-FileHash $p).Hash}
}}
$meta|ConvertTo-Json|Set-Content "$out\metadata.json" -Encoding UTF8
