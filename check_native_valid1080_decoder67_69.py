"""Continue same-RGB block66 through original C32 decoder67..69."""
from pathlib import Path
import json
import subprocess
import sys
import argparse

p=argparse.ArgumentParser();p.add_argument('--base',type=Path,default=Path('release/native-rgb-valid1080'));args=p.parse_args()
base = args.base
r = json.loads((base / 'upsample66/validation.json').read_text())
assert r['different'] == 0 and r['finite'] and r['tail_zero']
previous = base / 'upsample66/output.fp8'
root = base / 'decoder-c32'
for block, shift in zip(range(67, 70), (3, 1, 2)):
    subprocess.run([sys.executable, 'check_native_decoder_c32.py', '--block', str(block),
                    '--shift', str(shift), '--input', str(previous),
                    '--output-root', str(root), '--game-extent'], check=True)
    previous = root / f'decoder-block{block}/output.fp8'
