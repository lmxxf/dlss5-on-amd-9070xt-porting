import json,pathlib
rows=[]
for n in [400,640]:
 out=bytearray(n*1024);q=bytearray(n*1024);groups=((n+31)//32)*32;full=tail=0
 for bid in range(groups):
  head=bid%32;first=bid//32*32;two=first+16<n
  full+=two;tail+=not two
  for lane in range(32):
   rc=lane%16;gr=lane//16
   for query in range(2 if two else 1):
    for k in range(0,32,16):
     a=(first+query*16+rc)*1024+head*32+k+gr*8
     assert 0<=a and a+8<=n*1024
     for j in range(a,a+8):q[j]+=1
    for c in range(2):
     for e in range(8):
      a=(first+query*16+rc)*1024+head*32+c*16+gr*8+e
      assert 0<=a<len(out);out[a]+=1
   for key in range(0,n,16):
    for k in range(0,32,16):
     a=(n+key+rc)*1024+head*32+k+gr*8;assert n*1024<=a and a+8<=2*n*1024
    for c in range(2):
     for e in range(8):
      a=(2*n+key+gr*8+e)*1024+head*32+c*16+rc;assert 2*n*1024<=a<3*n*1024
 assert set(out)=={1} and set(q)=={1}
 oldgroups=n//16*32;keys=n//16
 rows.append(dict(tokens=n,old_groups=oldgroups,new_groups=groups,full_dual_query_groups=full,tail_single_query_groups=tail,Q_and_output_each_byte_once=True,K_V_all_addresses_in_bounds=True,old_wmma=oldgroups*keys*5,new_wmma=(full*10+tail*5)*keys,old_logical_KV_bytes=oldgroups*keys*1024,new_logical_KV_bytes=groups*keys*1024))
p=pathlib.Path(__file__).with_suffix('.json');p.write_text(json.dumps(rows,indent=2));print(json.dumps(rows,indent=2))
