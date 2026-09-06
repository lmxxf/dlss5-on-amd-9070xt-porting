from pathlib import Path
import json
import numpy as np
from native_c32_normalize import normalize
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18')
r=json.loads((root/'qk-registers-46d0.json').read_text())['rows'][16]['registers'];d=np.load(root/'query-reference.npz')
def halves(reg):
 w=r[str(reg)];return np.array([w&65535,w>>16],np.uint16).view(np.float16).astype(np.float32).tolist()
result={'scope':'preconversion diagnostic; R11 half does not match candidate K coordinate, merge mapping unresolved','lane':16,'R11_halves':halves(11),'R12_word':hex(r['12']),'reference_K8_channel3_raw':float(d['k'][0,8,3]),'reference_K8_channel3_normalized_half':float(normalize(d['k'])[0,8,3])}
(root/'key8-prequant.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
