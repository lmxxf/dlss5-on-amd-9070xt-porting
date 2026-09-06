"""Check split-product quantization without overflowing a shader uint32."""
import numpy as np
rng=np.random.default_rng(7307)
u=np.r_[np.arange(0,2097153,257,dtype=np.uint64),rng.integers(0,2097153,100000,dtype=np.uint64)]
for extent in (8,24,72,120,1080,1152,1920,16384):
    split=(u>>13)*extent+(((u&8191)*extent+4096)>>13)
    direct=(u*extent+4096)>>13
    assert np.array_equal(split,direct)
    assert int(((u&8191)*extent+4096).max())<2**32
print('split uint32 product: all eight extents exact vs uint64 reference')
