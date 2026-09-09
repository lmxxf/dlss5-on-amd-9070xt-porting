"""Original coefficients and edge oracles for same-RGB GPU0..14."""
from pathlib import Path
import json,numpy as np
from native_c64_reference import unpack
base=Path('release/native-rgb-valid1080');src=base/'encoder-c128';out=base/'amd-front14';out.mkdir(exist_ok=False)
r=json.loads((src/'main-validation.json').read_text());assert len(r['checks'])==6 and all(c['different']==0 and c['finite'] for c in r['checks'])
r=json.loads((src/'down-validation.json').read_text());assert r['different']==0 and r['finite']
for f in (base/'amd-front8').glob('block*.f32'):(out/f.name).write_bytes(f.read_bytes())
(out/'input.f32').write_bytes((base/'input-hwc.rgba32f').read_bytes())
for b in range(9,15):
 f,qkv,p,bias,sc,skip=unpack(src/f'block{b}.weights')
 np.concatenate([f[k].ravel() for k in ('W1','W2','W3','skip')]).astype('<f4').tofile(out/f'block{b}-ffn.f32')
 np.concatenate([*[m.ravel() for m in qkv],p.ravel(),bias.ravel(),sc,skip]).astype('<f4').tofile(out/f'block{b}-attention.f32')
for a,b in [('block14-ds.f32','block14-ds.f32'),('block14-main.f32','oracle-main14.f32'),('block14-down.f32','oracle-down.f32')]:(out/b).write_bytes((src/a).read_bytes())
(out/'provenance.json').write_text(json.dumps({'scope':'same controlled valid1080 RGB0..14; GPU pending','main_shape':[144,240,128],'down_shape':[72,120,256]},indent=2)+'\n')
