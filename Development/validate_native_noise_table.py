"""Universal table correctness against separately executed SFU traces."""
from pathlib import Path
import subprocess,json
import numpy as np
root=Path('release/native-rgb128/noise-residual')
table=np.fromfile(root/'functions.f32','<f4').reshape(1<<24,3)
assert np.isfinite(table).all()
for seed in [0,1,0x12345678]:
 path=root/f'trace-seed{seed}.f32'
 subprocess.run(['/tmp/probe-native-noise',str(path),'128','128',str(seed)],check=True)
 trace=np.fromfile(path,'<f4').reshape(-1,16)
 ix=(trace[:,:4]*np.float32(1<<24)).astype(np.uint32)-1
 got=np.stack([table[ix[:,0],0]*table[ix[:,2],1],table[ix[:,1],0]*table[ix[:,3],1],table[ix[:,1],0]*table[ix[:,3],2]],-1)
 assert np.array_equal(got,trace[:,13:16])
 print(json.dumps({'seed':seed,'values':got.size,'float32_exact':True,'table_bytes':table.nbytes}),flush=True)
