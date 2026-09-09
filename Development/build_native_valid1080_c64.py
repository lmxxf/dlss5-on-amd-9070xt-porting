"""Original encoder5..8 continuation from same valid1080 RGB; capture only."""
from pathlib import Path
import subprocess,json,os,hashlib,argparse
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--base',type=Path,default=Path('release/native-rgb-valid1080'));g=p.add_mutually_exclusive_group();g.add_argument('--c128',action='store_true');g.add_argument('--c256',action='store_true');args=p.parse_args()
base=args.base;root=base/('encoder-c256' if args.c256 else 'encoder-c128' if args.c128 else 'encoder-c64');root.mkdir(exist_ok=False)
source=base/('encoder-c128/block14-down.fp8' if args.c256 else 'encoder-c64/block8-down.fp8' if args.c128 else 'encoder-c32/block4-down.fp8');w,h,C=(120,72,256) if args.c256 else (240,144,128) if args.c128 else (480,288,64);reports=[]
sequence=((15,0),(16,3),(17,1),(18,2),(19,0),(20,3),(21,1),(22,2)) if args.c256 else ((9,0),(10,3),(11,1),(12,2),(13,0),(14,3)) if args.c128 else ((5,0),(6,3),(7,1),(8,2))
first,last=sequence[0][0],sequence[-1][0];heads=C//32
env={k:v for k,v in os.environ.items() if not k.startswith(('DLSS5_PREBLOCK_','DLSS5_NATIVE_SCAN_','DLSS5_SPLIT_'))}
for b,shift in sequence:
 weights=root/f'block{b}.weights';subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',f'block{b}.layer0.layer',str(weights)],check=True,capture_output=True)
 symbol=f'cc_tinlayout_fused_swin_{heads}h_{C}_{heads}'+('_inpview' if b==first else '_ds' if b==last else '')+'_fp8'
 main=root/f'block{b}-main.fp8';down=root/f'block{b}-down.fp8'
 subprocess.run(['/tmp/native-upsample-global-oracle',f'/tmp/dlssnr-cubins/dlssnr-{3 if args.c256 else 2 if args.c128 else 1:02d}.cubin',str(weights),str(source),str(main),str(down),symbol,str(w),str(h),str((w+(4 if shift&1 else 0)+7)//8),str((h+(4 if shift&2 else 0)+7)//8),str(heads),str(8 if b==last else 7),str(shift)],env=env,check=True,timeout=20)
 raw=np.fromfile(main,np.uint8);n=w*h*C;assert raw.size>=n and not raw[n:].any() and not np.any((raw[:n]&127)==127)
 if b==last:
  raw=np.fromfile(down,np.uint8);n=w*h*C//2;assert raw.size>=n and not raw[n:].any() and not np.any((raw[:n]&127)==127)
 reports.append({'block':b,'shift':shift,'input':str(source),'main_sha256':hashlib.sha256(main.read_bytes()).hexdigest()});source=main
(root/'capture.json').write_text(json.dumps({'scope':'original encoder same RGB capture, finite/length only; arithmetic verification pending','channels':C,'stages':reports},indent=2)+'\n')
