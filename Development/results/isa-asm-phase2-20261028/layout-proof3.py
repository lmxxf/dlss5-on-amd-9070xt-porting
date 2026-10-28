from pathlib import Path
import json
P=Path(__file__).parent;cases=0
for C in [64,128,256,512]:
 for width,height in [(1,1),(5,3),(4,4),(17,1),(50,30),(32,20),(28,16)]:
  M=width*height;N=2*C
  for first in sorted({0,(M-1)//16*16}):
   for col in sorted({0,N-64}):
    for vw,vh in [(0,0),(width,height),(max(1,width-2),max(1,height-1))]:
     a={};b={}
     for j in range(4):
      for g in range(2):
       for rc in range(16):
        for e in range(8):
         q=first+g*8+e;c=col+j*16+rc
         if q<M:a[q*N+c]=0 if vw and(q%width>=vw or q//width>=vh) else q*N+c+1
         q=first+rc;c=col+j*16+g*8+e
         if q<M:b[q*N+c]=0 if vw and(q%width>=vw or q//width>=vh) else q*N+c+1
     assert a==b;cases+=1
points=0
for first in [0,16,1488]:
 for row in range(0,512,64):
  seen=set()
  for j in range(4):
   for g in range(2):
    for rc in range(16):
     for e in range(8):
      r=first+rc;c0=row+j*16+g*8;c=c0+e
      old=((r//16)*16+c//32)*512+(r%16)*32+c%32
      new=((first//16)*16+c0//32)*512+rc*32+c0%32+e
      assert old==new and new not in seen;seen.add(new);points+=1
  assert len(seen)==1024
(P/'knife3-layout-proof.json').write_text(json.dumps({'pool_cases':cases,'pool_width_height_tail_and_valid_crop_identity':True,'split_checked_element_offsets':points,'split_tile_byte_indices_identical':True,'overlaps':0,'scope':'address and mask coverage; arithmetic separately checked by seven-profile GPU Gold'},indent=2));print('pool cases',cases,'split offsets',points,'PASS')
