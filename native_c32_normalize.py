"""C32 SASS norm: upper square HMUL, lower square HFMA, then lane tree."""
import numpy as np


def normalize(value):
    half = lambda x: np.asarray(x, np.float16).astype(np.float32)
    # Weight loads +0x2660/+0x2460 are output channels 16..31/0..15.
    # HMUL2 upper square, HFMA2 lower square + rounded upper square.
    sums = half(value[..., :16] * value[..., :16] + half(value[..., 16:] * value[..., 16:]))
    # B operands at +0/+8 bytes carry even/odd output channels. Their
    # HADD comes before lane XOR2/XOR1 and the final packed-half sum.
    sums = half(sums[..., ::2] + sums[..., 1::2])
    for width in (4, 2, 1):
        sums = half(sums[..., :width] + sums[..., width:2*width])
    inverse = half(1 / np.sqrt(np.maximum(sums, np.float32(6.198883056640625e-5))))
    return half(value * inverse)
