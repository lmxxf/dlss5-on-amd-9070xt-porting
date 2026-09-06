"""Controlled original preblock extra-texture sensitivity, not an AMD oracle."""
from pathlib import Path
import os,subprocess,json
import numpy as np
root=Path('release/native-temporal-inputs');root.mkdir(exist_ok=False)
rng=np.random.default_rng(7301)
rgb=rng.uniform(.1,.9,(8,8,4)).astype('<f4');rgb[:,:,3]=1
history=rgb[:,::-1].copy();motion=np.zeros_like(rgb)
for name,data in [('rgb',rgb),('history',history),('motion',motion)]:data.tofile(root/f'{name}.f32')
shifted=motion.copy();shifted[:,:,:2]=.125;shifted.tofile(root/'shifted.f32')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_')}
env.update(DLSS5_PREBLOCK_PARAMETER_FILE='release/native-game-present/temporal-3536/preblock-live-1.bin',DLSS5_PREBLOCK_SEED='0')
rows=[];baseline=None
for name,extra in [('single',{}),('slot8',{'DLSS5_PREBLOCK_SLOT8':'history.f32'}),('both_zero',{'DLSS5_PREBLOCK_SLOT8':'history.f32','DLSS5_PREBLOCK_SLOT10':'motion.f32'}),('both_shifted',{'DLSS5_PREBLOCK_SLOT8':'history.f32','DLSS5_PREBLOCK_SLOT10':'shifted.f32'})]:
    trial=dict(env,**{k:str(root/v) for k,v in extra.items()})
    prefix=root/name
    subprocess.run(['/tmp/native-preblock-temporal-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','release/native-rgb-game/block0.weights',str(root/'rgb.f32'),str(prefix)+'.main.fp8',str(prefix)+'.down.fp8','0'],env=trial,check=True,timeout=20)
    output=np.fromfile(str(prefix)+'.main.fp8',np.uint8)
    assert output.size==8*8*32 and not np.any((output&127)==127)
    if baseline is None:baseline=output
    rows.append({'case':name,'main_values':output.size,'different_from_single':int(np.count_nonzero(output!=baseline))})
report={'scope':'original 8x8 preblock controlled extra textures; semantics and AMD implementation pending','cases':rows}
(root/'sensitivity.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
