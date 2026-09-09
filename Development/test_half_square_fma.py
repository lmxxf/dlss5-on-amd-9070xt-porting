"""Regression for half FMA double rounding at an exact midpoint."""
import numpy as np
from native_c32_normalize import normalize
from pathlib import Path
from native_c64_reference import block,unpack
root=Path('release/native-rgb-valid1080/encoder-c64/window46-18')
v=np.load(root/'query-reference.npz')['k']
got=normalize(v)[0,8,3]
assert got==np.float32(-.0244140625),got
x=np.fromfile(root/'input.f32',np.float32).reshape(1,64,64)
actual=block(x,*unpack(root.parent/'block8.weights'))
expected=np.fromfile(root/'oracle.f32',np.float32).reshape(actual.shape)
assert np.array_equal(actual,expected)
print('Half fused-square regression: key8 normalized value and full original window exact')
