import unittest
import numpy as np
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn

class QuantizeTest(unittest.TestCase):
 def test_representable_values(self):
  codes=np.arange(127,dtype=np.uint8)
  np.testing.assert_array_equal(quantize(e4m3fn(codes)),codes)
  codes=np.arange(129,255,dtype=np.uint8)
  np.testing.assert_array_equal(quantize(e4m3fn(codes)),codes)
 def test_all_midpoints_tie_even(self):
  values=e4m3fn(np.arange(127,dtype=np.uint8))
  mid=(values[:-1]+values[1:])/2
  indices=np.arange(126);expected=np.where(indices%2==0,indices,indices+1).astype(np.uint8)
  np.testing.assert_array_equal(quantize(mid),expected)
  np.testing.assert_array_equal(quantize(-mid[1:]),expected[1:]|0x80)
 def test_saturates_large_finite(self):
  np.testing.assert_array_equal(quantize(np.array([448,480,512,1024,-448,-1024],np.float32)),[126,126,126,126,254,254])

if __name__=='__main__':unittest.main()
