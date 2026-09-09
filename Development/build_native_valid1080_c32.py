"""Generate original encoder1..4 from the verified valid1080 preblock output."""
from pathlib import Path
import subprocess,os,json,hashlib,argparse
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--base',type=Path,default=Path('release/native-rgb-valid1080'));args=p.parse_args()
base=args.base;out=base/'encoder-c32';out.mkdir(exist_ok=False)
source=base/'block0-down.fp8';w,h=960,576;reports=[]
env={k:v for k,v in os.environ.items() if not k.startswith(('DLSS5_PREBLOCK_','DLSS5_NATIVE_SCAN_','DLSS5_SPLIT_'))}
for b,shift in ((1,0),(2,3),(3,1),(4,2)):
 weights=out/f'block{b}.weights'
 subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',f'block{b}.layer0.layer',str(weights)],check=True,capture_output=True)
 suffix='inpview' if b==1 else 'ds' if b==4 else ''
 symbol='cc_tinlayout_fused_swin_1h_32_1'+('_'+suffix if suffix else '')+'_fp8'
 main=out/f'block{b}-main.fp8';down=out/f'block{b}-down.fp8'
 subprocess.run(['/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',str(weights),str(source),str(main),str(down),symbol,str(w),str(h),str((w+(4 if shift&1 else 0)+7)//8),str((h+(4 if shift&2 else 0)+7)//8),'1',str(5 if b==1 else 6 if b==4 else 4),str(shift)],env=env,check=True,timeout=20)
 raw=np.fromfile(main,np.uint8);assert raw.size>=w*h*32 and not raw[w*h*32:].any() and not np.any((raw[:w*h*32]&127)==127)
 if b==4:
  raw=np.fromfile(down,np.uint8);n=w*h*16;assert raw.size>=n and not raw[n:].any() and not np.any((raw[:n]&127)==127)
 reports.append({'block':b,'shift':shift,'input':str(source),'main_sha256':hashlib.sha256(main.read_bytes()).hexdigest()})
 source=main
(out/'capture.json').write_text(json.dumps({'scope':'original encoder1..4 capture; finite/length only, arithmetic comparison pending','stages':reports},indent=2)+'\n')
