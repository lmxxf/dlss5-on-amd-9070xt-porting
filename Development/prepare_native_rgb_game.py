"""Start a controlled actual-size RGB chain; captured scalar profile, one texture."""
from pathlib import Path
import subprocess,os,json,hashlib
import numpy as np
root=Path('release/native-rgb-game');root.mkdir(exist_ok=False);h,w=1152,1920
# Reuse one RGB fixture at encoder and eventual output composition; not a game capture.
source=Path('release/native-post70/game/color.f32');raw=source.read_bytes();assert len(raw)==h*w*16
(root/'input-hwc.rgba32f').write_bytes(raw)
rgb=np.frombuffer(raw,np.float32).reshape(h,w,4);assert np.isfinite(rgb).all()
rgb.reshape(h//8,8,w//8,8,4).transpose(0,2,1,3,4).copy().tofile(root/'input.rgba32f')
subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll','block0.layer0.layer',str(root/'block0.weights')],check=True,capture_output=True)
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_')}
env.update(DLSS5_PREBLOCK_WIDTH=str(w),DLSS5_PREBLOCK_HEIGHT=str(h),DLSS5_PREBLOCK_SEED='0',DLSS5_PREBLOCK_PARAMETER_FILE=str(Path('release/native-kernel-params-25972-17399312/launch-0001.bin').resolve()))
subprocess.run(['/tmp/preblock-branch-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',str(root/'block0.weights'),str(root/'input-hwc.rgba32f'),str(root/'block0-main.fp8'),str(root/'block0-down.fp8'),'0','0'],env=env,check=True,timeout=30)
for name,count in [('main',h*w*32),('down',h*w*8)]:
 x=np.fromfile(root/f'block0-{name}.fp8',np.uint8);assert x.size==count and not np.any((x&127)==127)
(root/'provenance.json').write_text(json.dumps({'scope':'controlled one-texture RGB; actual captured scalar profile, seed0 override; original preblock captured but numerical comparison pending','RGB_shape':[h,w,4],'input_sha256':hashlib.sha256(raw).hexdigest()},indent=2)+'\n')
