"""Original block48 identity-skip control, independent of learned weights."""
from pathlib import Path
import json,subprocess
import numpy as np
root=Path('release/native-upsample48');weights=np.zeros(820784,np.uint8)
for offset in (0x78000,0x78200,0xc8420):
    weights[offset:offset+512].view('<f2')[:]=1
weights[0xb8400:0xb8420].view('<f4')[:]=1
weights.tofile(root/'skip-control.weights')
subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-upsample-global-oracle',
    '/tmp/dlssnr-cubins/dlssnr-03.cubin',str(root/'skip-control.weights'),
    'release/smoke-bsffe819/main.fp8',str(root/'skip-control-output.fp8'),str(root/'skip-control-copy.fp8'),
    'cc_tinlayout_fused_swin_8h_256_8_upsample_fp8','16','16','2','2','8','9','0',
    'release/smoke-5aqzrlcy/main.fp8'],check=True,timeout=20)
raw=np.fromfile(root/'skip-control-output.fp8',np.uint8)
report={'scope':'original identity-skip control','different_from_fp8_half':int(np.count_nonzero(raw[:65536]!=0x30)),
        'nonzero_tail':int(np.count_nonzero(raw[65536:]))}
report['status']='pass' if not report['different_from_fp8_half'] and not report['nonzero_tail'] else 'fail'
(root/'skip-control-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
assert report['status']=='pass','identity skip addresses or call contract differ'
