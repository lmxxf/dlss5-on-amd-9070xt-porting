"""Isolate whether the C64 0x6000 matrix can act without either FFN matrix."""
from pathlib import Path
import subprocess,json
import numpy as np

folder=Path('release/native-c64/extra');folder.mkdir(parents=True,exist_ok=True)
source=np.full(32768,0x38,np.uint8);source.tofile(folder/'ones.fp8')
reports=[]
for label,offsets in [('zero',()),('extra_byte0',(0x6000,)),('ffn_pair',(0,0x4000)),('ffn_and_extra',(0,0x4000,0x6000))]:
 weights=np.zeros(61760,np.uint8)
 weights.view('<f2')[0xf0b0//2:0xf130//2]=1
 weights.view('<f4')[0xe0a0//4:0xe0a0//4+2]=1
 for offset in offsets:weights[offset]=0x38
 weights.tofile(folder/'probe.weights')
 output=folder/f'{label}.fp8'
 subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(folder/'probe.weights'),str(folder/'ones.fp8'),str(output),str(folder/'aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_inpview_fp8','32','16','4','2','2','7','0'],check=True,capture_output=True)
 raw=np.fromfile(output,np.uint8)[:32768]
 reports.append({'mode':label,'nonzero':int(np.count_nonzero(raw&127)),'values':np.unique(raw).tolist()})
print(json.dumps(reports,indent=2))
assert [row['nonzero'] for row in reports]==[0,0,0,512]
assert reports[-1]['values']==[0,0x3a], 'three-matrix controlled path changed'
