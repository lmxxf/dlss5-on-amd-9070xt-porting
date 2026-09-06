"""Original encoder5..8 continuation from same valid1080 RGB; capture only."""
from pathlib import Path
import subprocess,json,os,hashlib
import numpy as np
base=Path('release/native-rgb-valid1080');root=base/'encoder-c64';root.mkdir(exist_ok=False)
source=base/'encoder-c32/block4-down.fp8';w,h,C=480,288,64;reports=[]
env={k:v for k,v in os.environ.items() if not k.startswith(('DLSS5_PREBLOCK_','DLSS5_NATIVE_SCAN_','DLSS5_SPLIT_'))}
for b,shift in ((5,0),(6,3),(7,1),(8,2)):
 weights=root/f'block{b}.weights';subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',f'block{b}.layer0.layer',str(weights)],check=True,capture_output=True)
 symbol='cc_tinlayout_fused_swin_2h_64_2'+('_inpview' if b==5 else '_ds' if b==8 else '')+'_fp8'
 main=root/f'block{b}-main.fp8';down=root/f'block{b}-down.fp8'
 subprocess.run(['/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-01.cubin',str(weights),str(source),str(main),str(down),symbol,str(w),str(h),str((w+(4 if shift&1 else 0)+7)//8),str((h+(4 if shift&2 else 0)+7)//8),'2',str(8 if b==8 else 7),str(shift)],env=env,check=True,timeout=20)
 raw=np.fromfile(main,np.uint8);n=w*h*C;assert raw.size>=n and not raw[n:].any() and not np.any((raw[:n]&127)==127)
 if b==8:
  raw=np.fromfile(down,np.uint8);n=w*h*C//2;assert raw.size>=n and not raw[n:].any() and not np.any((raw[:n]&127)==127)
 reports.append({'block':b,'shift':shift,'input':str(source),'main_sha256':hashlib.sha256(main.read_bytes()).hexdigest()});source=main
(root/'capture.json').write_text(json.dumps({'scope':'original5..8 same RGB capture, finite/length only; arithmetic verification pending','stages':reports},indent=2)+'\n')
