"""Separate per-submit GPU intervals from host wall time; excludes readback.

Run analyze_native_network_profile.py first for independent output validation.
Timestamp intervals include GPU memory/barriers, not just arithmetic. The
remaining wall time includes recording, scheduling, fence waits and logging.
"""
import argparse
import json
import re
import statistics
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--root', type=Path, required=True)
args = p.parse_args()
validated = json.loads((args.root / 'profile-validation.json').read_text())
pending, frames = [], []
for line in (args.root / 'network.stdout.log').read_text().splitlines():
    m = re.fullmatch(r'submission_timing queue=(\S+) id=(\d+) gpu_ms=([\d.]+) wall_ms=([\d.]+)', line)
    if m:
        pending.append(dict(queue=m[1], id=int(m[2]), gpu=float(m[3]), wall=float(m[4])))
    m = re.fullmatch(r'network70 frame=(\d+) single_list=0 submit_wait_ms=([\d.]+)', line)
    if m:
        assert pending
        # Initialization upload queues precede frame zero. The final queue is
        # the test's dedicated network queue; subsequent frames reuse it.
        queue = pending[-1]['queue']
        rows = [r for r in pending if r['queue'] == queue]
        assert all(b['id'] == a['id'] + 1 for a, b in zip(rows, rows[1:]))
        wall = float(m[2])
        gpu = sum(r['gpu'] for r in rows)
        assert wall >= gpu
        frames.append(dict(frame=int(m[1]), count=len(rows), gpu_ms=gpu,
                           wall_ms=wall, gap_ms=wall-gpu,
                           submit_wall_ms=sum(r['wall'] for r in rows)))
    if re.fullmatch(r'network70 frame=\d+ history=\d+ values=6635520 different=0', line):
        pending = []  # Also discard that frame's CPU-validation readback copy.
assert len(frames) == len(validated['frames']) >= 5
assert [f['frame'] for f in frames] == list(range(len(frames)))
assert len({f['count'] for f in frames}) == 1
report = dict(scope=__doc__.strip(), frames=frames,
              warm={k: statistics.mean(f[k] for f in frames[1:])
                    for k in ['count', 'gpu_ms', 'wall_ms', 'gap_ms', 'submit_wall_ms']})
(args.root / 'submission-validation.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report['warm'], indent=2))
