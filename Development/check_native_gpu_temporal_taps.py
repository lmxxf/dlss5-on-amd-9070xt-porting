"""Compare isolated AMD tap diagnostics with original block14/warp1 registers.

The diagnostic output is NOT RGB and its downstream preblock output is invalid.
"""
from pathlib import Path
import json
import argparse
import numpy as np

root = Path('release/native-temporal-large')
parser=argparse.ArgumentParser()
parser.add_argument('--prefix',default='gpu-taps')
parser.add_argument('--remaining',action='store_true')
parser.add_argument('--valid1080',action='store_true')
args=parser.parse_args()
capture='block4-row6-warp1' if args.remaining else 'block14-warp1'
if args.valid1080:root=Path('release/native-temporal-valid1080');capture='block39-row42-warp1'
rows=json.loads((root/capture/'sample-registers-1590.json').read_text())['rows']
registers = [48,49,44,45,54,45,60,61,56,45,46,50,43,19,51,47]
expected = np.array([[r['raw'][str(k)] for k in registers] for r in rows],np.uint32)
data = np.fromfile(root/(args.prefix+'.f32'),np.float32).reshape(-1,16)
indices = np.array([y*120+x for y in range(4,8) for x in range(112,120)])
if args.remaining:indices=np.array([y*120+x-6240 for y in range(52,56) for x in range(32,40)])
if args.valid1080:indices=np.array([y*1920+x-652800 for y in range(340,344) for x in range(312,320)])
actual = data[indices].copy().view(np.uint32)
assert np.isfinite(actual.view(np.float32)).all()
different = np.argwhere(actual != expected)
report = {'scope':f'isolated GPU tap UV/axis weights vs original PC1590, {capture}; not neural acceptance',
          'different':len(different), 'values':int(actual.size),
          'differences':[{'lane':int(i),'field':int(j),'gpu_bits':hex(int(actual[i,j])),
                          'original_bits':hex(int(expected[i,j]))} for i,j in different]}
(root/(args.prefix+'-validation.json')).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='differences'},indent=2))
