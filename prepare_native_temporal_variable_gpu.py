"""Same original/AMD preblock inputs with spatially varying motion, no fitted RGB."""
from pathlib import Path
import os,subprocess,argparse
import numpy as np
p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group();g.add_argument('--rect',action='store_true');g.add_argument('--large',action='store_true');args=p.parse_args()
root=Path('release/native-temporal-large' if args.large else 'release/native-temporal-rect' if args.rect else 'release/native-temporal-variable');root.mkdir(exist_ok=False)
base=Path('release/native-temporal-inputs-gates')
for source,name in [(base/'rgb.f32','input.f32'),(base/'history.f32','history.f32'),(Path('release/native-temporal-coordinate-random/motion.f32'),'motion.f32')]:
 (root/name).write_bytes(source.read_bytes())
width,height=(120,72) if args.large else (24,16) if args.rect else (8,8)
if args.rect or args.large:
 rng=np.random.default_rng(7306 if args.large else 7305);rgb=rng.uniform(.1,.9,(height,width,4)).astype('<f4');rgb[:,:,3]=1
 history=rng.uniform(.1,.9,rgb.shape).astype('<f4');history[:,:,3]=1
 motion=np.zeros_like(rgb);motion[:,:,:2]=rng.uniform(-.75,.75,(height,width,2))
 rgb.tofile(root/'input-hwc.f32');rgb.reshape(height//8,8,width//8,8,4).transpose(0,2,1,3,4).copy().tofile(root/'input.f32')
 history.tofile(root/'history.f32');motion.tofile(root/'motion.f32')
for name in ('ffn','attention'):(root/f'block0-{name}.f32').write_bytes((base/f'amd-direct/block0-{name}.f32').read_bytes())
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_')}
env.update(DLSS5_PREBLOCK_PARAMETER_FILE='release/native-game-present/temporal-3536/preblock-live-1.bin',DLSS5_PREBLOCK_SEED='0',DLSS5_PREBLOCK_SLOT8=str(root/'history.f32'),DLSS5_PREBLOCK_SLOT10=str(root/'motion.f32'))
env.update(DLSS5_PREBLOCK_WIDTH=str(width),DLSS5_PREBLOCK_HEIGHT=str(height))
subprocess.run(['/tmp/native-preblock-temporal-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','release/native-rgb-game/block0.weights',str(root/('input-hwc.f32' if args.rect or args.large else 'input.f32')),str(root/'original.main.fp8'),str(root/'original.down.fp8'),'0'],env=env,check=True,timeout=20)
