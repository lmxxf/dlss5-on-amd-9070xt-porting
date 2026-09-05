"""Check the storage representation; not a substitute for GPU output validation."""
import numpy as np

levels = np.unique(np.concatenate([
    np.arange(8, dtype=np.float32) / 512,
    *[(1 + np.arange(8, dtype=np.float32) / 8) * 2.0**e
      for e in range(-6, 9)],
]))
levels = levels[levels <= 448]
levels = np.concatenate([levels, -levels])
restored = levels.astype(np.float16).astype(np.float32)
assert np.array_equal(levels.view(np.uint32), restored.view(np.uint32))

# Exercise token/channel word addressing and both halfword signs.
source = np.resize(levels, 64 * 32).reshape(64, 32)
half = source.astype(np.float16).view(np.uint16).astype(np.uint32)
packed = (half[:, ::2] | (half[:, 1::2] << 16)).ravel()
decoded = np.empty_like(source)
for token in range(64):
    for channel in range(32):
        word = packed[token * 16 + channel // 2]
        bits = np.uint16((word >> ((channel & 1) * 16)) & 65535)
        decoded[token, channel] = bits.view(np.float16)
assert np.array_equal(source.view(np.uint32), decoded.view(np.uint32))
print(f'{len(levels)} signed finite encodings exact; 64x32 packed addressing exact')
