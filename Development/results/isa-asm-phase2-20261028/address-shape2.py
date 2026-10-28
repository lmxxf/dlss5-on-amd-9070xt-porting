from pathlib import Path
import json
P=Path(__file__).parent;res={}
for stride in [1024,4096]:
 old=[];new=[]
 for n in range(4):
  for e in range(8):old.append([(g*8+e)*stride+n*16+rc for g in range(2) for rc in range(16)])
 for n in range(4):new.append([rc*stride+n*16+g*8+e for g in range(2) for rc in range(16) for e in range(8)])
 assert set(sum(old,[]))==set(sum(new,[])) and len(set(sum(new,[])))==1024
 rows={}
 for block in [32,64,128]:rows[str(block)]={'old_per_instruction_block_incidence':sum(len({x//block for x in op})for op in old),'new_per_instruction_block_incidence':sum(len({x//block for x in op})for op in new),'old_unique_blocks':len({x//block for x in sum(old,[])}),'new_unique_blocks':len({x//block for x in sum(new,[])})}
 res[str(stride)]={'old_instructions':len(old),'new_instructions':len(new),'bytes':1024,'block_model':rows}
(P/'knife2-address-shape.json').write_text(json.dumps({'scope':'logical lane byte-address/block-incidence model only; NOT measured cache transactions or hardware line-size claim','tiles':res},indent=2));print(res)
