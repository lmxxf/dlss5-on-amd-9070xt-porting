"""Verify diagnostic launch prefixes against unchanged full-size original output."""
from pathlib import Path
import os,subprocess,json
import numpy as np
root=Path('release/native-temporal-valid1080');out=root/'prefix-grid';out.mkdir(exist_ok=True)
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_')}
env.update(DLSS5_PREBLOCK_WIDTH='1920',DLSS5_PREBLOCK_HEIGHT='1152',DLSS5_PREBLOCK_GAME_TEXTURE='1',
 DLSS5_PREBLOCK_PARAMETER_FILE='release/native-game-present/temporal-3536/preblock-live-1.bin',DLSS5_PREBLOCK_SEED='0',
 DLSS5_PREBLOCK_SLOT8=str(root/'history.f32'),DLSS5_PREBLOCK_SLOT10=str(root/'motion.f32'))
report=[]
for gx,gy in [(4,111),(40,43)]:
 env.update(DLSS5_PREBLOCK_DEBUG_GRID_X=str(gx),DLSS5_PREBLOCK_DEBUG_GRID_Y=str(gy))
 prefix=f'{gx}x{gy}'
 subprocess.run(['/tmp/native-preblock-temporal-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',
  'release/native-rgb-game/block0.weights',str(root/'input.f32'),str(out/f'{prefix}.main.fp8'),str(out/f'{prefix}.down.fp8'),'0'],env=env,check=True,timeout=20)
 for branch,shape in [('main',(288,480,512)),('down',(2,576,960,16))]:
  a=np.fromfile(out/f'{prefix}.{branch}.fp8',np.uint8).reshape(shape)
  b=np.fromfile(root/f'original.{branch}.fp8',np.uint8).reshape(shape)
  selection=(slice(0,gy*2),slice(0,gx*2),slice(None)) if branch=='main' else (slice(None),slice(0,gy*4),slice(0,gx*4),slice(None))
  count=int(np.count_nonzero(a[selection]!=b[selection]))
  report.append({'grid':prefix,'branch':branch,'selected_bytes':int(a[selection].size),'different':count})
  assert count==0
(out/'validation.json').write_text(json.dumps({'scope':'prefix launch selected output only; full dimensions unchanged','checks':report},indent=2)+'\n')
print(report)
