"""Compare non-resource preblock fields used by controlled RGB experiments."""
from pathlib import Path
import struct,json,hashlib
paths=[Path('release/live-preblock-v2/preblock-live-0.bin'),Path('release/native-kernel-params-25972-17399312/launch-0001.bin')]
raw=[p.read_bytes() for p in paths];assert all(len(b)==264 for b in raw)
# No texture/resource pointer values are exported. Geometry/seed remain explicit.
differences=[]
for off in range(0x48,0xd8,4):
 a,b=[struct.unpack_from('<I',r,off)[0] for r in raw]
 if a!=b:differences.append({'offset':hex(off),'old_u32':a,'new_u32':b,'old_f32':struct.unpack('<f',struct.pack('<I',a))[0],'new_f32':struct.unpack('<f',struct.pack('<I',b))[0]})
report={'sources':[str(p) for p in paths],'sha256':[hashlib.sha256(r).hexdigest() for r in raw],'scalar_range':'0x48..0xd7','differences':differences,'scope':'raw scalar equality audit, not complete game texture contract'}
Path('preblock-scalar-profile-comparison.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n');print(json.dumps(report,indent=2))
