"""Read-only block39 archive/SASS audit; does not execute a GPU kernel."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dll', type=Path, default=Path('/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll'))
p.add_argument('--cubin', type=Path, default=Path('/tmp/dlssnr-cubins/dlssnr-06.cubin'))
p.add_argument('--cuobjdump', default='/usr/local/cuda/bin/cuobjdump')
a = p.parse_args()
records = json.loads(Path(__file__).with_name('weights-index.json').read_text())
r = next(x for x in records if x['name'] == 'block39.layer0.layer')
base = 0x114a160
with a.dll.open('rb') as f:
    f.seek(base)
    size = struct.unpack('<Q', f.read(8))[0]
    if base + size > a.dll.stat().st_size or r['payload_offset'] + r['payload_size'] > size:
        raise ValueError('archive bounds')
    f.seek(base + r['payload_offset'])
    raw = f.read(r['payload_size'])
if len(raw) != 525312 or r['element_count'] != 262656:
    raise ValueError('unexpected block39 record; re-audit required')
sass = subprocess.run([a.cuobjdump, '--dump-sass', str(a.cubin)],
                      check=True, capture_output=True, text=True, timeout=15).stdout
name = 'cc_dec_input_upsample_1024_512_fp8'
parts = re.split(r'Function\s*:\s*', sass)
matches = [s for s in parts if s.splitlines()[0].strip() == name]
if len(matches) != 1:
    raise ValueError('expected exactly one non-tilesync decoder entry')
body = matches[0]
ops = sorted(set(re.findall(r'\b(?:Q|H)MMA\.[A-Z0-9.]+', body)))
if 'QMMA.16832.F16.E4M3.E4M3' not in ops:
    raise ValueError('expected FP8 matrix instruction missing')
tail_loads = [line.split(';')[0].strip() for line in body.splitlines()
              if 'LDG.' in line and '+0x80000]' in line]
if not tail_loads:
    raise ValueError('expected separate tail loads missing')
print(json.dumps({
    'status': 'static_audit_pass', 'kernel': name,
    'payload_bytes': len(raw), 'container_element_count': r['element_count'],
    'payload_sha256': hashlib.sha256(raw).hexdigest(),
    'matrix_instructions': ops,
    'separate_tail_loads': tail_loads,
    'candidate_storage_split': {
        'matrix_bytes': 0x80000, 'matrix_fp8_shape_candidate': [512, 1024],
        'tail_bytes': len(raw) - 0x80000,
        'tail_half_count_if_fp16': (len(raw) - 0x80000) // 2,
        'status': 'address-supported candidate; logical permutation and tail role unverified',
    },
    'constant_load_offsets': sorted(set(re.findall(r'c\[0x0\]\[(0x[0-9a-f]+)\]', body))),
    'unverified': ['matrix byte layout and tail semantics', 'input/skip/output layouts',
                   'launch geometry', 'numerical equivalence', 'final game image'],
}, indent=2))
