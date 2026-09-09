#!/usr/bin/env python3
"""Compare sampled DirectML FP16 GEMM output with the existing FP32-dot ViT path."""
import argparse
import numpy as np


def fp8_round(x: np.ndarray) -> np.ndarray:
    out = np.zeros_like(x, dtype=np.float32)
    finite = np.isfinite(x)
    a = np.abs(x[finite]).astype(np.float32)
    sign = np.sign(x[finite]).astype(np.float32)
    small = a < np.float32(0.015625)
    y = np.empty_like(a)
    y[small] = np.rint(a[small] * 512.0) / 512.0
    large = ~small
    exponent = np.clip(np.floor(np.log2(a[large])), -6.0, 8.0)
    mantissa = np.rint((a[large] / np.exp2(exponent) - 1.0) * 8.0)
    carry = mantissa >= 8.0
    exponent[carry] += 1.0
    mantissa[carry] = 0.0
    y[large] = np.minimum(np.exp2(exponent) * (1.0 + mantissa / 8.0), 448.0)
    out[finite] = sign * y
    return out


def metrics(name: str, got: np.ndarray, ref: np.ndarray) -> None:
    delta = got.astype(np.float64) - ref.astype(np.float64)
    corr = np.corrcoef(got.ravel(), ref.ravel())[0, 1]
    print(f"{name}: correlation={corr:.9f} mae={np.mean(np.abs(delta)):.9g} "
          f"rmse={np.sqrt(np.mean(delta * delta)):.9g} max={np.max(np.abs(delta)):.9g}")


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("input_f32")
    p.add_argument("weight_f16")
    p.add_argument("directml_output_f16")
    p.add_argument("--m", type=int, default=2160)
    p.add_argument("--k", type=int, default=1024)
    p.add_argument("--n", type=int, default=4096)
    p.add_argument("--tokens", type=int, nargs="*", default=[0, 1, 100, 1000, 2159])
    a = p.parse_args()
    source = np.fromfile(a.input_f32, dtype="<f4").reshape(a.m, a.k)
    weight = np.fromfile(a.weight_f16, dtype="<f2").reshape(a.k, a.n)
    got = np.fromfile(a.directml_output_f16, dtype="<f2").reshape(a.m, a.n)
    rows = np.asarray(a.tokens, dtype=np.int64)
    source_f16 = source[rows].astype(np.float16).astype(np.float32)
    weight_f32 = weight.astype(np.float32)
    ref_same_inputs = source_f16 @ weight_f32
    ref_old_shader = source[rows] @ weight_f32
    got_rows = got[rows].astype(np.float32)
    metrics("raw_vs_fp16_input_fp32_accum", got_rows, ref_same_inputs)
    metrics("raw_vs_old_fp32_input_shader", got_rows, ref_old_shader)
    got_post = fp8_round(got_rows)
    same_post = fp8_round(ref_same_inputs)
    old_post = fp8_round(ref_old_shader)
    metrics("post_fp8_vs_same_inputs", got_post, same_post)
    metrics("post_fp8_vs_old_shader", got_post, old_post)
    print(f"post_fp8_exact_same_inputs={np.mean(got_post == same_post):.9f} "
          f"post_fp8_exact_old_shader={np.mean(got_post == old_post):.9f} samples={got_rows.size}")


if __name__ == "__main__":
    main()
