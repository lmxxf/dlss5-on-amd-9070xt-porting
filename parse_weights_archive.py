#!/usr/bin/env python3
"""Parse the top-level record framing of DLSSNR's embedded WEIGHTS_HT archive.

This parser deliberately names only fields whose framing semantics can be
verified from the binary. Unknown record-body fields remain raw integers until
their meaning is supported by additional evidence.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from importlib.util import module_from_spec, spec_from_file_location


def load_pe_helpers() -> object:
    helper_path = Path(__file__).parents[1] / "296" / "analyze_dlssnr.py"
    spec = spec_from_file_location("analyze_dlssnr", helper_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load PE helpers from {helper_path}")
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def parse_archive(archive: bytes) -> list[dict[str, object]]:
    declared_size = u64(archive, 0)
    if declared_size != len(archive):
        raise ValueError(
            f"archive size mismatch: header={declared_size}, actual={len(archive)}"
        )

    records: list[dict[str, object]] = []
    cursor = 8
    while cursor < len(archive):
        record_offset = cursor
        name_length = u64(archive, cursor)
        cursor += 8
        if not 1 <= name_length <= 4096:
            raise ValueError(
                f"implausible name length {name_length} at archive offset 0x{record_offset:x}"
            )

        name_bytes = archive[cursor : cursor + name_length]
        if len(name_bytes) != name_length:
            raise ValueError(f"truncated name at archive offset 0x{record_offset:x}")
        try:
            name = name_bytes.decode("ascii")
        except UnicodeDecodeError as exc:
            raise ValueError(f"non-ASCII record name at 0x{record_offset:x}") from exc
        cursor += name_length

        body_span = u64(archive, cursor)
        body_offset = cursor + 8
        next_record_offset = body_offset + body_span
        if next_record_offset > len(archive):
            raise ValueError(
                f"record {name!r} overruns archive: next=0x{next_record_offset:x}"
            )
        if body_span < 20:
            raise ValueError(f"record {name!r} has implausible body span {body_span}")

        # These fields are stable enough to expose numerically, but their exact
        # semantics are intentionally not guessed yet.
        body_u64_0 = u64(archive, body_offset)
        payload_size = u64(archive, body_offset + 8)
        dtype_code = u32(archive, body_offset + 16)
        payload_offset = body_offset + 20
        trailer_offset = payload_offset + payload_size
        trailer_u32 = list(struct.unpack_from("<5I", archive, trailer_offset))
        element_count = trailer_u32[4]

        records.append(
            {
                "index": len(records),
                "name": name,
                "record_offset": record_offset,
                "name_length": name_length,
                "body_offset": body_offset,
                "body_span": body_span,
                "body_u64_0": body_u64_0,
                "payload_offset": payload_offset,
                "payload_size": payload_size,
                "dtype_code": dtype_code,
                "trailer_u32": trailer_u32,
                "element_count": element_count,
                "inferred_storage": "float16"
                if payload_size == element_count * 2
                else "unknown",
                "next_record_offset": next_record_offset,
            }
        )
        cursor = next_record_offset

    if cursor != len(archive):
        raise ValueError(f"parser stopped at 0x{cursor:x}, archive ends at 0x{len(archive):x}")
    return records


def build_aligned_arena(
    archive: bytes, records: list[dict[str, object]], alignment: int = 512
) -> tuple[bytes, list[dict[str, object]]]:
    """Rebuild the flat GPU-weight arena observed in the NVIDIA runtime."""
    if alignment <= 0 or alignment & (alignment - 1):
        raise ValueError("arena alignment must be a positive power of two")

    arena = bytearray()
    arena_records: list[dict[str, object]] = []
    for record in records:
        payload_offset = int(record["payload_offset"])
        payload_size = int(record["payload_size"])
        arena_offset = len(arena)
        arena.extend(archive[payload_offset : payload_offset + payload_size])
        aligned_size = (payload_size + alignment - 1) & -alignment
        arena.extend(b"\0" * (aligned_size - payload_size))
        arena_records.append(
            {
                "index": record["index"],
                "name": record["name"],
                "arena_offset": arena_offset,
                "payload_size": payload_size,
                "aligned_size": aligned_size,
            }
        )
    return bytes(arena), arena_records


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("--json", type=Path, help="write the parsed top-level index")
    parser.add_argument("--arena", type=Path, help="write the 512-byte-aligned FP16 arena")
    parser.add_argument(
        "--arena-json", type=Path, help="write record offsets in the aligned arena"
    )
    args = parser.parse_args()

    helpers = load_pe_helpers()
    dll = args.dll.read_bytes()
    _, resource_rva, resource_offset, _ = helpers.read_pe(dll)
    leaves = helpers.resource_leaves(dll, resource_rva, resource_offset)
    matches = [leaf for leaf in leaves if leaf[0].startswith("10/WEIGHTS_HT/")]
    if len(matches) != 1:
        raise ValueError(f"expected one WEIGHTS_HT resource, found {len(matches)}")
    _, archive_size, archive_offset = matches[0]
    archive = dll[archive_offset : archive_offset + archive_size]
    records = parse_archive(archive)

    print(f"archive file offset: 0x{archive_offset:x}")
    print(f"archive size: {archive_size}")
    print(f"records parsed: {len(records)}")
    print(f"closed at archive end: yes")
    print(f"first record: {records[0]['name']}")
    print(f"last record: {records[-1]['name']}")
    print(f"dtype code values: {sorted({record['dtype_code'] for record in records})}")
    print(f"body_span == body_u64_0: {sum(record['body_span'] == record['body_u64_0'] for record in records)}/{len(records)}")
    print(f"body_span == payload_size + 40: {sum(record['body_span'] == record['payload_size'] + 40 for record in records)}/{len(records)}")
    print(f"payload_size == element_count * 2: {sum(record['payload_size'] == record['element_count'] * 2 for record in records)}/{len(records)}")
    print(f"total elements: {sum(record['element_count'] for record in records)}")
    print("first five records:")
    for record in records[:5]:
        print(
            f"  {record['index']:3}: {record['name']:<32} "
            f"span={record['body_span']:<10} payload={record['payload_size']:<10} "
            f"dtype_code={record['dtype_code']} elements={record['element_count']}"
        )

    if args.json:
        args.json.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
        print(f"wrote: {args.json}")

    if args.arena or args.arena_json:
        arena, arena_records = build_aligned_arena(archive, records)
        if args.arena:
            args.arena.write_bytes(arena)
            print(f"arena alignment: 512")
            print(f"arena size: {len(arena)}")
            print(f"wrote: {args.arena}")
        if args.arena_json:
            manifest = {
                "alignment": 512,
                "arena_size": len(arena),
                "records": arena_records,
            }
            args.arena_json.write_text(
                json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
            )
            print(f"wrote: {args.arena_json}")


if __name__ == "__main__":
    main()
