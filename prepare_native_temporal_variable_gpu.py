"""Same original/AMD preblock inputs with spatially varying motion, no fitted RGB."""
from pathlib import Path
import os,subprocess
root=Path('release/native-temporal-variable');root.mkdir(exist_ok=False)
base=Path('release/native-temporal-inputs-gates')
for source,name in [(base/'rgb.f32','input.f32'),(base/'history.f32','history.f32'),(Path('release/native-temporal-coordinate-random/motion.f32'),'motion.f32')]:
 (root/name).write_bytes(source.read_bytes())
for name in ('ffn','attention'):(root/f'block0-{name}.f32').write_bytes((base/f'amd-direct/block0-{name}.f32').read_bytes())
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_')}
env.update(DLSS5_PREBLOCK_PARAMETER_FILE='release/native-game-present/temporal-3536/preblock-live-1.bin',DLSS5_PREBLOCK_SEED='0',DLSS5_PREBLOCK_SLOT8=str(root/'history.f32'),DLSS5_PREBLOCK_SLOT10=str(root/'motion.f32'))
subprocess.run(['/tmp/native-preblock-temporal-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','release/native-rgb-game/block0.weights',str(root/'input.f32'),str(root/'original.main.fp8'),str(root/'original.down.fp8'),'0'],env=env,check=True,timeout=20)
