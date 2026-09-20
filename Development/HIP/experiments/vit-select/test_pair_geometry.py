# Check the exact pairing used by packed_prep.hip: every token occurs once, no long-distance pair.
for n,w in [(400,25),(640,32)]:
 for first in range(0,n,32):
  if first+31>=n:continue
  a=b=32;x=first%w
  if w==25:
   if x%2==0:a,b=0,25
   elif x>=19:a=25-x;b=a+25
  pairs=[]
  for j in range(16):
   u,v=2*j,2*j+1
   if a<32:
    if j==15:u,v=a,b
    else:
     u+=u>=a;u+=u>=b;v+=v>=a;v+=v>=b
   pairs.append((first+u,first+v))
  assert sorted(t for p in pairs for t in p)==list(range(first,first+32))
  for u,v in pairs:
   assert (u//w==v//w and v-u==1) or (u%w==v%w and v-u==w),(first,u,v)
print('PASS: full 400/640-token groups are exact partitions into adjacent 2D pairs')
