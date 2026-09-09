"""Prepare separate zero/shift3 GPU fixtures against original crop outputs."""
from pathlib import Path
import shutil,numpy as np
base=Path('release/native-temporal-valid1080/post70')
for shift,name in [(0,'origin-zero.f32'),(3,'origin-minus4.f32')]:
 out=base/f'gpu-origin{shift}';out.mkdir(exist_ok=False)
 np.memmap(base/'main.f32',np.float32,'r',shape=(576,960,32))[224:232,112:120].copy().tofile(out/'main.f32')
 np.memmap(base/'skip.f32',np.float32,'r',shape=(1152,1920,32))[448:464,224:240].copy().tofile(out/'skip.f32')
 shutil.copyfile(base/'crop/color.f32',out/'color.f32')
 np.fromfile(base/'crop'/name,np.float32).reshape(16,16,4)[:,:,:3].copy().tofile(out/'oracle.f32')
 for coefficient in ('scales','ffn','attention','head'):shutil.copyfile(base/f'amd/{coefficient}.f32',out/f'{coefficient}.f32')
