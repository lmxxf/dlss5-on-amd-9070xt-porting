"""Validate float32 sum-residual half rounding against a float64 reference."""
import numpy as np
rng=np.random.default_rng(3020)
codes=rng.integers(0,0x7c00,(2,1000000),dtype=np.uint16)
a,b=codes.view(np.float16).astype(np.float32)
with np.errstate(over='ignore',invalid='ignore'):
 square=a*a;upper=(b*b).astype(np.float16).astype(np.float32);summed=square+upper
 large=np.maximum(square,upper);small=np.minimum(square,upper);error=small-(summed-large)
 rounded=summed.astype(np.float16).astype(np.float32);expected=(a.astype(np.float64)**2+upper.astype(np.float64)).astype(np.float16).astype(np.float32)
 valid=np.isfinite(summed)&np.isfinite(rounded)&(rounded!=summed)&(error!=0)
 bits=rounded[valid].astype(np.float16).view(np.uint16);lowbits=bits-(summed[valid]<rounded[valid]).astype(np.uint16)
 lo=lowbits.view(np.float16).astype(np.float32);hi=(lowbits+1).astype(np.uint16).view(np.float16).astype(np.float32)
 mid=(lo+hi)*.5
 rounded[valid]=np.where(summed[valid]==mid,np.where(error[valid]>0,hi,lo),rounded[valid])
 rounded[(summed==65520)&(error<0)]=65504
assert np.array_equal(rounded,expected),np.count_nonzero(rounded!=expected)
print('1000000 finite-half operand pairs: float32 residual algorithm matches single half rounding')
