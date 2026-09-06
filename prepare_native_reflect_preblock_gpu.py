"""Decode original valid1080 preblock branches as independent GPU test oracles."""
from pathlib import Path
import json
import numpy as np
from decode_tinlayout_global import e4m3fn
source=Path('release/native-rgb-valid1080');out=source/'amd-preblock';out.mkdir(exist_ok=False)
h,w=1152,1920
basis=np.fromfile('release/post-skip-basis/matrix.f32','<f4').reshape(2048,2048);mapping=np.argmax(abs(basis),axis=0).reshape(8,8,32)[:4,:4].ravel()
main=np.fromfile(source/'block0-main.fp8',np.uint8);down=np.fromfile(source/'block0-down.fp8',np.uint8)
assert main.size==h*w*32 and down.size==h*w*8
assert not np.any((main&127)==127) and not np.any((down&127)==127)
# Explicit logical HWC shape for the CPU readback oracle.
logical=e4m3fn(main.reshape(-1,512)[:,mapping]).reshape(h//4,w//4,4,4,32).transpose(0,2,1,3,4).reshape(h,w,32)
logical.tofile(out/'main-oracle.f32')
e4m3fn(down).reshape(2,h//2,w//2,16).transpose(1,2,0,3).copy().tofile(out/'down-oracle.f32')
(out/'input.f32').write_bytes((source/'input-hwc.rgba32f').read_bytes())
for name in ('ffn','attention'):
 (out/f'block0-{name}.f32').write_bytes((Path('release/native-rgb512/amd')/f'block0-{name}.f32').read_bytes())
(out/'provenance.json').write_text(json.dumps({'scope':'valid1080 single texture, original preblock main/down decoded; GPU execution pending','processing_HW':[h,w],'main_values':h*w*32,'down_values':h*w*8,'seed':0},indent=2)+'\n')
