"""Separate original temporal coordinate error from first texture lookup error."""
from pathlib import Path
import json,struct
import numpy as np
from native_temporal_sampling_reference import geometry,bilinear
root=Path('release/native-temporal-inputs-gates')
def rows(pc):
    r=json.loads((root/f'sample-registers-{pc:x}.json').read_text())
    assert r['pc_offset']==pc and len({x['pc'] for x in r['rows']})==1
    return r['rows']
def value(row,reg):return struct.unpack('<f',struct.pack('<I',row['raw'][str(reg)]))[0]
r=rows(0x1590)
actual_xy=np.array([[[value(a,x)*8,value(a,y)*8] for x,y in [(48,49),(44,45),(54,45),(60,61),(56,45)]] for a in r])
lane=np.arange(32);xy,w=geometry(lane%8+.625,lane//8+.625,8,8)
top=np.array([[value(a,k) for k in (48,49,53)] for a in rows(0x16b0)])
center=np.array([[value(a,k) for k in (54,55,58)] for a in rows(0x1730)])
history=np.fromfile(root/'history.f32',np.float32).reshape(8,8,4)[:,:,:3]
report={'scope':'controlled warp0 .125 displacement; not whole sampler numerical acceptance',
        'coordinate_max_abs_pixels':float(np.abs(actual_xy-xy).max()),
        'first_TEX_ideal_max_abs':float(np.abs(bilinear(history,actual_xy[:,0])-top).max()),
        'first_TEX_fraction8_max_abs':float(np.abs(bilinear(history,actual_xy[:,0],8)-top).max()),
        'center_TEX_fraction8_max_abs':float(np.abs(bilinear(history,actual_xy[:,2],8)-center).max()),
        'center_TEX_fraction8_product8_max_abs':float(np.abs(bilinear(history,actual_xy[:,2],8,8)-center).max())}
(root/'coordinate-comparison.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
