from pathlib import Path
import json,numpy as np
root=Path('release/native-temporal-holdout');out=root/'amd';out.mkdir(exist_ok=False)
(out/'history.f32').write_bytes((root/'history.f32').read_bytes())
coords=[];targets=[];lane=np.arange(32)
for i,(dx,dy) in enumerate([(.37,-.29),(-.63,.81),(.015625,.484375),(.72,.23)]):
 r=json.loads((root/f'case{i}/sample-registers.json').read_text());assert r['pc_offset']==0x1800
 coords.append(np.stack([lane%8+.5+float(np.float32(dx)),lane//8+.5+float(np.float32(dy))],-1))
 targets.extend([row['rgb']+[1.] for row in r['rows']])
np.concatenate(coords).astype('<f4').tofile(out/'coordinates.f32')
np.asarray(targets,dtype=np.float16).astype('<f4').tofile(out/'oracle-half.f32')
