"""Separate per-block arithmetic error from accumulated resident-chain error."""
import json
from pathlib import Path
import numpy as np
from native_c32_reference import block, unpack
from decode_tinlayout_global import e4m3fn


def metrics(actual, expected):
    assert actual.shape == expected.shape
    assert np.isfinite(actual).all() and np.isfinite(expected).all()
    error = np.abs(actual - expected)
    return dict(correlation=float(np.corrcoef(actual.ravel(), expected.ravel())[0, 1]),
                exact_fraction=float(np.mean(actual == expected)),
                mae=float(error.mean()), max_error=float(error.max()))


def stage(source, weights, shift):
    height, width, _ = source.shape
    px, py = (4 if shift & 1 else 0), (4 if shift & 2 else 0)
    padded = np.pad(source, ((py, py), (px, px), (0, 0)))
    hh, ww = padded.shape[:2]
    tiles = padded.reshape(hh//8, 8, ww//8, 8, 32).transpose(0, 2, 1, 3, 4).reshape(-1, 64, 32)
    result = block(tiles, weights).reshape(hh//8, ww//8, 8, 8, 32).transpose(0, 2, 1, 3, 4).reshape(padded.shape)
    return result[py:py+height, px:px+width]


def main():
    root = Path('release/native-c32')
    mapping = np.fromfile('release/post-skip-basis/matrix.f32', '<f4').reshape(2048, 2048)
    cell_map = np.argmax(np.abs(mapping), axis=0).reshape(8, 8, 32)[:4, :4]
    def cells(name):
        raw = np.fromfile(root/name, np.uint8)
        assert raw.size >= 64*32*32
        raw = raw[:64*32*32]  # Oracle allocation includes a trailing guard region.
        return e4m3fn(raw.reshape(-1, 512)[:, cell_map]).reshape(8, 16, 4, 4, 32).transpose(0, 2, 1, 3, 4).reshape(32, 64, 32)
    original = e4m3fn(np.fromfile('release/preblock-global/ds.fp8', np.uint8)).reshape(2, 32, 64, 16).transpose(1, 2, 0, 3).reshape(32, 64, 32)
    amd = np.fromfile('release/preblock-global/amd-down.f32', '<f4').reshape(original.shape)
    chains = {'original_ds_start': original.copy(), 'amd_ds_start': amd.copy()}
    report = {'preblock_ds': metrics(amd, original), 'stages': []}
    for index, shift, name in [(1, 0, 'block1-inpview-output.fp8'), (2, 3, 'block2-output.fp8'), (3, 1, 'block3-output.fp8')]:
        weights = unpack(root/f'block{index}.weights')
        target = cells(name)
        row = {'block': index, 'shift_mask': shift, 'isolated': metrics(stage(original, weights, shift), target)}
        for key, source in chains.items():
            chains[key] = stage(source, weights, shift)
            row[key] = metrics(chains[key], target)
        report['stages'].append(row)
        original = target
    resident = np.fromfile('release/native-front-chain/output.f32', '<f4').reshape(original.shape)
    report['resident_vs_cpu_chain'] = metrics(resident, chains['amd_ds_start'])
    report['resident_vs_original'] = metrics(resident, original)
    # Wiring regression is exact; numerical agreement with NVIDIA is reported,
    # deliberately not disguised as an end-to-end image acceptance threshold.
    assert np.array_equal(resident, chains['amd_ds_start']), 'resident wiring regression'
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
