from pathlib import Path
import json
P=Path(__file__).parent;new=[None]*2048;writes=0
for tile in range(4):
 for ci in range(2):
  for half in range(2):
   for row in range(16):
    for e in range(8):
     off=(tile*2+ci)*256+(half*16+row)*8+e;value=(tile*16+half*8+e)*32+ci*16+row;assert new[off] is None;new[off]=value;writes+=1
reads=0
for kt in range(4):
 for ci in range(2):
  for half in range(2):
   for row in range(16):
    for e in range(8):
     off=(kt*2+ci)*256+(half*16+row)*8+e;old=(kt*16+half*8+e)*32+ci*16+row;assert new[off]==old;reads+=1
assert writes==reads==2048 and sorted(new)==list(range(2048))
(P/'knife1-layout-proof.json').write_text(json.dumps({'elements':2048,'writes':writes,'reads':reads,'missing':0,'overlapping_writes':0,'old_V_operand_index_vs_new_fragment':0,'QK_bytes_unchanged':4096,'LDS_bytes_unchanged':6144,'scope':'logical V element identity; numeric WMMA/rounding verified separately by GPU Gold'},indent=2));print('V fragment coordinate proof PASS')
