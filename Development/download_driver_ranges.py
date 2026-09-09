"""Finish a stopped partial AMD download with checked HTTP range requests."""
import argparse
import concurrent.futures
import shutil
import time
import urllib.request
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('prefix', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
url = 'https://drivers.amd.com/drivers/amd-software-adrenalin-edition-26.10.07.02-win11-rc7-agility-sdk.exe'
headers = {'Referer': 'https://www.amd.com/', 'User-Agent': 'curl/8'}
with urllib.request.urlopen(urllib.request.Request(url, headers=headers, method='HEAD'), timeout=30) as r:
    total = int(r.headers['Content-Length'])
    etag = r.headers['ETag']
assert total == 874669800, total
start = a.prefix.stat().st_size
assert 0 < start < total and not a.output.exists()
parts = a.output.parent / 'driver-download-parts'
parts.mkdir(exist_ok=True)
step = (total - start + 7) // 8
ranges = [(i, offset, min(offset + step, total) - 1)
          for i, offset in enumerate(range(start, total, step))]

def download(job):
    i, lo, hi = job
    path = parts / f'{lo}-{hi}.part'
    if path.exists() and path.stat().st_size == hi - lo + 1:
        print(f'part {i} retained: {hi-lo+1} bytes', flush=True)
        return path
    for attempt in range(3):
        try:
            request = urllib.request.Request(url, headers={**headers, 'Range': f'bytes={lo}-{hi}', 'If-Range': etag})
            with urllib.request.urlopen(request, timeout=60) as r:
                assert r.status == 206 and r.headers['Content-Range'] == f'bytes {lo}-{hi}/{total}'
                with path.open('wb') as f:
                    shutil.copyfileobj(r, f, 1024 * 1024)
            assert path.stat().st_size == hi - lo + 1
            print(f'part {i} complete: {hi-lo+1} bytes', flush=True)
            return path
        except Exception as error:
            print(f'part {i} attempt {attempt+1}: {error}', flush=True)
            if attempt == 2:
                raise
            time.sleep(2)

print(f'preserved prefix={start}, remaining={total-start}, parts={len(ranges)}', flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    paths = list(pool.map(download, ranges))
with a.output.open('xb') as out:
    for path in [a.prefix, *paths]:
        with path.open('rb') as src:
            shutil.copyfileobj(src, out, 1024 * 1024)
assert a.output.stat().st_size == total
print(f'complete: {a.output} bytes={total}; Authenticode validation still required', flush=True)
