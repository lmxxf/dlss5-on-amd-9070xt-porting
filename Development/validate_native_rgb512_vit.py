"""Compare full AMD RGB512 -> ViT38 against the original RGB-derived oracle."""
from pathlib import Path
import json,hashlib
import numpy as np
from native_split_reference import bits
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb512');meta=json.loads((root/'bridge.json').read_text());job=Path(meta['original_vit_chain'])
report=json.loads((job/'validation.json').read_text());assert report['status']=='pass' and len(report['stages'])==56 and all(s['different']==0 for s in report['stages'])
assert report['input_sha256']==hashlib.sha256((root/'vit-input.fp8').read_bytes()).hexdigest()
raw=np.fromfile(job/'block38/projection.fp8',np.uint8);assert not np.any(raw[65536:]) and not np.any((raw[:65536]&127)==127)
target=np.empty((64,1024),np.float32);target[bits(65536,[2,6,7,8,14,15]),bits(65536,[0,1,3,4,5,9,10,11,12,13])]=e4m3fn(raw[:65536])
actual=np.fromfile(root/'amd/output-rgb512-vit.f32','<f4').reshape(target.shape);assert np.isfinite(actual).all()
error=np.abs(actual-target);result={'status':'pass' if np.array_equal(actual,target) else 'fail','RGB_extent':[512,512],'last_block':38,'values':65536,'different':int(np.count_nonzero(error)),'max_error':float(error.max()),'scope':'RGB encoder and ViT, not decoder or game image acceptance'}
(root/'amd/validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
assert np.array_equal(actual,target),'RGB512 to ViT differs'
