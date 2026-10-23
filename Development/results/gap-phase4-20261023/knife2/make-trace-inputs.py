from pathlib import Path
from array import array
import hashlib,json,sys
W=Path('/home/lmxxf/work/dlss5-gap-phase4-20261023/knife2/trace-inputs');W.mkdir(exist_ok=True)
profiles=[('small900',1600,900,1600,1024,448),('small720',1280,720,1344,768,288),('smallfree1088',1920,1072,1920,1088,640),('small1080',1920,1080,1920,1152,640)]
cache={};rows=[]
for name,vw,vh,w,h,n in profiles:
 p=W/(name+'.rgba32f')
 with p.open('wb')as f:
  for y in range(h):
   ry=y if y<vh else 2*vh-y-2;key=(w,vw,ry%256)
   if key not in cache:
    a=array('f')
    for x in range(w):
     rx=x if x<vw else 2*vw-x-2
     a.extend([(1+((rx*17+ry*31+c*53)%256))/8192.0 for c in range(3)]+[1.0])
    if sys.byteorder!='little':a.byteswap()
    cache[key]=a.tobytes()
   f.write(cache[key])
 assert p.stat().st_size==w*h*16
 rows.append(dict(profile=name,path=str(p),valid=[vw,vh],processing=[w,h],tokens=n,bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
receipt={'format':'little-endian encoded RGBA32F synthetic; each RGB also exact binary16','formula':'rgb=(1+((reflected_x*17+reflected_y*31+c*53)%256))/8192; alpha1; mirror=2*extent-coordinate-2','not_real_capture':True,'profiles':rows}
Path(__file__).with_suffix('.json').write_text(json.dumps(receipt,indent=2));print(json.dumps(receipt,indent=2))
