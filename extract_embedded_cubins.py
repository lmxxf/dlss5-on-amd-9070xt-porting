#!/usr/bin/env python3
"""Extract standalone CUDA ELF/CUBIN images embedded as plain DLL data."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


ELF_MAGIC = b"\x7fELF"
SHT_NOBITS = 8


def cubin_size(data: bytes, start: int) -> tuple[int, int]:
    if data[start : start + 4] != ELF_MAGIC:
        raise ValueError(f"missing ELF magic at 0x{start:x}")
    if data[start + 4] != 2 or data[start + 5] != 1:
        raise ValueError(f"expected ELF64 little-endian image at 0x{start:x}")

    program_offset = struct.unpack_from("<Q", data, start + 0x20)[0]
    section_offset = struct.unpack_from("<Q", data, start + 0x28)[0]
    header_size, program_entry_size, program_count, section_entry_size, section_count = struct.unpack_from(
        "<HHHHH", data, start + 0x34
    )
    end = max(
        header_size,
        program_offset + program_entry_size * program_count,
        section_offset + section_entry_size * section_count,
    )
    for index in range(section_count):
        entry = start + section_offset + index * section_entry_size
        section_type = struct.unpack_from("<I", data, entry + 4)[0]
        payload_offset, payload_size = struct.unpack_from("<QQ", data, entry + 0x18)
        if section_type != SHT_NOBITS:
            end = max(end, payload_offset + payload_size)
    return end, section_count


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("output_directory", type=Path)
    args = parser.parse_args()

    data = args.dll.read_bytes()
    starts: list[int] = []
    cursor = 0
    while True:
        cursor = data.find(ELF_MAGIC, cursor)
        if cursor < 0:
            break
        starts.append(cursor)
        cursor += len(ELF_MAGIC)

    args.output_directory.mkdir(parents=True, exist_ok=True)
    manifest: list[dict[str, object]] = []
    for index, start in enumerate(starts):
        size, section_count = cubin_size(data, start)
        image = data[start : start + size]
        output = args.output_directory / f"dlssnr-{index:02}.cubin"
        output.write_bytes(image)
        manifest.append(
            {
                "index": index,
                "dll_offset": start,
                "size": size,
                "section_count": section_count,
                "sha256": hashlib.sha256(image).hexdigest(),
                "file": output.name,
            }
        )

    manifest_path = args.output_directory / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"extracted {len(manifest)} CUBIN images to {args.output_directory}")
    for item in manifest:
        print(
            f"  {item['index']:02}: offset=0x{item['dll_offset']:x} "
            f"size={item['size']} sections={item['section_count']}"
        )


if __name__ == "__main__":
    main()
