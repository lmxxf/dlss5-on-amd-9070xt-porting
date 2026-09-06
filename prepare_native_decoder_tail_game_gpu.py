"""Package continuous49..69 original references with explicit independent skips."""
from pathlib import Path
import json,hashlib
import numpy as np
from native_upsample48_reference import unpack
from native_upsample66_reference import unpack as unpack66
early=Path('release/native-decoder-game-c256');root=Path('release/native-decoder-tail-game');out=root/'amd';out.mkdir(exist_ok=False)
previous=Path('release/native-upsample48/game/output.fp8');previous_hwc=None;checks=[]
shifts={r['block']:r['shift_mask'] for r in json.loads(Path('native-runtime-parameters-1080.json').read_text())['decoder']}
def copy(src,dst): (out/dst).write_bytes(src.read_bytes())
for b in range(49,70):
 if b in (56,62,66):
  src=root/f'upsample{b}';r=json.loads((src/'validation.json').read_text());assert r['different']==0 and r['finite'] and r['tail_zero']
  assert previous_hwc and (src/'input.f32').read_bytes()==previous_hwc.read_bytes(),'disconnected main branch'
  params=unpack66('release/native-upsample66/weights.bin') if b==66 else unpack(src/'weights.bin')
  matrix,scale,body=params;np.concatenate([matrix.ravel(),scale]).astype('<f4').tofile(out/f'block{b}-weights.f32')
  if b==66:
   w1,w2,q,k,v,p,bias,sc,fs,ats=body
   ffn=np.concatenate([np.zeros(512),w1.ravel(),w2.ravel(),fs]);aw=np.concatenate([q.ravel(),k.ravel(),v.ravel(),p.ravel(),bias.ravel(),[sc],ats])
  else:
   f,qkv,p,bias,sc,ss=body;ffn=np.concatenate([f[k].ravel() for k in ('W1','W2','W3','skip')]);aw=np.concatenate([*[m.ravel() for m in qkv],p.ravel(),bias.ravel(),sc,ss])
  ffn.astype('<f4').tofile(out/f'block{b}-ffn.f32');aw.astype('<f4').tofile(out/f'block{b}-attention.f32')
  copy(src/'skip.f32',f'skip{14 if b==56 else 8 if b==62 else 4}.f32')
 else:
  src=(early if b<56 else root)/f'decoder-block{b}';r=json.loads((src/'validation.json').read_text())
  assert r['status']=='pass' and r['different']==0 and Path(r['input'])==previous and r['shift']==shifts[b]
  for name in ('ffn','attention'):copy(src/f'{name}.f32',f'block{b}-{name}.f32')
 previous=src/'output.fp8';previous_hwc=src/'oracle.f32'
 checks.append({'block':b,'source':str(src),'output_sha256':hashlib.sha256(previous.read_bytes()).hexdigest()})
copy(early/'decoder-block49/input.f32','input.f32');copy(previous_hwc,'oracle.f32')
(out/'provenance.json').write_text(json.dumps({'scope':'continuous main49..69; three independent random skips; not RGB chain','stages':checks,'AMD_verified':False},indent=2)+'\n')
print('Packaged21 continuous stages; GPU verification pending')
