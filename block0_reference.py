#!/usr/bin/env python3
"""Readable FP32 reference for the recovered DLSSNR block0 math.

This is intentionally a correctness oracle, not a fast implementation. It runs
the RGB-only ablation (the four generative-noise channels are zero) and writes
the first three block-output feature channels as a diagnostic image.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
from PIL import Image


ARCHIVE_FILE_OFFSET = 0x114A160


def load_record(
    dll: bytes, index: list[dict[str, object]], name: str
) -> np.ndarray:
    record = next(item for item in index if item["name"] == name)
    start = ARCHIVE_FILE_OFFSET + int(record["payload_offset"])
    return np.frombuffer(
        dll, dtype="<f2", count=int(record["element_count"]), offset=start
    )


def load_block0(
    dll: bytes, index: list[dict[str, object]], layout_path: Path
) -> dict[str, torch.Tensor]:
    values = load_record(dll, index, "block0.layer0.layer")
    layout = json.loads(layout_path.read_text(encoding="utf-8"))
    tensors: dict[str, torch.Tensor] = {}
    for item in layout["tensors"]:
        begin = int(item["element_offset"])
        if item["name"].endswith("padding"):
            continue
        if item.get("storage") == "float32_packed":
            count = int(np.prod(item["shape"]))
            packed = np.frombuffer(
                values[begin : begin + int(item["storage_half_count"])].tobytes(),
                dtype="<f4",
                count=count,
            ).copy()
            tensors[item["name"]] = torch.from_numpy(packed).reshape(item["shape"])
            continue
        end = begin + int(item["element_count"])
        tensors[item["name"]] = torch.from_numpy(
            values[begin:end].astype(np.float32)
        ).reshape(item["shape"])
    return tensors


def load_fused_block(
    dll: bytes,
    index: list[dict[str, object]],
    block: int,
    width: int,
    allowed_extra: int = 0,
) -> dict[str, torch.Tensor]:
    values = load_record(dll, index, f"block{block}.layer0.layer")
    heads = width // 32
    hidden = 64 if width == 32 else width + 32
    shapes: list[tuple[str, tuple[int, ...]]] = []
    if width != 32:
        shapes.append(("weight0", (width, width // 2)))
    shapes.extend(
        [
            ("weight1", (hidden, width)),
            ("weight2", (width, hidden)),
        ]
    )
    tensors: dict[str, torch.Tensor] = {}
    offset = 0
    for name, shape in shapes:
        count = int(np.prod(shape))
        tensors[name] = torch.from_numpy(
            values[offset : offset + count].astype(np.float32)
        ).reshape(shape)
        offset += count
    offset += 8
    tensors["ffn_cos_skip"] = torch.from_numpy(
        values[offset : offset + width].astype(np.float32)
    )
    offset += width
    qkv_count = 3 * (width // 2) * width
    tensors["qkv_weight"] = torch.from_numpy(
        values[offset : offset + qkv_count].astype(np.float32)
    ).reshape(3 * width // 2, width)
    offset += qkv_count
    bias_count = heads * 64 * 64
    tensors["attn_bias"] = torch.from_numpy(
        values[offset : offset + bias_count].astype(np.float32)
    ).reshape(heads, 64, 64)
    offset += bias_count
    scale = np.frombuffer(values[offset : offset + 16].tobytes(), dtype="<f4", count=heads).copy()
    tensors["attn_scale"] = torch.from_numpy(scale)
    offset += 16
    projection_count = width * (width // 2)
    tensors["projection_weight"] = torch.from_numpy(
        values[offset : offset + projection_count].astype(np.float32)
    ).reshape(width, width // 2)
    offset += projection_count
    tensors["attn_cos_skip"] = torch.from_numpy(
        values[offset : offset + width].astype(np.float32)
    )
    offset += width
    tail = values.size - offset
    padding = 16 if width == 256 else 8
    if tail not in (padding, padding + allowed_extra):
        raise ValueError(f"block{block}: unexplained fused tail {values.size - offset}")
    if tail == padding + allowed_extra and allowed_extra:
        tensors["_extra"] = torch.from_numpy(
            values[offset : values.size - padding].astype(np.float32)
        )
    return tensors


def fast_activation(value: torch.Tensor) -> torch.Tensor:
    value = value.clamp(-4.0, 4.0)
    gate = 0.447265625 - 0.055908203125 * value.abs()
    gate = 0.89453125 + value * gate
    return value * gate


def swin_block(
    feature: torch.Tensor, weight: dict[str, torch.Tensor], shifted: bool
) -> torch.Tensor:
    _, original_height, original_width, channels = feature.shape
    padded_height = (original_height + 7) & -8
    padded_width = (original_width + 7) & -8
    if padded_height != original_height or padded_width != original_width:
        feature = F.pad(
            feature.permute(0, 3, 1, 2),
            (0, padded_width - original_width, 0, padded_height - original_height),
        ).permute(0, 2, 3, 1)
    _, height, image_width, _ = feature.shape
    heads = channels // 32
    hidden = fast_activation(feature @ weight["weight1"].T)
    feature = hidden @ weight["weight2"].T + feature * weight["ffn_cos_skip"]

    qkv = feature @ weight["qkv_weight"].T
    q, k, v = (item.reshape(1, height, image_width, heads, 16) for item in qkv.chunk(3, dim=-1))
    q = F.normalize(q, dim=-1)
    k = F.normalize(k, dim=-1)
    if shifted:
        q, k, v = (torch.roll(item, shifts=(-4, -4), dims=(1, 2)) for item in (q, k, v))

    def to_windows(value: torch.Tensor) -> torch.Tensor:
        return (
            value.reshape(1, height // 8, 8, image_width // 8, 8, heads, 16)
            .permute(0, 1, 3, 5, 2, 4, 6)
            .reshape(-1, heads, 64, 16)
        )

    q_windows, k_windows, v_windows = map(to_windows, (q, k, v))
    logits = q_windows @ k_windows.transpose(-1, -2)
    logits = logits * weight["attn_scale"].reshape(1, heads, 1, 1)
    logits = logits + weight["attn_bias"].unsqueeze(0)
    attended = torch.softmax(logits, dim=-1) @ v_windows
    attended = (
        attended.reshape(1, height // 8, image_width // 8, heads, 8, 8, 16)
        .permute(0, 1, 4, 2, 5, 3, 6)
        .reshape(1, height, image_width, channels // 2)
    )
    if shifted:
        attended = torch.roll(attended, shifts=(4, 4), dims=(1, 2))
    projected = attended @ weight["projection_weight"].T
    output = projected + feature * weight["attn_cos_skip"]
    return output[:, :original_height, :original_width, :]


def block0(
    rgb: torch.Tensor, weight: dict[str, torch.Tensor], use_noise: bool
) -> torch.Tensor:
    _, _, height, width = rgb.shape
    if use_noise:
        generator = torch.Generator().manual_seed(0x297)
        noise = torch.randn((1, 4, height, width), dtype=rgb.dtype, generator=generator)
    else:
        noise = torch.zeros((1, 4, height, width), dtype=rgb.dtype)
    seven = torch.cat((rgb, noise), dim=1)

    adapter = torch.einsum("bihw,oi->bohw", seven, weight["input_adapter_weight"])
    feature = F.conv2d(
        adapter,
        weight["dw_weight"].unsqueeze(1),
        padding=1,
        groups=32,
    )

    return swin_block(feature.permute(0, 2, 3, 1), weight, shifted=False)


def downsample(
    feature: torch.Tensor,
    weight: dict[str, torch.Tensor],
    width: int,
) -> torch.Tensor:
    feature = swin_block(feature, weight, shifted=True)
    matrix_values = weight["_extra"]
    if matrix_values.numel() < width * width:
        matrix_values = F.pad(matrix_values, (0, width * width - matrix_values.numel()))
    matrix = matrix_values[: width * width].reshape(width, width)
    feature = feature @ matrix
    return F.avg_pool2d(feature.permute(0, 3, 1, 2), 2).permute(0, 2, 3, 1)


def enter_stage(feature: torch.Tensor, weight: dict[str, torch.Tensor]) -> torch.Tensor:
    if "weight0" in weight and feature.shape[-1] * 2 == weight["weight0"].shape[0]:
        feature = feature @ weight["weight0"].T
    return swin_block(feature, weight, shifted=False)


def save_feature(feature: torch.Tensor, path: Path) -> None:
    rgb = feature[0, :, :, :3].numpy()
    # A fixed linear diagnostic mapping; deliberately no per-image auto-level.
    rgb = np.clip(0.5 + rgb * 40.0, 0.0, 1.0)
    Image.fromarray(np.rint(rgb * 255.0).astype(np.uint8), "RGB").save(path)


def check_feature(label: str, feature: torch.Tensor) -> torch.Tensor:
    finite = torch.isfinite(feature)
    print(
        f"{label}: shape={tuple(feature.shape)} finite={finite.all().item()} "
        f"absmax={torch.nan_to_num(feature).abs().max().item():.8g}"
    )
    if not finite.all():
        raise FloatingPointError(f"{label} produced non-finite values")
    return feature


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("image", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--noise", action="store_true", help="enable the formal four Gaussian channels")
    parser.add_argument(
        "--end-block", type=int, choices=(3, 7, 13, 21), default=3,
        help="experimental encoder stopping point; stages past block3 still need tinlayout unswizzling",
    )
    parser.add_argument("--index", type=Path, default=Path(__file__).with_name("weights-index.json"))
    parser.add_argument(
        "--layout", type=Path, default=Path(__file__).with_name("block0-tensor-layout.json")
    )
    args = parser.parse_args()

    image = Image.open(args.image).convert("RGB").resize((256, 144), Image.Resampling.LANCZOS)
    rgb = torch.from_numpy(np.asarray(image, dtype=np.float32) / 255.0).permute(2, 0, 1).unsqueeze(0)
    dll = args.dll.read_bytes()
    index = json.loads(args.index.read_text(encoding="utf-8"))
    weight = load_block0(dll, index, args.layout)
    with torch.inference_mode():
        output = check_feature("block0", block0(rgb, weight, args.noise))
        for block, shifted in ((1, False), (2, True), (3, True)):
            output = check_feature(
                f"block{block}",
                swin_block(output, load_fused_block(dll, index, block, 32), shifted),
            )
        if args.end_block >= 7:
            output = check_feature("block4", downsample(
                output, load_fused_block(dll, index, 4, 32, allowed_extra=1024), 32
            ))
            output = check_feature("block5", enter_stage(output, load_fused_block(dll, index, 5, 64)))
            for block in (6, 7):
                output = check_feature(f"block{block}", swin_block(output, load_fused_block(dll, index, block, 64), shifted=True))
        if args.end_block >= 13:
            output = check_feature("block8", downsample(
                output, load_fused_block(dll, index, 8, 64, allowed_extra=4088), 64
            ))
            output = check_feature("block9", enter_stage(output, load_fused_block(dll, index, 9, 128)))
            for block, shifted in ((10, True), (11, True), (12, True), (13, False)):
                output = check_feature(f"block{block}", swin_block(output, load_fused_block(dll, index, block, 128), shifted))
        if args.end_block >= 21:
            output = check_feature("block14", downsample(
                output, load_fused_block(dll, index, 14, 128, allowed_extra=16376), 128
            ))
            output = check_feature("block15", enter_stage(output, load_fused_block(dll, index, 15, 256)))
            for block, shifted in (
                (16, True), (17, True), (18, True), (19, False), (20, True), (21, True)
            ):
                output = check_feature(f"block{block}", swin_block(output, load_fused_block(dll, index, block, 256), shifted))
    save_feature(output, args.output)
    print(f"shape: {tuple(output.shape)}")
    print(
        "feature range: "
        f"min={output.min().item():.8g} max={output.max().item():.8g} "
        f"mean={output.mean().item():.8g} std={output.std().item():.8g}"
    )
    print(f"wrote: {args.output}")


if __name__ == "__main__":
    main()
