import argparse
import hashlib
import json
import struct
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('bundle', type=Path)
p.add_argument('manifest', type=Path)
a = p.parse_args()
raw = memoryview(a.bundle.read_bytes())
assert raw[:8] == b'DLSS5PK1'
count, = struct.unpack_from('<I', raw, 8)
manifest = json.loads(a.manifest.read_text(encoding='utf-8-sig'))
expected = {entry['name']: entry for entry in manifest}
assert len(expected) == len(manifest) == count
offset = 12
seen = set()
for _ in range(count):
    length, size = struct.unpack_from('<HQ', raw, offset)
    offset += 10
    name = bytes(raw[offset:offset + length]).decode('ascii')
    offset += length
    assert name not in seen and name in expected
    seen.add(name)
    assert size == expected[name]['bytes'] and offset + size <= len(raw)
    digest = hashlib.sha256(raw[offset:offset + size]).hexdigest()
    assert digest.lower() == expected[name]['sha256'].lower(), name
    offset += size
assert offset == len(raw)
print(f'verified {count} assets, {len(raw)} bytes, no trailing data')
