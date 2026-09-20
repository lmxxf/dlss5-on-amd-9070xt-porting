# Verify raster pairing through the actual Gather permutation, not just logical token indices.
def gather(t):return (t&~15)|((t&1)<<3)|((t&14)>>1)
def inverse(r):return (r&~15)|((r&7)<<1)|((r&8)>>3)
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
   lu,lv=inverse(first+u),inverse(first+v)
   assert first<=lu<first+32 and first<=lv<first+32
   pairs.append((gather(lu),gather(lv)))
  assert sorted(t for p in pairs for t in p)==list(range(first,first+32))
  for u,v in pairs:
   assert (u//w==v//w and v-u==1) or (u%w==v%w and v-u==w),(first,u,v)
  realw,realh=(25,15) if n==400 else (30,18)
  live=lambda t:t//w<realh and t%w<realw
  eligible=all(live(u)==live(v) for u,v in pairs)
  assert eligible==(n==640 or first<352)
print('PASS: gathered 400/640-token groups partition into true adjacent 2D pairs')
