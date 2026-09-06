"""Continue the same RGB original decoder39 through split40..47."""
from pathlib import Path
import json
import subprocess
import sys

base = Path('release/native-rgb-valid1080')
report = json.loads((base / 'decoder39/validation.json').read_text())
assert report['different'] == 0 and report['finite'] and report['tail_zero']
previous = base / 'decoder39/result.output.fp8'
root = base / 'decoder-split'
for block, shift in zip(range(40, 48), (0, 3, 1, 2, 0, 3, 1, 2)):
    subprocess.run([sys.executable, 'check_native_decoder_split.py',
                    '--block', str(block), '--shift', str(shift),
                    '--input', str(previous), '--output-root', str(root),
                    '--game-extent'], check=True)
    previous = root / f'decoder-block{block}/output.fp8'
