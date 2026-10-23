from pathlib import Path
import json,hashlib
root=Path(__file__).resolve().parent
results=[]
for n in (400,640):
 plane=n*1024;size=3*plane;groups=(n//16+4)//5;prod_wg=groups*32*3
 for digit in range(3):
  # Coordinate's three byte digits distinguish every original value; this is post-byte_F identity, not a new quantizer.
  old=bytearray((i>>(digit*8))&255 for i in range(size))
  out=bytearray([0xa5])*32+bytearray(size)+bytearray([0x5a])*32
  visits=bytearray(size);invalid=0
  for bid in range(prod_wg):
   perpart=groups*32;part=bid//perpart;local=bid%perpart;row=(local%32)*32
   for wv in range(5):
    first=(local//32*5+wv)*16
    if first>=n:invalid+=1;continue
    for lane in range(32):
     gr=lane//16;rc=lane%16
     if part==2:
      at=2*plane+((row//32)*(n//16)+first//16)*512+lane*16
      assert at%16==0 and 2*plane<=at and at+16<=3*plane
      packed=bytearray(16)
      for j in range(2):
       for e in range(8):packed[j*8+e]=old[(part*n+first+gr*8+e)*1024+row+j*16+rc]
      out[32+at:32+at+16]=packed
      for j in range(16):assert visits[at+j]==0;visits[at+j]=1
     else:
      for j in range(2):
       for e in range(8):
        at=(part*n+first+gr*8+e)*1024+row+j*16+rc
        assert at<2*plane and visits[at]==0
        out[32+at]=old[at];visits[at]=1
  assert all(visits) and not invalid
  assert out[:32]==bytes([0xa5])*32 and out[-32:]==bytes([0x5a])*32
  assert out[32:32+2*plane]==old[:2*plane]
  seen=bytearray(plane)
  for key in range(0,n,16):
   for head in range(32):
    for lane in range(32):
     gr=lane//16;rc=lane%16
     at=2*plane+(head*(n//16)+key//16)*512+lane*16
     packed=out[32+at:32+at+16]
     for c in range(2):
      for e in range(8):
       logical=(key+gr*8+e)*1024+head*32+c*16+rc
       assert packed[c*8+e]==old[2*plane+logical]
       assert seen[logical]==0;seen[logical]=1
  assert all(seen)
 results.append({'tokens':n,'producer_groups':prod_wg,'attention_groups_unchanged':n//16*32,'total_bytes':size,'V_base':2*plane,'V_bytes':plane,'coordinate_byte_passes':3,'QK_unchanged':True,'all_V_bytes_match_original_consumer':True,'every_producer_byte_written_once':True,'every_consumer_byte_mapped_once':True,'all_16B_addresses_aligned':True,'guard_bytes_unchanged':True,'inactive_wave_count_in_these_shapes':0})
receipt={'scope':'CPU exhaustive address/byte permutation check; arbitrary post-byte_F bytes, not GPU quantization arithmetic','cases':results}
patch=root/'candidate.patch'
if patch.exists():receipt['candidate_patch_sha256']=hashlib.sha256(patch.read_bytes()).hexdigest()
(root/'mapping-review.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(results,indent=2))
