#!/usr/bin/env python3
"""Compare two HIP module sets by code, not by file hash.

Every build embeds a random __hip_cuid_<hex> symbol, so a rebuilt .hsaco never matches the shipped one byte for byte.
This compares the sections that matter (.note kernel metadata, .rodata kernel descriptors, .text code).

usage: python3 hip/compare-modules.py <built-dir> <reference-dir>
       each dir holds gfx1200/ and gfx1201/ (or .hsaco files directly); exit 1 if any module differs or is missing.
"""
import struct, sys
from pathlib import Path

SECTIONS = (b'.note', b'.rodata', b'.text')


def sections(path):
    d = path.read_bytes()
    shoff, = struct.unpack_from('<Q', d, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x3A)
    hdr = [struct.unpack_from('<IIQQQQ', d, shoff + i * shentsize) for i in range(shnum)]
    names = hdr[shstrndx][4]
    out = {}
    for name, typ, _, _, off, size in hdr:
        n = d[names + name:d.index(b'\0', names + name)]
        if n in SECTIONS:
            out[n] = d[off:off + size] if typ != 8 else b''
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    built, ref = Path(sys.argv[1]), Path(sys.argv[2])
    same = bad = 0
    for f in sorted(built.rglob('*.hsaco')):
        r = ref / f.relative_to(built)
        if not r.exists():
            print('MISSING', r); bad += 1; continue
        a, b = sections(f), sections(r)
        diff = [n.decode() for n in SECTIONS if a.get(n) != b.get(n)]
        if diff:
            print('DIFF', f.relative_to(built), ' '.join(diff)); bad += 1
        else:
            same += 1
    print(f'identical code: {same}  differ: {bad}')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
