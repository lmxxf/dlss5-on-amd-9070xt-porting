from pathlib import Path
import argparse
import numpy as np
root=Path('release/native-c512');out=root/'amd-window';out.mkdir(parents=True,exist_ok=True)
for name in ('ffwd.f32','ffwd-projection.f32','attention.f32'):(out/name).write_bytes((root/'amd'/name).read_bytes())
parser=argparse.ArgumentParser();parser.add_argument('--width',type=int,default=4);parser.add_argument('--height',type=int,default=4);parser.add_argument('--shift',type=int,default=3);args=parser.parse_args()
fixture=np.load(root/f'small-check/fixture-{args.width}x{args.height}-shift{args.shift}.npz')
fixture['input'].astype('<f4').tofile(out/'input.f32')
fixture['oracle_3'].astype('<f4').tofile(out/'oracle-0.f32')
