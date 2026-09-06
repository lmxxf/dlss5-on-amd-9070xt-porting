"""Continue same-RGB block48 output through the original C256 decoder."""
from pathlib import Path
import json
import subprocess
import sys

base = Path('release/native-rgb-valid1080')
report = json.loads((base / 'upsample48/validation.json').read_text())
assert report['different'] == 0 and report['finite'] and report['tail_zero']
previous = base / 'upsample48/output.fp8'
root = base / 'decoder-c256'
for block, shift in zip(range(49, 56), (3, 1, 2, 0, 3, 1, 2)):
    subprocess.run([sys.executable, 'check_native_decoder_c256.py',
                    '--block', str(block), '--shift', str(shift),
                    '--input', str(previous), '--output-root', str(root),
                    '--game-extent'], check=True)
    previous = root / f'decoder-block{block}/output.fp8'
