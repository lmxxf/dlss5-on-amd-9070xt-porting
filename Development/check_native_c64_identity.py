"""Check C64 ABI/skip identity without assuming its output spatial layout."""
from pathlib import Path
import subprocess
import numpy as np

folder=Path('release/native-c64');folder.mkdir(parents=True,exist_ok=True)
weights=np.zeros(61760,np.uint8)
weights.view('<f2')[0x7010//2:0x7090//2]=1
weights.view('<f2')[0xf0b0//2:0xf130//2]=1
weights.view('<f4')[0xe0a0//4:0xe0a0//4+2]=1
weights.tofile(folder/'identity.weights')
source=Path('release/native-c32/block4-aux.fp8')
subprocess.run(['/tmp/native-c32-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(folder/'identity.weights'),str(source),str(folder/'identity-output.fp8'),str(folder/'identity-aux.fp8'),'cc_tinlayout_fused_swin_2h_64_2_inpview_fp8','32','16','4','2','2','7','0'],check=True)
x=np.fromfile(source,np.uint8)[:32768];y=np.fromfile(folder/'identity-output.fp8',np.uint8)[:32768]
# Treat the two zero signs equivalently. This checks values, not coordinates.
x[(x&127)==0]=0;y[(y&127)==0]=0
assert np.array_equal(np.sort(x),np.sort(y)), 'C64 identity changed value multiset'
print('identity_value_multiset=exact output_coordinate_mapping=not_yet_verified')
