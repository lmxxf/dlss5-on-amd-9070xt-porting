from pathlib import Path
from array import array
import hashlib,json,sys
w,h=1920,1072
p=Path('/home/lmxxf/work/dlss5-gap-phase4-20261023/knife2/free1072-input.f16')
with p.open('wb')as f:
 for y in range(h):
  row=array('H')
  for x in range(w):row.extend([0x2000+(x*17+y*31+c*53)%10240 for c in range(3)]+[0x3c00])
  if sys.byteorder!='little':row.byteswap()
  row.tofile(f)
assert p.stat().st_size==w*h*8
r={'file':str(p),'width':w,'height':h,'format':'little-endian RGBA16F','kind':'controlled synthetic gradient, not real capture','formula':'rgb half bits=0x2000+(x*17+y*31+c*53)%10240; alpha=0x3c00','bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
Path(__file__).with_suffix('.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
