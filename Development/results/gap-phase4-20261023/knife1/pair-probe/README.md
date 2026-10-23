# ViT V-fragment producer/consumer pair probe

CPU-built only; no GPU/RTC run by this builder. Root owns guarded execution.

`pair-probe.exe MODULE_DIRECTORY BLOCK31_QKV_F32 OUTPUT_DIRECTORY [HIP_DEVICE=0]`

Executable: `/home/lmxxf/work/dlss5-gap-phase4-20261023/knife1/pair-probe.exe`. SHA and source/compiler identities are in `build-receipt.json`. It runs400 and640 tokens in one invocation. `MODULE_DIRECTORY` must contain candidate `vit-stream-fast.hsaco` and `deep_fast-packed-fast.hsaco`, with old/new exports both available. Input weights are the actual Original `block31-qkv.f32`, exactly3×1048576+32 float32 values; weights are read at runtime and never copied into Git or probe outputs.

Packing uses the production `ExactWeightFp8` directly and the exact `PackedVitQkvWeightFrag8` index loop (source expression checked identical), with32 f32 scales copied to the same packed offset. No approximate quantization is allowed. Input bytes are deterministic finite small E4M3 values of both signs, varying by token and channel.

For each geometry:

- producerold/new use96×ceil((n/16)/5) workgroups and160 threads;
- output QKV payload remains3×n×1024 bytes, with256-byte prefix/suffix canaries;
- Q/K planes are compared directly; V is independently inverse-mapped by token/head/channel to old order and compared byte-for-byte;
- corresponding old/new attention use2n workgroups and32 threads, same n; n×1024-byte AV outputs must match exactly with guards intact;
- output E4M3 nonfinite codes are rejected, input/packed weights read back unchanged;
- all four kernels are invoked again withreuse_gate1 and distinct nonzero output sentinels; complete buffers including guards must remain unchanged.

ABI and layout were confirmed with the kernel author. Each launch drains the private stream before dependent reads or resource cleanup. An uncertain drain exits without freeing potentially live GPU resources. There is no whole-network execution or timing claim.

The output directory contains each n's input/QKV/AV guarded raws, new-V inverse output, and receipt.json. `verify.py OUTPUT_DIRECTORY` is a separate Python-standard-library CPU readback check of the mapping/AV/guards and hashes; it runs no GPU work. Reuse-gate equality is checked by the GPU probe and recorded, with no separate reuse raw dumps. Module/exe/actual-weight SHA should be captured by root's ordinary job receipt.

This probe covers one controlled finite input and one real QKV weight tensor for400/640. It validates the representation/consumer pairing, not all dynamic game inputs or end-to-end network quality/performance.

## Head-major layout correction before GPU

The final V layout is `Vbase + (head*(tokens/16)+tile)*512 + lane*16 + j*8 + e`; for a fixed head, consecutive key tiles are512 bytes apart. Both the C++ inverse and independent Python inverse use this head-major order. The earlier tile-major draft was never GPU-executed; its build identity is preserved as `tile-major-unused-build-receipt.json`, and the normal executable path is rebuilt with the corrected layout. ABI and buffer capacity remain unchanged.

## Production loader correction

The first head-major host rejected the logical `.f32` path on CPU because only physical `block31-qkv.f16` exists; it never reached HIP initialization or a numerical GPU test. Preserve that host/identity. Use **`pair-probe-loader.exe`** for the new case, with the same logical `.f32` argument. It directly calls current `hip_reference::ReadWeights`, whose missing-f32 path expands the same-stem f16. Passing an existing `.f16` directly to that API would be interpreted as f32, so the probe explicitly requires the logical `.f32` suffix.

`build-loader-receipt.json` is the new build identity. Runtime receipt separately records actual resolved path, source file bytes (expected6291520 for f16), expanded float32 bytes12583040, and packed weight bytes3145856. Exact encoder/indexing and every GPU ABI/layout/check remain unchanged. Root binds the real f16 SHA in its job manifest; no weights were copied to this repository. `verify.py` now also checks these metadata sizes.
