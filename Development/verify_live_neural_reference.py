"""Compare an actual captured game result to its independently replayed reference.

Single reset-history frame only; this cannot accept temporal or real-time work.
"""
from pathlib import Path
import argparse,hashlib,json
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--capture',type=Path,required=True);p.add_argument('--pid',type=int,required=True);p.add_argument('--request',type=int,default=1);args=p.parse_args()
root=args.capture;ref=root/f'reference-request-{args.request}';prefix=f'neural-{args.pid}-request-{args.request}'
before=(root/f'{prefix}-before.f16').read_bytes();after=(root/f'{prefix}-after.f16').read_bytes()
assert before==(ref/'source.f16').read_bytes(),'Reference source differs from captured source'
expected=(ref/'expected.f16').read_bytes();assert len(before)==len(after)==len(expected)==16588800
source=np.frombuffer(before,np.float16).reshape(1080,1920,4)
actual=np.frombuffer(after,np.float16).reshape(source.shape);oracle=np.frombuffer(expected,np.float16).reshape(source.shape)
assert np.isfinite(source).all() and np.isfinite(actual).all() and np.isfinite(oracle).all()
assert np.count_nonzero(source[:,:,:3])>0,'Black source is not game-image acceptance'
assert np.array_equal(actual[:,:,3],source[:,:,3]) and np.array_equal(oracle[:,:,3],source[:,:,3])
report=dict(scope='captured in-game reset-history frame versus independent original replay',pid=args.pid,request=args.request,
 bit_different=int(np.count_nonzero(np.frombuffer(after,np.uint16)!=np.frombuffer(expected,np.uint16))),
 max_abs=float(np.abs(actual.astype(np.float32)-oracle.astype(np.float32)).max()),
 source_sha256=hashlib.sha256(before).hexdigest(),actual_sha256=hashlib.sha256(after).hexdigest(),reference_sha256=hashlib.sha256(expected).hexdigest(),
 single_frame_verified=after==expected,temporal_verified=False,realtime_verified=False)
(root/f'{prefix}-reference-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2));assert after==expected,'Live output differs; investigate, do not loosen the gate'
