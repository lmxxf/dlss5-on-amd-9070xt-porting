from pathlib import Path
import json,struct,numpy as np
root=Path('release/native-temporal-coordinate-random');out=root/'amd';out.mkdir(exist_ok=False)
(out/'motion.f32').write_bytes((root/'motion.f32').read_bytes())
r=json.loads((root/'sample-registers-10d0.json').read_text());assert r['pc_offset']==0x10d0
a=[[struct.unpack('<f',struct.pack('<I',x['raw'][str(k)]))[0]*8 for k in (46,19)] for x in r['rows']]
assert len(a)==32
np.asarray(a,'<f4').tofile(out/'oracle.f32')
