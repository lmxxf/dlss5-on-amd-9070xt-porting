"""Check float compensation for the bounded cubic interpolation expression."""
import numpy as np
from native_temporal_sampling_reference import fma32
t=np.r_[np.linspace(0,1,1000001,dtype=np.float32),np.random.default_rng(7310).random(1000000,dtype=np.float32)]
square=t*t;cube=square*t;scaled=square*np.float32(2.5)
product=cube*np.float32(1.5)
product_error=(cube-product)+cube*np.float32(.5)
total=product-scaled
virtual_b=total-product
sum_error=(product-(total-virtual_b))+(-scaled-virtual_b)
corrected=total+(sum_error+product_error)
expected=fma32(cube,1.5,-scaled)
different=int(np.count_nonzero(corrected!=expected))
print({'scope':'CPU bounded interpolation expression only','values':t.size,'different':different})
assert different==0
