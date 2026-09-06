"""One bounded original decoder call, not a port acceptance test."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--case', choices=['zero', 'main', 'skip'], default='zero')
a = p.parse_args()
base = Path(__file__).resolve().parent
root = Path(tempfile.mkdtemp(prefix='smoke-', dir=base / 'release'))
report = {'status': 'running', 'scope': 'original kernel smoke only', 'case': a.case,
          'directory': str(root), 'width': 8, 'height': 8}
def save():
    (root / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
save()
try:
    (root / 'main.fp8').write_bytes(bytes([0x30 if a.case == 'main' else 0]) * (8 * 8 * 1024))
    (root / 'skip.fp8').write_bytes(bytes([0x30 if a.case == 'skip' else 0]) * (16 * 16 * 512))
    subprocess.run(['python3', str(base / 'extract_native_weight_record.py'),
                    '/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',
                    'block39.layer0.layer', str(root / 'weights.bin')], check=True, timeout=5)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra',
                    str(base / 'run_native_decoder_entry_probe.cpp'),
                    '-I/usr/local/cuda/include', '-L/usr/local/cuda/lib64', '-lcuda',
                    '-o', str(root / 'probe')], check=True, timeout=10)
    args = [str(root / 'probe'), '/tmp/dlssnr-cubins/dlssnr-06.cubin',
            str(root / 'main.fp8'), str(root / 'skip.fp8'), str(root / 'weights.bin'),
            str(root / 'result'), '8', '8']
    subprocess.run(args, check=True, timeout=5)
    # External timeout bounds host process observation, not GPU recovery.
    result = subprocess.run(['timeout', '--signal=TERM', '--kill-after=2s', '15s',
                             *args, '--run'], capture_output=True, text=True, timeout=20)
    (root / 'stdout.txt').write_text(result.stdout)
    (root / 'stderr.txt').write_text(result.stderr)
    print(result.stdout, end=''); print(result.stderr, end='')
    report['returncode'] = result.returncode
    if result.returncode:
        raise RuntimeError(f'probe returned {result.returncode}')
    output = (root / 'result.output.fp8').read_bytes()
    counters = struct.unpack('<8i', (root / 'result.counters.i32').read_bytes()[:32])
    report['partition_counters'] = counters
    if counters != (3,) * 8:
        raise RuntimeError('not all tile counters reached Z3')
    report.update(output_bytes=len(output), nonzero=sum(x != 0 for x in output),
                  nan_count=sum((x & 127) == 127 for x in output),
                  output_sha256=hashlib.sha256(output).hexdigest())
    if len(output) != 2 * 1024 * 1024 or report['nan_count']:
        raise RuntimeError('output is nonfinite or wrong length')
    if bool(any(output)) != (a.case != 'zero'):
        raise RuntimeError('zero/nonzero input influence check failed')
    report['status'] = 'smoke_pass'
except Exception as error:
    report.update(status='fail', error=str(error))
    raise
finally:
    save()
    print(json.dumps(report, indent=2))
