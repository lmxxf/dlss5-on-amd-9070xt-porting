"""Check finish dispatch coverage, including the 65535-group row boundary."""
import numpy as np

for width, height in [(8, 8), (64, 64), (968, 584), (1920, 1152)]:
    count = width * height * 32
    groups = count // 64
    gx = min(groups, 65535)
    gy = (groups + 65534) // 65535
    ids = (np.arange(gy)[:, None] * 65535 + np.arange(gx)).ravel()
    valid = ids[ids < groups]
    assert np.array_equal(valid, np.arange(groups))
    # Each selected pixel/channel writes one distinct downsample element.
    pixels = np.arange(width * height)
    x, y = pixels % width, pixels // width
    selected = (x % 2 == 0) & (y % 2 == 0)
    down = (y[selected] // 2) * (width // 2) + x[selected] // 2
    assert np.array_equal(down, np.arange(width * height // 4))
    assert ((y[selected] + 1) < height).all()
    assert ((x[selected] + 1) < width).all()
print('finish dispatch covers main/down outputs once; 2D boundary passed')
