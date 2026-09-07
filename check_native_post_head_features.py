"""Compare captured HMMA input value multiset, without assuming its lane layout."""
from pathlib import Path
from collections import Counter
import json,numpy as np
from native_post70_reference import unpack,H
from native_c32_reference import block,F
root=Path('release/native-temporal-valid1080/post70');crop=root/'crop'
main=np.memmap(root/'main.f32',np.float32,'r',shape=(576,960,32))[224:232,112:120].copy()
skip=np.memmap(root/'skip.f32',np.float32,'r',shape=(1152,1920,32))[448:464,224:240].copy()
body,sm,ss,head=unpack('release/native-post70/smoke/weights.bin')
merged=H(H(np.repeat(np.repeat(main,2,0),2,1)*sm)+skip*ss)
debug={}
features=block(merged.reshape(2,8,2,8,32).transpose(0,2,1,3,4).reshape(-1,64,32),body,raw_output=True,debug=debug).reshape(2,2,8,8,32).transpose(0,2,1,3,4).reshape(16,16,32)
def raw(pc,rs):
    rows=json.loads((crop/f'registers-{pc}.json').read_text())['rows']
    return np.array([[r['raw'][str(k)] for k in rs] for r in rows],np.uint32).view(np.uint16).ravel()
captured=np.r_[raw('c0a0',[48,49,50,51,12,13,14,15]),raw('c100',[104,105,106,107,108,109,110,111])]
expected=features[4:8,8:16].astype(np.float16).view(np.uint16)
a,b=Counter(map(int,captured)),Counter(map(int,expected.ravel()))
def detail(items):return [{'bits':hex(k),'value':float(np.uint16(k).view(np.float16)),'count':n} for k,n in items.items()]
report={'scope':'post HMMA input multiset vs CPU body; not lane-layout or full feature proof',
        'captured_only':detail(a-b),'reference_only':detail(b-a),
        'reference_locations':[{ 'bits':hex(k),'local_y_x_channel':np.argwhere(expected==k).tolist()} for k in (b-a)]}
av=F(debug['attention_value'])[1,48].astype(np.float64)
weights=debug['projection_weights'][12].astype(np.float64)
residual=H(debug['feature'][1,48,12]*debug['residual_scale'][12])
dot=np.sum(av*weights)
report['projection_precision_isolation']={'dot64':float(dot),'residual':float(residual),
 'sum64':float(dot+residual),'half_from_sum64':float(H(dot+residual)),
 'half_from_float32_sum':float(H(np.float32(np.float32(dot)+residual)))}
(crop/'head-feature-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
