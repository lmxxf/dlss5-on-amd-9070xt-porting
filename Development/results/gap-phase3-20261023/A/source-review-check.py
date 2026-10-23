from pathlib import Path
import hashlib,json
p=Path(__file__).resolve().parent
r={"candidate_patch_sha256":hashlib.sha256((p/'candidate.patch').read_bytes()).hexdigest(),"scope":"CPU enumeration of reviewed grid and source address formulas, not execution of HIP kernel","cases":[]}
for n in (400,640):
 groups=((n+31)//32)*32
 seen=bytearray(n*1024)
 full=tail=0
 for bid in range(groups):
  head=bid%32;first=bid//32*32;two=first+16<n
  full+=two;tail+=not two
  assert first<n
  for q in range(2 if two else 1):
   for rc in range(16):
    tok=first+q*16+rc;assert tok<n
    for gr in range(2):
     for c in range(2):
      for e in range(8):
       idx=tok*1024+head*32+c*16+gr*8+e
       assert idx<len(seen) and seen[idx]==0
       seen[idx]=1
     for k in (0,16):
      lo=tok*1024+head*32+k+gr*8
      assert 0<=lo and lo+8<=n*1024
  # K/V address extrema over the loops; affine monotone in lane/element.
  for key in range(0,n,16):
   for rc in (0,15):
    for gr in (0,1):
     for k in (0,16):
      lo=(n+key+rc)*1024+head*32+k+gr*8
      assert n*1024<=lo and lo+8<=2*n*1024
     for c in (0,1):
      for e in (0,7):
       idx=(2*n+key+gr*8+e)*1024+head*32+c*16+rc
       assert 2*n*1024<=idx<3*n*1024
 assert all(seen)
 oldgroups=n//16*32; keytiles=n//16
 oldwmma=oldgroups*keytiles*5
 newwmma=(full*10+tail*5)*keytiles
 assert oldwmma==newwmma
 # Each lane loads two K x8B and two V x8B per key tile; includes all wave lanes.
 oldbytes=oldgroups*keytiles*32*32
 newbytes=groups*keytiles*32*32
 r['cases'].append(dict(tokens=n,groups=groups,full_groups=full,tail_groups=tail,output_bytes=len(seen),every_output_written_once=True,input_ranges_pass=True,wmma_old=oldwmma,wmma_new=newwmma,logical_kv_requested_bytes_old=oldbytes,logical_kv_requested_bytes_new=newbytes,logical_kv_reduction=1-newbytes/oldbytes))
(p/'source-review-check.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r['cases'],indent=2))
