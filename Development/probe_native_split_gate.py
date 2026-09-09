"""Diagnose split ffwd connectivity; old gate/up hypothesis is not assumed."""
from pathlib import Path
import json,subprocess,sys
import numpy as np
root=Path('release/native-c512/gate-probe');root.mkdir(parents=True,exist_ok=True)
np.full(4*1024*1024,0x38,np.uint8).tofile(root/'input.fp8')
for i,size in enumerate((263168,917568,263168),1):
 w=np.zeros(size,np.uint8)
 if i in (1,3):w.view('<f2')[262144//2:]=1
 if i==2:w.view('<f4')[917504//4:]=1
 w.tofile(root/f'w{i}.bin')
reports=[]
cases=[('triple',0x60000)] if '--triple' in sys.argv else [('pair',offset) for offset in (0x40000,0x40200,0x40400,0x40600,0x42000,0x60000,0x60200,0x60400,0x60600)] if '--pairs' in sys.argv else [('partition',1<<bit) for bit in range(19)]
for mode,offset in cases:
 w=np.zeros(524288,np.uint8)
 if mode=='partition':w[(np.arange(524288)&offset)!=0]=0x38
 else:w[offset]=0x38
 if mode=='triple':w[0x40000]=0x38
 w[0]=0x38;w.tofile(root/'w0.bin')
 output=root/'output.fp8'
 subprocess.run(['/tmp/native-split-global-oracle',str(root/'input.fp8'),str(output),*[str(root/f'w{i}.bin') for i in range(4)],'8','8','0','native-inpview'],check=True,capture_output=True)
 raw=np.fromfile(str(output)+'.branch',np.uint8);assert not np.any((raw&127)==127)
 active=np.flatnonzero((raw&127)!=0)
 if active.size:
  row={'probe':mode,'offset':hex(offset),'nonzero_byte_count':int(active.size),'nonzero_codes':np.unique(raw[active]).tolist()};reports.append(row);print(json.dumps(row),flush=True)
assert reports, 'no active partition found'
if '--triple' in sys.argv:
 assert len(reports)==1 and reports[0]['nonzero_byte_count']==64 and reports[0]['nonzero_codes']==[0x3a], 'three-stage unit path changed'
print(json.dumps({'partition_probes':reports,'scope':'controlled first-stage connection, not full split arithmetic'}))
