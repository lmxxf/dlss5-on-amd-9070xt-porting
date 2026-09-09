"""Half-add graph recovered by trace_preblock_softmax.py, not a balanced tree."""
import numpy as np


def denominator(exp):
    half = lambda x: np.asarray(x, np.float16).astype(np.float32)
    parity = []
    for odd in (0, 1):
        partial = []
        for lane in (0, 2, 8, 10):
            base = odd + lane
            value = half(exp[..., base] + exp[..., base+16])
            for offset in (4, 32, 36):
                value = half(value + half(exp[..., base+offset] + exp[..., base+offset+16]))
            partial.append(value)
        total = partial[0]
        for value in partial[1:]:
            total = half(total + value)
        parity.append(total)
    return half(parity[0] + parity[1])[..., None]
