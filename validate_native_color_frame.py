"""Verify downloaded integration pixels, never infer game acceptance."""
from pathlib import Path
import argparse,hashlib,json,re
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--results',type=Path,required=True);a=p.parse_args()
fixture=Path('release/native-color-frame');root=a.results
digest=lambda b:hashlib.sha256(b).hexdigest()
reference=(fixture/'expected.f16').read_bytes()
assert digest(reference)=='e3c82de76e428a682780522b1147650bee5e80ce8ab26bbbe1a4d31712b71535'
source=(fixture/'source.f16').read_bytes()
run=json.loads((root/'frame-run.json').read_text(encoding='utf-8-sig'))
assert run['source_sha256'].lower()==digest(source),'Wrong source in deployed run'
log=(root/'frame.stdout.log').read_text()
rows=[tuple(map(int,v)) for v in re.findall(r'game_frame frame=(\d+) half_values=(\d+) different=(\d+) history=(\d+)',log)]
assert rows==[(i,8294400,0,0) for i in range(3)],rows
assert 'full_color_frame=exact;' in log
actual=(root/'actual-frame.f16').read_bytes()
assert len(actual)==len(reference)==len(source)==16588800
assert actual==reference,'Downloaded output differs from independent original reference'
pixels=np.frombuffer(actual,np.float16).reshape(1080,1920,4)
original=np.frombuffer(source,np.float16).reshape(pixels.shape)
assert np.isfinite(pixels).all() and np.array_equal(pixels[:,:,3],original[:,:,3])
report=dict(scope='fixed linear input full color/network chain; no temporal feedback or game display proof',
 pid=run['pid'],frames=rows,output_sha256=digest(actual),source_sha256=digest(source),
 exe_sha256=run['exe_sha256'],pass_result=True,game_verified=False)
(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
