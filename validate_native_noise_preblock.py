"""Compare AMD universal-function-table preblock against original CUBIN."""
from pathlib import Path
import argparse,json,os,subprocess
import numpy as np
from decode_tinlayout_global import e4m3fn
parser=argparse.ArgumentParser();parser.add_argument('--seed',type=lambda s:int(s,0),default=0);args=parser.parse_args()
root=Path('release/native-rgb128');folder=root/'noise-residual'
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_')}
env.update(DLSS5_PREBLOCK_WIDTH='128',DLSS5_PREBLOCK_HEIGHT='128',DLSS5_PREBLOCK_SEED=str(args.seed),DLSS5_PREBLOCK_PARAMETER_FILE=str(Path('release/live-preblock-v2/preblock-live-0.bin').resolve()))
main=folder/f'original-seed{args.seed}-main.fp8';down=folder/f'original-seed{args.seed}-down.fp8'
subprocess.run(['/tmp/preblock-branch-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','/tmp/block0.weights',str(root/'input-hwc.rgba32f'),str(main),str(down),'0','0'],env=env,check=True,capture_output=True)
packed=np.fromfile(main,np.uint8);assert packed.size==128*128*32
pm=np.argmax(np.abs(np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048)),axis=0)
expected=np.empty((128,128,32),np.float32)
for ty in range(16):
 for tx in range(16):
  base=ty*16*2048+tx*1024
  record=np.concatenate([packed[base:base+1024],packed[base+16*1024:base+17*1024]])
  expected[ty*8:ty*8+8,tx*8:tx*8+8]=e4m3fn(record[pm]).reshape(8,8,32)
ds=e4m3fn(np.fromfile(down,np.uint8)).reshape(2,64,64,16).transpose(1,2,0,3).reshape(64,64,32)
ok=True
for name,want in [('main',expected),('down',ds)]:
 got=np.fromfile(root/f'amd/lut-{name}.f32','<f4').reshape(want.shape)
 assert np.isfinite(got).all()
 error=np.abs(got-want);different=int(np.count_nonzero(error));ok &= different==0
 print(json.dumps({'seed':args.seed,'branch':name,'values':want.size,'different':different,'max_error':float(error.max()),'scope':'AMD preblock, not final RGB/game acceptance'}),flush=True)
raise SystemExit(0 if ok else 1)
