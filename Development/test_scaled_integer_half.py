"""Check integer RNE formulation against exact float64-scaled int32 -> half."""
import numpy as np

def shift_round(m,s):
    if s<=0:return m<<(-s)
    if s>=32:return int(s==32 and m>0x80000000)
    q,rem=m>>s,m&((1<<s)-1)
    return q+int(rem>(1<<(s-1)) or (rem==(1<<(s-1)) and q&1))

def convert(v,p):
    sign=0x8000 if v<0 else 0
    m=abs(v)
    if not m:return 0
    lead=m.bit_length()-1;e=lead+p
    if e < -14:bits=shift_round(m,-p-24)
    else:
        q=shift_round(m,lead-10)
        if q==2048:q=1024;e+=1
        bits=0x7c00 if e>15 else ((e+15)<<10)|(q-1024)
    return sign|bits

rng=np.random.default_rng(297)
v=rng.integers(-(1<<31),1<<31,200000,dtype=np.int64)
p=rng.integers(-90,10,len(v),dtype=np.int32)
edges=[]
for s in range(1,21):
    for q in (0,1,1023,1024,2047):
        for d in (-1,0,1):
            m=(q<<s)+(1<<(s-1))+d
            if 0<=m<(1<<31):
                for e in (-24,-15,0,5):edges.extend([(m,e-s),(-m,e-s)])
edges.extend((n,e) for n in (-(1<<31),(1<<31)-1,0,1,-1) for e in range(-90,11))
v=np.concatenate((v,np.array([a for a,b in edges],dtype=np.int64)))
p=np.concatenate((p,np.array([b for a,b in edges],dtype=np.int32)))
with np.errstate(over='ignore',under='ignore'):
    expected=np.ldexp(v.astype(np.float64),p).astype(np.float16).view(np.uint16)
actual=np.array([convert(int(a),int(b)) for a,b in zip(v,p)],dtype=np.uint16)
assert np.array_equal(actual,expected)
print(f'scaled_integer_half cases={len(v)} bit_different=0')
