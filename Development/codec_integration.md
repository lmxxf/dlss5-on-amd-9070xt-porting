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

R10G10B10A2_UNORM and TYPELESS retain upstream's format fallback. Neither
is classified as a direct game-colour format. The addon converts both through
`NativeFormatConvert` to private FP16; codec consumers use private FP16 output
and retain `SourceView()` for typed source views. Disabling format fallback
rejects both formats. This PR does not add direct packed R10 write-back.

## Run

From an MSVC x64 developer shell at the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Development/test_codec_integration.ps1 -Amd
```

Omit `-Amd` for WARP only. Requires the upstream commit `297b032ac55f005d78568e684f30608651044f62` locally to
compile the original shaders as an independent compatibility reference.
No weights, HIP modules or game captures are required.

The test checks:

- Byte-for-byte default encode/decode compatibility, HDR and sRGB, nonunit
  pre-exposure and multiple ColorStrength values.
- Legacy RGBA8/BGRA8 (Magpie-style), R11G11B10 and UNORM16 packed output.
- Explicit pre-exposure and hue-safe opt-ins, and all four debug views with flags.
- Both R10 formats at 65x7 and 1920x1080, including boundary and alpha values:
  identical private FP16 decode against the unchanged upstream shader, rotating
  input rebinding, and disabled-fallback rejection in a separate process. The
  direct conversion agrees within one half ULP on AMD (alpha is exact); unlike
  the codec it does not perform the upstream luminance round-trip.

These are synthetic GPU tests, not Magpie or game visual validation. WARP and RX 9070 XT passed
on 2026-10-06; the D3D12 debug layer was unavailable on the test machine.
