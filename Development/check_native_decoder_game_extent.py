"""Probe real decoder39 crop with independent random main/skip features."""
from pathlib import Path
import subprocess,os,json
import numpy as np
from native_c32_reference import F,H
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
from native_decoder_entry_reference import unpack,project
root=Path('release/native-decoder-game');root.mkdir(exist_ok=False)
inv=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
rng=np.random.default_rng(3012)
x=F(rng.normal(0,.25,(20,32,1024)).astype(np.float32));skip=F(rng.normal(0,.25,(36,60,512)).astype(np.float32))
def pack(v):
 h,w,c=v.shape;b=c//512
 cells=quantize(v).reshape(h//4,4,w//4,4,b,512).transpose(0,2,4,1,3,5).reshape(-1,8192)
 out=np.empty_like(cells);out[:,inv]=cells;return out
pack(x).tofile(root/'main.fp8');pack(skip).tofile(root/'skip.fp8')
x.tofile(root/'main.f32');skip.tofile(root/'skip.f32')
subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll','block39.layer0.layer',str(root/'weights.bin')],check=True,capture_output=True)
env=dict(os.environ,DLSS5_DECODER_GAME_EXTENT='1')
subprocess.run(['/tmp/native-decoder-entry-oracle','/tmp/dlssnr-cubins/dlssnr-06.cubin',str(root/'main.fp8'),str(root/'skip.fp8'),str(root/'weights.bin'),str(root/'result'),'32','20','--run'],env=env,check=True,timeout=20)
raw=np.fromfile(root/'result.output.fp8',np.uint8);n=36*60*512
actual=e4m3fn(raw[:n].reshape(-1,8192)[:,inv]).reshape(9,15,4,4,512).transpose(0,2,1,3,4).reshape(skip.shape)
matrix,scale=unpack(root/'weights.bin');up=np.repeat(np.repeat(project(x,matrix),2,0),2,1)
checks=[]
for y,z in [(0,0),(2,2),(4,4)]:
 expected=F(H(up[y:y+36,z:z+60]+skip*scale));delta=np.abs(expected-actual)
 checks.append({'crop_yx':[y,z],'different':int(np.count_nonzero(delta)),'max_abs':float(delta.max())})
report={'scope':'decoder game-extent crop candidates; not AMD/game acceptance','finite':bool(np.isfinite(actual).all()),'tail_zero':not bool(raw[n:].any()),'checks':checks}
print(json.dumps(report,indent=2));(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['finite'] and report['tail_zero'] and checks[0]['different']==0
