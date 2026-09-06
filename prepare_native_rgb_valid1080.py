"""Original preblock with valid1080 texture and padded1152 processing extent."""
from pathlib import Path
import numpy as np
import subprocess,os,json
root=Path('release/native-rgb-valid1080');root.mkdir(exist_ok=False)
color=np.fromfile('release/native-rgb-game/input-hwc.rgba32f',np.float32).reshape(1152,1920,4)[:1080].copy()
color.tofile(root/'input-hwc.rgba32f')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_')}
env.update(DLSS5_PREBLOCK_WIDTH='1920',DLSS5_PREBLOCK_HEIGHT='1152',DLSS5_PREBLOCK_SEED='0',DLSS5_PREBLOCK_GAME_TEXTURE='1',DLSS5_PREBLOCK_PARAMETER_FILE=str(Path('release/native-kernel-params-25972-17399312/launch-0001.bin').resolve()))
subprocess.run(['/tmp/preblock-branch-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','release/native-rgb-game/block0.weights',str(root/'input-hwc.rgba32f'),str(root/'block0-main.fp8'),str(root/'block0-down.fp8'),'0','0'],env=env,check=True,timeout=30)
r={'scope':'single valid1080 texture, captured scalar profile, padded1152 processing, seed0; not full game textures','branches':{}}
for name,count in [('main',1152*1920*32),('down',1152*1920*8)]:
 raw=np.fromfile(root/f'block0-{name}.fp8',np.uint8);old=np.fromfile(f'release/native-rgb-game/block0-{name}.fp8',np.uint8)
 assert raw.size==count and not np.any((raw&127)==127)
 r['branches'][name]={'bytes':count,'different_from_full1152_texture':int(np.count_nonzero(raw!=old))}
(root/'capture.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
