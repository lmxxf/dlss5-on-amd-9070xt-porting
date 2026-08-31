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


def load_block0(dll_path: Path, index_path: Path, layout_path: Path) -> dict[str, torch.Tensor]:
    index = json.loads(index_path.read_text(encoding="utf-8"))
    record = next(item for item in index if item["name"] == "block0.layer0.layer")
    dll = dll_path.read_bytes()
    start = ARCHIVE_FILE_OFFSET + int(record["payload_offset"])
    values = np.frombuffer(
        dll, dtype="<f2", count=int(record["element_count"]), offset=start
    ).astype(np.float32)
    layout = json.loads(layout_path.read_text(encoding="utf-8"))
    tensors: dict[str, torch.Tensor] = {}
    for item in layout["tensors"]:
        begin = int(item["element_offset"])
        end = begin + int(item["element_count"])
        tensors[item["name"]] = torch.from_numpy(values[begin:end].copy()).reshape(item["shape"])
    return tensors


def fast_activation(value: torch.Tensor) -> torch.Tensor:
    value = value.clamp(-4.0, 4.0)
    gate = 0.447265625 - 0.055908203125 * value.abs()
    gate = 0.89453125 + value * gate
    return value * gate


def block0(rgb: torch.Tensor, weight: dict[str, torch.Tensor]) -> torch.Tensor:
    _, _, height, width = rgb.shape
    noise = torch.zeros((1, 4, height, width), dtype=rgb.dtype)
    seven = torch.cat((rgb, noise), dim=1)

    adapter = torch.einsum("bihw,oi->bohw", seven, weight["input_adapter_weight"])
    feature = F.conv2d(
        adapter,
        weight["dw_weight"].unsqueeze(1),
        padding=1,
        groups=32,
    )

    channels_last = feature.permute(0, 2, 3, 1)
    hidden = fast_activation(channels_last @ weight["weight1"].T)
    ffn = hidden @ weight["weight2"].T
    feature = ffn + channels_last * weight["ffn_cos_skip"]

    qkv = feature @ weight["qkv_weight"].T
    q, k, v = qkv.chunk(3, dim=-1)
    q = F.normalize(q * weight["attn_scale"][:16], dim=-1)
    k = F.normalize(k * weight["attn_scale"][16:], dim=-1)

    def to_windows(value: torch.Tensor) -> torch.Tensor:
        return (
            value.reshape(1, height // 8, 8, width // 8, 8, 16)
            .permute(0, 1, 3, 2, 4, 5)
            .reshape(-1, 64, 16)
        )

    q_windows, k_windows, v_windows = map(to_windows, (q, k, v))
    logits = q_windows @ k_windows.transpose(-1, -2)
    logits = logits + weight["attn_bias"].unsqueeze(0)
    attended = torch.softmax(logits, dim=-1) @ v_windows
    attended = (
        attended.reshape(1, height // 8, width // 8, 8, 8, 16)
        .permute(0, 1, 3, 2, 4, 5)
        .reshape(1, height, width, 16)
    )
    projected = attended @ weight["projection_weight"].T
    return projected + feature * weight["attn_cos_skip"]


def save_feature(feature: torch.Tensor, path: Path) -> None:
    rgb = feature[0, :, :, :3].numpy()
    # A fixed linear diagnostic mapping; deliberately no per-image auto-level.
    rgb = np.clip(0.5 + rgb * 40.0, 0.0, 1.0)
    Image.fromarray(np.rint(rgb * 255.0).astype(np.uint8), "RGB").save(path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("image", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--index", type=Path, default=Path(__file__).with_name("weights-index.json"))
    parser.add_argument(
        "--layout", type=Path, default=Path(__file__).with_name("block0-tensor-layout.json")
    )
    args = parser.parse_args()

    image = Image.open(args.image).convert("RGB").resize((256, 144), Image.Resampling.LANCZOS)
    rgb = torch.from_numpy(np.asarray(image, dtype=np.float32) / 255.0).permute(2, 0, 1).unsqueeze(0)
    weight = load_block0(args.dll, args.index, args.layout)
    with torch.inference_mode():
        output = block0(rgb, weight)
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
