#!/usr/bin/env python3
"""Validate and characterize a captured DLSSNR runtime-packed weight arena."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("runtime", type=Path)
    parser.add_argument("archive", type=Path)
    parser.add_argument("index", type=Path)
    args = parser.parse_args()
    runtime = args.runtime.read_bytes()
    archive = args.archive.read_bytes()
    document = json.loads(args.index.read_text(encoding="utf-8"))
    records = document["records"] if isinstance(document, dict) else document
    expected = int(document.get("arena_size", 147719680)) if isinstance(document, dict) else 147719680
    if len(runtime) != expected or len(archive) != expected:
        raise ValueError(f"arena size mismatch runtime={len(runtime)} archive={len(archive)} expected={expected}")
    exact_records = 0
    changed_bytes = 0
    report = []
    for record in records:
        begin = int(record["arena_offset"])
        size = int(record["payload_size"])
        runtime_payload = runtime[begin:begin + size]
        archive_payload = archive[begin:begin + size]
        differences = int(np.count_nonzero(
            np.frombuffer(runtime_payload, dtype=np.uint8) !=
            np.frombuffer(archive_payload, dtype=np.uint8)))
        exact_records += differences == 0
        changed_bytes += differences
        report.append({
            "name": record["name"],
            "offset": begin,
            "size": size,
            "changed_bytes": differences,
            "runtime_sha256": hashlib.sha256(runtime_payload).hexdigest(),
            "archive_sha256": hashlib.sha256(archive_payload).hexdigest(),
        })
    summary = {
        "arena_size": expected,
        "runtime_sha256": hashlib.sha256(runtime).hexdigest(),
        "archive_sha256": hashlib.sha256(archive).hexdigest(),
        "record_count": len(records),
        "exact_records": exact_records,
        "changed_payload_bytes": changed_bytes,
        "records": report,
    }
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
