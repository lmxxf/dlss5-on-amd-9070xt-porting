#!/usr/bin/env python3
"""Build the recovered DLSSNR block skeleton from decompiled CPU factories.

The block/type sequence is supported by the descriptor builder at 0x180039780
and its layer helper functions. Input indices and exact skip edges are left
unknown until the descriptor's edge vectors are decoded.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def add_range(
    graph: list[dict[str, object]],
    start: int,
    end: int,
    *,
    block_type: str,
    layer_type: str,
    width: int,
    role: str,
    variant: str = "body",
) -> None:
    for block in range(start, end + 1):
        graph.append(
            {
                "block": block,
                "block_type": block_type,
                "layer_type": layer_type,
                "width": width,
                "role": role,
                "variant": variant,
                "inputs": None,
            }
        )


def recovered_skeleton() -> list[dict[str, object]]:
    graph: list[dict[str, object]] = []
    add_range(graph, 0, 0, block_type="single_layer_block", layer_type="CCTinlayoutFusedPreBlockSwin1H", width=32, role="input", variant="pre")
    add_range(graph, 1, 3, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin1H", width=32, role="encoder")
    add_range(graph, 4, 4, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin1H", width=32, role="encoder", variant="downsample_to_64")
    add_range(graph, 5, 7, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin2H", width=64, role="encoder")
    add_range(graph, 8, 8, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin2H", width=64, role="encoder", variant="downsample_to_128")
    add_range(graph, 9, 13, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin4H", width=128, role="encoder")
    add_range(graph, 14, 14, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin4H", width=128, role="encoder", variant="downsample_to_256")
    add_range(graph, 15, 21, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin8H", width=256, role="encoder")
    add_range(graph, 22, 22, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin8H", width=256, role="encoder", variant="downsample_to_512")
    add_range(graph, 23, 29, block_type="cc_split_swin_16h_block", layer_type="CCSplitSwin16H*", width=512, role="encoder")
    add_range(graph, 30, 30, block_type="cc_split_swin_16h_block", layer_type="CCSplitSwin16H*", width=512, role="encoder", variant="terminal")
    add_range(graph, 31, 38, block_type="cc_vit_1d_block", layer_type="CCVit1D*", width=1024, role="bottleneck")
    add_range(graph, 39, 39, block_type="single_layer_block", layer_type="CCDecInputUpsample", width=512, role="decoder", variant="upsample_1024_to_512")
    add_range(graph, 40, 47, block_type="cc_split_swin_16h_block", layer_type="CCSplitSwin16H*", width=512, role="decoder")
    add_range(graph, 48, 48, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin8H", width=256, role="decoder", variant="upsample_512_to_256")
    add_range(graph, 49, 55, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin8H", width=256, role="decoder")
    add_range(graph, 56, 56, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin4H", width=128, role="decoder", variant="upsample_256_to_128")
    add_range(graph, 57, 61, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin4H", width=128, role="decoder")
    add_range(graph, 62, 62, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin2H", width=64, role="decoder", variant="upsample_128_to_64")
    add_range(graph, 63, 65, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin2H", width=64, role="decoder")
    add_range(graph, 66, 66, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin1H", width=32, role="decoder", variant="upsample_64_to_32")
    add_range(graph, 67, 69, block_type="single_layer_block", layer_type="CCTinlayoutFusedSwin1H", width=32, role="decoder")
    add_range(graph, 70, 70, block_type="single_layer_block", layer_type="CCTinlayoutFusedPostBlockSwin1H", width=32, role="output", variant="post_blend")
    assert [entry["block"] for entry in graph] == list(range(71))

    # Ordinary blocks consume output 0 of the immediately preceding block.
    # The descriptor builder explicitly overrides six U-Net skip sites with
    # two source-block vectors and matching source-output vectors.
    graph[0]["inputs"] = None
    for block in range(1, 71):
        graph[block]["inputs"] = [{"source_block": block - 1, "source_output": 0}]
    skip_edges = {
        39: ((38, 0), (30, 1)),
        48: ((47, 0), (22, 1)),
        56: ((55, 0), (14, 1)),
        62: ((61, 0), (8, 1)),
        66: ((65, 0), (4, 1)),
        70: ((69, 0), (0, 1)),
    }
    for block, sources in skip_edges.items():
        graph[block]["inputs"] = [
            {"source_block": source_block, "source_output": source_output}
            for source_block, source_output in sources
        ]
    split_body = [
        "CCSplitSwin16HFfwd",
        "CCSplitSwin16HFfwdProj",
        "CCSplitSwin16HQKVAttn",
        "CCSplitSwin16HProj",
    ]
    split_terminal = [
        "CCSplitSwin16HFfwd",
        "CCSplitSwin16HFfwdProj",
        "CCSplitSwin16HQKVAttn",
        "CCSplitSwin16HProjPool",
        "CCSplitSwin16HFinalHead",
    ]
    vit_1d = [
        "CCVit1DFfnExpand",
        "CCVit1DFfnContract",
        "CCVit1DQKV",
        "CCVit1DAttention",
        "CCVit1DProjection",
    ]
    for entry in graph:
        block = int(entry["block"])
        if 23 <= block <= 29 or 40 <= block <= 47:
            entry["layer_types"] = split_body
        elif block == 30:
            entry["layer_types"] = split_terminal
        elif 31 <= block <= 38:
            entry["layer_types"] = vit_1d
        else:
            entry["layer_types"] = [entry["layer_type"]]
    return graph


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("weights_index", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    records = json.loads(args.weights_index.read_text(encoding="utf-8"))
    by_block: dict[int, list[dict[str, object]]] = {block: [] for block in range(71)}
    pattern = re.compile(r"block(\d+)\.")
    for record in records:
        match = pattern.match(str(record["name"]))
        if match is None:
            raise ValueError(f"unrecognized weight record name: {record['name']}")
        by_block[int(match.group(1))].append(record)

    graph = recovered_skeleton()
    for entry in graph:
        block = int(entry["block"])
        block_records = sorted(by_block[block], key=lambda record: str(record["name"]))
        entry["weight_records"] = [record["name"] for record in block_records]
        entry["weight_elements"] = sum(int(record["element_count"]) for record in block_records)

    document = {
        "sample_sha256": "e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e",
        "config": "hnet-vigilant-squid",
        "architecture_variant": "crazy-cuckoo",
        "descriptor_builder": "0x180039780",
        "boundary": "Block types, order, and block-to-block edges are recovered; block 0 external texture bindings remain unresolved.",
        "blocks": graph,
    }
    args.output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(graph)} blocks to {args.output}")


if __name__ == "__main__":
    main()
