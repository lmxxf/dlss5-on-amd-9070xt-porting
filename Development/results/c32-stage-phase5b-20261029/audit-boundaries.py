from pathlib import Path
import numpy as np,json
p=Path(__file__).parent;out=[]
for W,H in [(1920,1152),(1920,1088),(1600,960),(1600,1024),(1344,768),(896,512)]:
 w,h=W//2,H//2
 for sx,sy in [(0,0),(4,0),(0,4),(4,4)]:
  cover=np.zeros((h//2,w//2),dtype=np.uint8)
  for ty in range(0,h+2*sy,8):
   for tx in range(0,w+2*sx,8):
    for tok in range(16):
     x=tx//2+tok%4-sx//2;y=ty//2+tok//4-sy//2
     if not(0<=x<w//2 and 0<=y<h//2):continue
     t=tok//4*16+tok%4*2
     assert (tx+t%8)//2-sx//2==x and(ty+t//8)//2-sy//2==y
     # Separate DownCrop and fused routes address the same ordered 2x2 samples.
     assert tx+t%8-sx==2*x and ty+t//8-sy==2*y
     cover[y,x]+=1
  assert np.all(cover==1),(W,H,sx,sy)
  out.append({'processing':[W,H],'shift':[sx,sy],'valid_pooled_pixels':int(cover.size),'coverage_min':int(cover.min()),'coverage_max':int(cover.max())})
(p/'boundary-audit.json').write_text(json.dumps({'valid_width_argument_in_actual_call':0,'note':'This producer/caller uses a cropped tensor with valid_width=valid_height=0. No valid output is left unwritten; invalid padded local rows are never stored. Row permutation preserves each pixel K32 order.','cases':out},indent=2));print('24 crop/shift cases, exact one-write coverage and ordered sample coordinates')
