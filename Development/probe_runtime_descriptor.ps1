param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath,

    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class DlssnrNative {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr LoadLibraryW(string path);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool FreeLibrary(IntPtr module);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate IntPtr BuildDescriptor(IntPtr output, int variant);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate IntPtr ConstructNetwork(IntPtr output, IntPtr descriptor, int batch, int width, int height);
}
'@

function Read-U64([IntPtr]$Address, [int]$Offset) {
    $bytes = New-Object byte[] 8
    [System.Runtime.InteropServices.Marshal]::Copy(
        [IntPtr]($Address.ToInt64() + $Offset),
        $bytes,
        0,
        8
    )
    return [BitConverter]::ToUInt64($bytes, 0)
}

function Read-MsvcString([IntPtr]$Address) {
    $length = Read-U64 $Address 16
    $capacity = Read-U64 $Address 24
    if ($length -gt 1048576) {
        return [ordered]@{ valid = $false; length = $length; capacity = $capacity }
    }
    $dataAddress = $Address
    if ($capacity -gt 15) {
        $dataAddress = [System.Runtime.InteropServices.Marshal]::ReadIntPtr($Address, 0)
    }
    $bytes = New-Object byte[] ([int]$length)
    if ($length -gt 0) {
        [System.Runtime.InteropServices.Marshal]::Copy($dataAddress, $bytes, 0, [int]$length)
    }
    return [ordered]@{
        valid = $true
        value = [System.Text.Encoding]::UTF8.GetString($bytes)
        length = $length
        capacity = $capacity
        data_address = ('0x{0:x}' -f $dataAddress.ToInt64())
    }
}

function Read-U64Vector([IntPtr]$Address) {
    $begin = Read-U64 $Address 0
    $end = Read-U64 $Address 8
    if ($end -lt $begin -or (($end - $begin) % 8) -ne 0) {
        throw ('Invalid uint64 vector at 0x{0:x}: begin=0x{1:x}, end=0x{2:x}' -f $Address.ToInt64(), $begin, $end)
    }
    $count = [int](($end - $begin) / 8)
    $values = @()
    for ($index = 0; $index -lt $count; $index++) {
        $values += [uint64][System.Runtime.InteropServices.Marshal]::ReadInt64([IntPtr][long]$begin, $index * 8)
    }
    return $values
}

function Read-QwordHexVector([IntPtr]$Address) {
    $begin = Read-U64 $Address 0
    $end = Read-U64 $Address 8
    if ($end -lt $begin -or (($end - $begin) % 8) -ne 0) { return @() }
    $values = @()
    for ($index = 0; $index -lt [int](($end - $begin) / 8); $index++) {
        $value = [System.Runtime.InteropServices.Marshal]::ReadInt64([IntPtr][long]$begin, $index * 8)
        $values += ('0x{0:x16}' -f $value)
    }
    return $values
}

function Read-MsvcStringVector([IntPtr]$Address) {
    $begin = Read-U64 $Address 0
    $end = Read-U64 $Address 8
    $stride = 0x20
    if ($end -lt $begin -or (($end - $begin) % $stride) -ne 0) {
        throw ('Invalid string vector at 0x{0:x}: begin=0x{1:x}, end=0x{2:x}' -f $Address.ToInt64(), $begin, $end)
    }
    $count = [int](($end - $begin) / $stride)
    $values = @()
    for ($index = 0; $index -lt $count; $index++) {
        $item = [IntPtr][long]($begin + $index * $stride)
        $values += (Read-MsvcString $item).value
    }
    return $values
}

$resolvedDll = (Resolve-Path -LiteralPath $DllPath).Path
$module = [DlssnrNative]::LoadLibraryW($resolvedDll)
if ($module -eq [IntPtr]::Zero) {
    $code = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "LoadLibraryW failed with Win32 error $code"
}

$descriptor = [System.Runtime.InteropServices.Marshal]::AllocHGlobal(0xf0)
try {
    $zeros = New-Object byte[] 0xf0
    [System.Runtime.InteropServices.Marshal]::Copy($zeros, 0, $descriptor, $zeros.Length)

    $builderAddress = [IntPtr]($module.ToInt64() + 0x39780)
    $builder = [System.Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer(
        $builderAddress,
        [type][DlssnrNative+BuildDescriptor]
    )
    $result = $builder.Invoke($descriptor, 0)

    $raw = New-Object byte[] 0xf0
    [System.Runtime.InteropServices.Marshal]::Copy($descriptor, $raw, 0, $raw.Length)

    $vectors = @()
    for ($offset = 0x60; $offset -le 0xe8; $offset += 8) {
        $vectors += [ordered]@{
            offset = ('0x{0:x}' -f $offset)
            value = ('0x{0:x}' -f (Read-U64 $descriptor $offset))
        }
    }

    $blockBegin = Read-U64 $descriptor 0x70
    $blockEnd = Read-U64 $descriptor 0x78
    $blockStride = 0xe0
    if ($blockEnd -lt $blockBegin -or (($blockEnd - $blockBegin) % $blockStride) -ne 0) {
        throw ('Invalid block vector: begin=0x{0:x}, end=0x{1:x}' -f $blockBegin, $blockEnd)
    }
    $blockCount = [int](($blockEnd - $blockBegin) / $blockStride)
    $blocks = @()
    for ($blockIndex = 0; $blockIndex -lt $blockCount; $blockIndex++) {
        $blockAddress = [IntPtr][long]($blockBegin + $blockIndex * $blockStride)
        $layerBegin = Read-U64 $blockAddress 0x20
        $layerEnd = Read-U64 $blockAddress 0x28
        $layerStride = 0x170
        if ($layerEnd -lt $layerBegin -or (($layerEnd - $layerBegin) % $layerStride) -ne 0) {
            throw ('Invalid layer vector for block {0}: begin=0x{1:x}, end=0x{2:x}' -f $blockIndex, $layerBegin, $layerEnd)
        }
        $layerCount = [int](($layerEnd - $layerBegin) / $layerStride)
        $layers = @()
        for ($layerIndex = 0; $layerIndex -lt $layerCount; $layerIndex++) {
            $layerAddress = [IntPtr][long]($layerBegin + $layerIndex * $layerStride)
            $candidateVectors = @()
            for ($candidateOffset = 0x40; $candidateOffset -le 0x158; $candidateOffset += 8) {
                $candidateBegin = Read-U64 $layerAddress $candidateOffset
                $candidateEnd = Read-U64 $layerAddress ($candidateOffset + 8)
                $candidateCapacity = Read-U64 $layerAddress ($candidateOffset + 16)
                if (
                    $candidateBegin -gt 0x10000 -and
                    $candidateBegin -le $candidateEnd -and
                    $candidateEnd -le $candidateCapacity -and
                    ($candidateCapacity - $candidateBegin) -lt 0x1000000
                ) {
                    $candidateVectors += [ordered]@{
                        offset = ('0x{0:x}' -f $candidateOffset)
                        begin = ('0x{0:x}' -f $candidateBegin)
                        end = ('0x{0:x}' -f $candidateEnd)
                        capacity = ('0x{0:x}' -f $candidateCapacity)
                        used_bytes = $candidateEnd - $candidateBegin
                        capacity_bytes = $candidateCapacity - $candidateBegin
                        values = if (
                            (($candidateEnd - $candidateBegin) % 8) -eq 0 -and
                            ($candidateEnd - $candidateBegin) -le 0x100
                        ) {
                            @(Read-QwordHexVector ([IntPtr]($layerAddress.ToInt64() + $candidateOffset)))
                        } else { @() }
                        raw_hex = if ($blockIndex -eq 70 -and
                            ($candidateEnd - $candidateBegin) -le 0x10000) {
                            $candidateRaw = New-Object byte[] ([int]($candidateEnd - $candidateBegin))
                            if ($candidateRaw.Length -gt 0) {
                                [System.Runtime.InteropServices.Marshal]::Copy(
                                    [IntPtr][long]$candidateBegin,
                                    $candidateRaw,
                                    0,
                                    $candidateRaw.Length
                                )
                            }
                            [BitConverter]::ToString($candidateRaw).Replace('-', '').ToLowerInvariant()
                        } else { $null }
                        strings = if (($blockIndex -eq 1 -or $blockIndex -eq 39 -or
                            $blockIndex -eq 40 -or $blockIndex -eq 48 -or
                            $blockIndex -eq 70) -and
                            (($candidateEnd - $candidateBegin) % 0x20) -eq 0 -and
                            ($candidateEnd - $candidateBegin) -le 0x10000) {
                            $candidateStrings = @()
                            for (
                                $stringOffset = 0;
                                $stringOffset -lt ($candidateEnd - $candidateBegin);
                                $stringOffset += 0x20
                            ) {
                                $candidateStrings += (Read-MsvcString (
                                    [IntPtr][long]($candidateBegin + $stringOffset)
                                )).value
                            }
                            $candidateStrings
                        } else { @() }
                    }
                }
            }
            $layerRawHex = $null
            if ($blockIndex -eq 39 -or $blockIndex -eq 48 -or $blockIndex -eq 70) {
                $layerRaw = New-Object byte[] 0x170
                [System.Runtime.InteropServices.Marshal]::Copy($layerAddress, $layerRaw, 0, $layerRaw.Length)
                $layerRawHex = [BitConverter]::ToString($layerRaw).Replace('-', '').ToLowerInvariant()
            }
            $layers += [ordered]@{
                index = $layerIndex
                address = ('0x{0:x}' -f $layerAddress.ToInt64())
                name = Read-MsvcString $layerAddress
                type = Read-MsvcString ([IntPtr]($layerAddress.ToInt64() + 0x20))
                candidate_vectors = $candidateVectors
                raw_hex = $layerRawHex
            }
        }
        $blocks += [ordered]@{
            block = $blockIndex
            address = ('0x{0:x}' -f $blockAddress.ToInt64())
            type = Read-MsvcString $blockAddress
            layers = $layers
            source_blocks = @(Read-U64Vector ([IntPtr]($blockAddress.ToInt64() + 0x38)))
            source_outputs = @(Read-U64Vector ([IntPtr]($blockAddress.ToInt64() + 0x50)))
        }
    }

    $networkSize = 0x2b0
    $network = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($networkSize)
    $networkDocument = $null
    try {
        $networkZeros = New-Object byte[] $networkSize
        [System.Runtime.InteropServices.Marshal]::Copy($networkZeros, 0, $network, $networkZeros.Length)
        $networkConstructorAddress = [IntPtr]($module.ToInt64() + 0x31c10)
        $networkConstructor = [System.Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer(
            $networkConstructorAddress,
            [type][DlssnrNative+ConstructNetwork]
        )
        $networkResult = $networkConstructor.Invoke($network, $descriptor, 1, 320, 320)
        $networkTail = @()
        for ($tailOffset = 0x240; $tailOffset -le 0x2a8; $tailOffset += 8) {
            $networkTail += [ordered]@{
                offset = ('0x{0:x}' -f $tailOffset)
                value = ('0x{0:x}' -f (Read-U64 $network $tailOffset))
            }
        }
        $networkBlockPointers = @(Read-U64Vector ([IntPtr]($network.ToInt64() + 0xf8)))
        $liveBlocks = @()
        for ($blockIndex = 0; $blockIndex -lt $networkBlockPointers.Count; $blockIndex++) {
            $liveBlockAddress = [IntPtr][long]$networkBlockPointers[$blockIndex]
            $layerPointers = @(Read-U64Vector ([IntPtr]($liveBlockAddress.ToInt64() + 0xe8)))
            $liveLayers = @()
            for ($layerIndex = 0; $layerIndex -lt $layerPointers.Count; $layerIndex++) {
                $liveLayerAddress = [IntPtr][long]$layerPointers[$layerIndex]
                $liveCandidates = @()
                for ($candidateOffset = 8; $candidateOffset -le 0x168; $candidateOffset += 8) {
                    $candidateBegin = Read-U64 $liveLayerAddress $candidateOffset
                    $candidateEnd = Read-U64 $liveLayerAddress ($candidateOffset + 8)
                    $candidateCapacity = Read-U64 $liveLayerAddress ($candidateOffset + 16)
                    if (
                        $candidateBegin -gt 0x10000 -and
                        $candidateBegin -le $candidateEnd -and
                        $candidateEnd -le $candidateCapacity -and
                        ($candidateCapacity - $candidateBegin) -lt 0x1000000
                    ) {
                        $liveCandidates += [ordered]@{
                            offset = ('0x{0:x}' -f $candidateOffset)
                            begin = ('0x{0:x}' -f $candidateBegin)
                            end = ('0x{0:x}' -f $candidateEnd)
                            capacity = ('0x{0:x}' -f $candidateCapacity)
                            used_bytes = $candidateEnd - $candidateBegin
                            capacity_bytes = $candidateCapacity - $candidateBegin
                            values = if (
                                (($candidateEnd - $candidateBegin) % 8) -eq 0 -and
                                ($candidateEnd - $candidateBegin) -le 0x100
                            ) {
                                @(Read-QwordHexVector ([IntPtr]($liveLayerAddress.ToInt64() + $candidateOffset)))
                            } else { @() }
                        }
                    }
                }
                $liveLayers += [ordered]@{
                    layer = $layerIndex
                    address = ('0x{0:x}' -f $liveLayerAddress.ToInt64())
                    candidate_vectors = $liveCandidates
                }
            }
            $liveBlocks += [ordered]@{
                block = $blockIndex
                address = ('0x{0:x}' -f $liveBlockAddress.ToInt64())
                layers = $liveLayers
            }
        }
        $networkDocument = [ordered]@{
            address = ('0x{0:x}' -f $network.ToInt64())
            constructor = ('0x{0:x}' -f $networkConstructorAddress.ToInt64())
            returned = ('0x{0:x}' -f $networkResult.ToInt64())
            block_count = $networkBlockPointers.Count
            blocks = $liveBlocks
            qwords_0x240_to_0x2a8 = $networkTail
        }
    }
    finally {
        # The probe process exits immediately after serialization. Avoid calling
        # an unverified C++ destructor; process teardown reclaims its allocations.
        [System.Runtime.InteropServices.Marshal]::FreeHGlobal($network)
    }

    $document = [ordered]@{
        dll = $resolvedDll
        module_base = ('0x{0:x}' -f $module.ToInt64())
        builder = ('0x{0:x}' -f $builderAddress.ToInt64())
        descriptor = ('0x{0:x}' -f $descriptor.ToInt64())
        returned = ('0x{0:x}' -f $result.ToInt64())
        string_0x00 = Read-MsvcString $descriptor
        string_0x20 = Read-MsvcString ([IntPtr]($descriptor.ToInt64() + 0x20))
        string_0x40 = Read-MsvcString ([IntPtr]($descriptor.ToInt64() + 0x40))
        block_stride = $blockStride
        block_count = $blockCount
        blocks = $blocks
        weight_names = @(Read-MsvcStringVector ([IntPtr]($descriptor.ToInt64() + 0xa8)))
        live_network = $networkDocument
        qwords_0x60_to_0xe8 = $vectors
        raw_base64 = [Convert]::ToBase64String($raw)
    }
    $json = $document | ConvertTo-Json -Depth 12
    if ($OutputPath) {
        [System.IO.File]::WriteAllText($OutputPath, $json + [Environment]::NewLine)
        Write-Output "DLSSNR_DESCRIPTOR_OUTPUT=$OutputPath"
    }
    else {
        Write-Output $json
    }
}
finally {
    [System.Runtime.InteropServices.Marshal]::FreeHGlobal($descriptor)
    [void][DlssnrNative]::FreeLibrary($module)
}
