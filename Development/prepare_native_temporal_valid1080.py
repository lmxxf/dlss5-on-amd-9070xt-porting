"""Prepare full valid1080 temporal fixture and original preblock oracle."""
from pathlib import Path
import numpy as np
import os,subprocess,shutil
root=Path('release/native-temporal-valid1080');root.mkdir(exist_ok=False)
source=Path('release/native-rgb-valid1080/input-hwc.rgba32f')
rgb=np.fromfile(source,np.float32).reshape(1080,1920,4)
shutil.copyfile(source,root/'input.f32')
rgb[:,::-1].copy().tofile(root/'history.f32')
motion=np.zeros_like(rgb)
motion[:,:,:2]=np.random.default_rng(7308).uniform(-.75,.75,(1080,1920,2))
motion.tofile(root/'motion.f32')
for name in ('ffn','attention'):
    shutil.copyfile(Path('release/native-temporal-large')/f'block0-{name}.f32',root/f'block0-{name}.f32')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_')}
env.update(DLSS5_PREBLOCK_WIDTH='1920',DLSS5_PREBLOCK_HEIGHT='1152',DLSS5_PREBLOCK_GAME_TEXTURE='1',
    DLSS5_PREBLOCK_PARAMETER_FILE='release/native-game-present/temporal-3536/preblock-live-1.bin',
    DLSS5_PREBLOCK_SEED='0',DLSS5_PREBLOCK_SLOT8=str(root/'history.f32'),DLSS5_PREBLOCK_SLOT10=str(root/'motion.f32'))
subprocess.run(['/tmp/native-preblock-temporal-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',
    'release/native-rgb-game/block0.weights',str(root/'input.f32'),str(root/'original.main.fp8'),
    str(root/'original.down.fp8'),'0'],env=env,check=True,timeout=45)
