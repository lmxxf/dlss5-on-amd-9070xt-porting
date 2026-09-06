"""Same controlled RGB0..30/head fixture with original intermediate provenance."""
from pathlib import Path
import json,numpy as np
base=Path('release/native-rgb-valid1080');root=base/'encoder-split';out=base/'amd-head';out.mkdir(exist_ok=False)
for f in (base/'amd-front22').glob('block*.f32'):(out/f.name).write_bytes(f.read_bytes())
(out/'input.f32').write_bytes((base/'input-hwc.rgba32f').read_bytes())
previous=base/'encoder-c256/block22-down.fp8'
for b,shift in zip(range(23,31),(0,3,1,2,0,3,1,2)):
 src=root/f'decoder-block{b}';r=json.loads((src/'validation.json').read_text())
 assert r['status']=='pass' and r['shift']==shift and Path(r['input'])==previous and len(r['checks'])==4 and all(c['different']==0 for c in r['checks'])
 for name in ('ffwd','ffwd-projection','attention'):(out/f'block{b}-{name}.f32').write_bytes((src/f'{name}.f32').read_bytes())
 previous=src/'output.fp8'
head=root/'pool-head';r=json.loads((head/'validation.json').read_text());assert r['main_exact'] and r['pool_exact'] and r['head_exact']
for a,b in [('head.f32','oracle-head.f32'),('head-matrix.f32','head-matrix.f32'),('block30-raw.f32','oracle-main30-raw.f32')]:(out/b).write_bytes((head/a).read_bytes())
(out/'provenance.json').write_text(json.dumps({'scope':'same valid1080 single texture RGB0..30/head, GPU pending','raw_main_shape':[36,60,512],'head_shape':[20,32,1024]},indent=2)+'\n')
