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
early=json.loads((root/'qk-registers-46a0.json').read_text())['rows'][16]['registers']
w=early['12'];half=np.array([w&65535,w>>16],np.uint16).view(np.float16).astype(np.float32)
print({'before_first_conversion_R12':half.tolist(),'reference_normalized':float(normalize(d['k'])[0,8,3])})
before=json.loads((root/'qk-registers-41d0.json').read_text())['rows'][16]['registers']
operands={}
for reg in (96,138):
 word=before[str(reg)];operands[str(reg)]=np.array([word&65535,word>>16],np.uint16).view(np.float16).astype(np.float32).tolist()
print({'normalize_multiply_operands':operands})
(root/'key8-normalize-operands.json').write_text(json.dumps(operands,indent=2)+'\n')
