# Codec integration options and regression test

`NativeCodecParameters` retains the old defaults. Existing aggregate initializers
remain valid: all new booleans default to false.

| Parameter | Default | Effect when enabled |
| --- | --- | --- |
| `auto_white` | false | Without an exposure texture, estimate a white point from the original image. |
| `use_pre_exposure` | false | Without an exposure texture and with `auto_white` off, use valid nonunit host pre-exposure to scale paper white. |
| `hue_safe` | false | Alternative ColorStrength curve: 0 gives original colour, 1 transfers luminance while retaining game chroma, 2 reaches the legacy CS=1 result. |

The legacy ColorStrength formula remains the default, including CS=1. An exposure
texture retains its existing precedence and formula. The new no-texture policies
do not alter sRGB I/O. Set the same exposure policy on encoder and decoder.

The existing 20-word cbuffer is unchanged in size. `Reserved.x` uses bits 0–15
for debug view, 16 for auto white, 17 for pre-exposure-only, and 18 for hue-safe.
All four debug views mask out the policy bits. C++ callers must use the boolean
members rather than injecting flags into `NativeCodecDebugView`; invalid enums
are rejected.

R10G10B10A2_UNORM now has a full raw-buffer write-back path, with 10/10/10/2
quantization, RGBA bit order and preserved two-bit alpha. `BufferFootprint()`
returns the R10 format and 256-byte-aligned row pitch. The existing frame's
`CopyTextureRegion` path therefore writes the result into an R10 target.
The opt-in private FP16 output path remains available.

## Run

From an MSVC x64 developer shell at the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Development/test_codec_integration.ps1 -Amd
```

Omit `-Amd` for WARP only. Requires the pre-PR commit `54e14de5` locally to
compile the original shaders as an independent compatibility reference.
No weights, HIP modules or game captures are required.

The test checks:

- Byte-for-byte default encode/decode compatibility, HDR and sRGB, nonunit
  pre-exposure and multiple ColorStrength values.
- Legacy RGBA8/BGRA8 (Magpie-style), R11G11B10 and UNORM16 packed output.
- Explicit pre-exposure and hue-safe opt-ins, and all four debug views with flags.
- R10 texture write-back at 65×7 (padded row pitch) and 1920×1080, boundary values,
  all alpha codes, source rebinding and private FP16 output.

These are synthetic GPU tests, not Magpie or game visual validation. The earlier
integration test of R10 input with FP16 output did not cover upstream R10
write-back; this regression explicitly copies back into an R10 texture and reads
its packed pixels.
