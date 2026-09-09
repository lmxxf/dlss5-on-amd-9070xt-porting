"""Validate planned GPU address formula against independently generated reflect fixture."""
from pathlib import Path
import numpy as np,json
root=Path('release/native-rgb-reflect1080');w,h,vh=1920,1152,1080
source=np.fromfile('release/native-rgb-valid1080/input-hwc.rgba32f',np.float32).reshape(vh,w,4)
expected=np.fromfile(root/'input-hwc.rgba32f',np.float32).reshape(h,w,4)
y=np.arange(h);period=2*(vh-1);r=y%period;sy=np.where(r<vh,r,period-r)
got=source[sy]
assert np.array_equal(got,expected)
packed=got.reshape(h//8,8,w//8,8,4).transpose(0,2,1,3,4).copy()
packed.tofile(root/'input-tiled.rgba32f')
report={'scope':'CPU index formula versus original-verified reflection fixture; shader execution pending','pixels':h*w,'different':int(np.count_nonzero(got!=expected)),'last_source_row':int(sy[-1]),'dispatch_groups':[w//8,h//8,1]}
(root/'layout-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
