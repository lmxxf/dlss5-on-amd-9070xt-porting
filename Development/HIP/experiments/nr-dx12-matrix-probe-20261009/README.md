# Native D3D12 matrix primitive support gate — 2026-10-09

This reuses this repository's `Development/HIP/hlsl_wmma_probe.hlsl` and
`hlsl_wmma_validate.cpp`; no external renderer implementation is copied.
Owned scratch: `/tmp/nr-dx12-matrix-probe-20261009`; AMD machine staging:
`D:\DLSSNR-Lab\nr-dx12-matrix-probe-20261009`.

Actual GPU execution handle **30509** completed with exit **0** and
`LOCK_RELEASED`. Standard game/RTC checks, atomic GPU lock, 15-second game
watchdog, 45-second own-process deadline, and 100-GB free-disk gate were used.
No game DLL or configuration was installed or changed.

Both actual RX 9070 XT probes queried Shader Model **0x6a (6.10)** successfully
and created their own compute PSO before dispatch. Each performs one dispatch
of five wave32 groups, with four matrix computation variants per case:
K32 MMA, two K16 MMAs, first K16 partial, and product followed by adding C.
The concrete operand tuples are FP8 E4M3FN or F16, A=16x32, B=32x16, F32
accumulator. The HLSL explicitly uses `dx::linalg::MatrixScope::Wave`; there is
no source-level scalar fallback. This does not constitute an ISA inspection.

Five exact small dyadic fixtures cover zero, first-K identity, second-K
nonasymmetric basis, and two signed nonasymmetric matrices. CPU double-precision
products are exact for these inputs. All 16 output arrays (two operand types,
four variants, F32 and F16 output) have **0 bit differences**, maximum absolute
error **0**, and no nonfinite outputs. Actual raw output remains in the scratch
and remote staging; the small numerical receipt and actual stdout/stderr are
under `results/`.

Toolchain CPU compilation handle **9437** completed with exit 0:

- Preview DXC/dxcompiler `1.10.2605.24`; `inc/hlsl/dx/linalg.h` is the actual header.
- Agility D3D12Core `1.721.3.0.20260730.2`, copied from the existing matrix-probe installation.
- Driver `32.0.31007.2048` and developer mode enabled were independently inspected.
- FP8 CSO SHA256 `6a4dbbef6eaca4b87311ac06876e518d7dad9f590ffb75cdb16f429f250ad3ba`.
- F16 CSO SHA256 `1c9e4447669167176ed3f40f53297a9cc14c881314cc915318c8a05a573cb5df`.
- Runner SHA256 `d591b4680f69a2567b5a696752a62e993d7f693ce40137fb6e76ae31a7c2ff34`.

This proves these concrete native D3D12 matrix tuples execute correctly on this
machine for the listed inputs. It does **not** prove arbitrary rounding/order
equivalence, full-network correctness, support on an already-created game
device, retail-driver compatibility, or performance. The existing wider
HIP/HLSL primitive corpus is the next numerical gate if new arithmetic paths
are adopted.
