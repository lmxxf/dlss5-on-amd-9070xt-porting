"""Export original C32 upsample oracle, half-merge coefficients and C32 body."""
from pathlib import Path
import json,argparse
import numpy as np
from native_upsample66_reference import unpack
p=argparse.ArgumentParser();p.add_argument('--game-extent',action='store_true');a=p.parse_args()
base=Path('release/native-upsample66');source=base/('game' if a.game_extent else 'spatial-256-2707-3');out=base/('amd-game' if a.game_extent else 'amd');out.mkdir(exist_ok=False)
report=json.loads((source/'validation.json').read_text());assert report['different']==0
if a.game_extent:assert report['finite'] and report['tail_zero'] and (source/'oracle.f32').stat().st_size==960*576*32*4
else:assert report['status']=='pass' and report['values']==2097152
for src,dst in [('input.f32','input.f32'),('skip.f32','residual.f32'),('oracle.f32','oracle.f32')]:(out/dst).write_bytes((source/src).read_bytes())
matrix,scale,body=unpack(base/'weights.bin');np.concatenate([matrix.ravel(),scale]).astype('<f4').tofile(out/'weights.f32')
w1,w2,q,k,v,p,bias,scale,fs,ats=body
np.concatenate([np.zeros(512),w1.ravel(),w2.ravel(),fs]).astype('<f4').tofile(out/'ffn.f32')
np.concatenate([q.ravel(),k.ravel(),v.ravel(),p.ravel(),bias.ravel(),[scale],ats]).astype('<f4').tofile(out/'attention.f32')
np.array([0 if a.game_extent else report['shift']],'<f4').tofile(out/'shift.f32')
(out/'provenance.json').write_text(json.dumps({'fixture':str(source),'channels':32,'oracle':'original block66 output','AMD_verified':False},indent=2)+'\n')
