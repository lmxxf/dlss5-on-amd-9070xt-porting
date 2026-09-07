"""Check the normal-half midpoint correction used after C32 projection."""
import numpy as np
rng=np.random.default_rng(7311)
a=(rng.integers(-1048576,1048577,1000000).astype(np.float32)/1024)
b=rng.uniform(-.01,.01,a.size).astype(np.float16).astype(np.float32)
total=a+b;virtual_b=total-a;error=(a-(total-virtual_b))+(b-virtual_b)
bits=total.view(np.uint32).copy();magnitude=bits&0x7fffffff
mask=(magnitude>=0x38800000)&(magnitude<0x47800000)&((magnitude&8191)==4096)&(error!=0)
increase=(error>0)==(total>0)
bits[mask&increase]+=1;bits[mask&~increase]-=1
actual=bits.view(np.float32).astype(np.float16)
expected=(a.astype(np.float64)+b.astype(np.float64)).astype(np.float16)
domain=(np.abs(total)>=2**-14)&(np.abs(total)<65504)
different=np.count_nonzero(actual[domain]!=expected[domain])
print({'scope':'normal-half result domain, CPU emulation of shader correction','values':int(domain.sum()),'corrected':int(mask.sum()),'different':int(different)})
assert different==0
