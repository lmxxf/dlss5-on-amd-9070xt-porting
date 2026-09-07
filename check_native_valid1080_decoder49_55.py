"""Continue same-RGB block48 output through the original C256 decoder."""
from pathlib import Path
import json
import subprocess
import sys
import argparse

p = argparse.ArgumentParser()
p.add_argument('--base',type=Path,default=Path('release/native-rgb-valid1080'))
group = p.add_mutually_exclusive_group()
group.add_argument('--c128', action='store_true')
group.add_argument('--c64', action='store_true')
args = p.parse_args()
base = args.base
entry = 62 if args.c64 else 56 if args.c128 else 48
report = json.loads((base / f'upsample{entry}/validation.json').read_text())
assert report['different'] == 0 and report['finite'] and report['tail_zero']
previous = base / f'upsample{entry}/output.fp8'
root = base / ('decoder-c64' if args.c64 else 'decoder-c128' if args.c128 else 'decoder-c256')
blocks = range(63, 66) if args.c64 else range(57, 62) if args.c128 else range(49, 56)
shifts = (3, 1, 2) if args.c64 else (2, 0, 3, 1, 2) if args.c128 else (3, 1, 2, 0, 3, 1, 2)
for block, shift in zip(blocks, shifts):
    subprocess.run([sys.executable, 'check_native_decoder_c256.py',
                    '--block', str(block), '--shift', str(shift),
                    '--input', str(previous), '--output-root', str(root),
                    '--game-extent'], check=True)
    previous = root / f'decoder-block{block}/output.fp8'
