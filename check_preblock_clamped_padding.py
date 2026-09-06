"""Test whether edge-replicated RGB reproduces valid-texture preblock padding."""
from pathlib import Path
import numpy as np
import subprocess,os,json
root=Path('release/native-rgb-clamp1080');root.mkdir(exist_ok=False)
valid=np.fromfile('release/native-rgb-valid1080/input-hwc.rgba32f',np.float32).reshape(1080,1920,4)
rgb=np.pad(valid,((0,72),(0,0),(0,0)),mode='edge');rgb.tofile(root/'input-hwc.rgba32f')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_')}
env.update(DLSS5_PREBLOCK_WIDTH='1920',DLSS5_PREBLOCK_HEIGHT='1152',DLSS5_PREBLOCK_SEED='0',DLSS5_PREBLOCK_PARAMETER_FILE=str(Path('release/native-kernel-params-25972-17399312/launch-0001.bin').resolve()))
subprocess.run(['/tmp/preblock-branch-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','release/native-rgb-game/block0.weights',str(root/'input-hwc.rgba32f'),str(root/'block0-main.fp8'),str(root/'block0-down.fp8'),'0','0'],env=env,check=True,timeout=30)
r={'scope':'original clamp-padding candidate, not AMD/full game textures','branches':{}}
for name in ('main','down'):
 actual=np.fromfile(root/f'block0-{name}.fp8',np.uint8);target=np.fromfile(f'release/native-rgb-valid1080/block0-{name}.fp8',np.uint8)
 assert actual.size==target.size and not np.any((actual&127)==127)
 r['branches'][name]={'different':int(np.count_nonzero(actual!=target))}
print(json.dumps(r,indent=2));(root/'validation.json').write_text(json.dumps(r,indent=2)+'\n')
