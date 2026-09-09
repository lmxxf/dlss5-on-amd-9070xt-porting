"""Exhaustive normalized positive reciprocal table, independent of game inputs."""
from pathlib import Path
import subprocess,json
import numpy as np
root=Path('release/native-reciprocal')
bits=np.arange(0x3f800000,0x40000000,dtype=np.uint32)
bits.tofile(root/'normalized-input.f32')
subprocess.run(['/tmp/probe-cuda-reciprocal',str(root/'normalized-input.f32'),str(root/'normalized-output.f32')],check=True,timeout=30)
table=np.fromfile(root/'normalized-output.f32',np.float32)
inputs=np.fromfile(root/'input.f32',np.uint32)
actual=np.fromfile(root/'output.f32',np.uint32)
assert table.size==2**23 and np.isfinite(table).all()
exponent=((inputs>>23)&255).astype(np.int32)
assert np.all((exponent>0)&(exponent<255)) and np.all(inputs<0x80000000)
predicted=np.ldexp(table[inputs&0x7fffff],127-exponent).astype(np.float32).view(np.uint32)
report={'scope':'generic normalized reciprocal table exponent-scaling vs independent GPU samples; not AMD runtime validation',
 'entries':int(table.size),'bytes':int(table.nbytes),'tested_values':int(inputs.size),
 'different':int(np.count_nonzero(predicted!=actual))}
(root/'table-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
assert report['different']==0
