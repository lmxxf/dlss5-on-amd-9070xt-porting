"""Package the same-RGB full GPU chain: input, weights, maps, comparison oracles."""
from pathlib import Path
import json
import numpy as np
from native_upsample48_reference import unpack
from native_upsample66_reference import unpack as unpack66

base = Path('release/native-rgb-valid1080')
out = base / 'amd-full'
post_report = json.loads((base / 'post70/validation.json').read_text())
assert post_report['different'] == 0 and post_report['finite']
out.mkdir(exist_ok=False)
def copy(src, name):
    (out / name).write_bytes(src.read_bytes())
for src in (base / 'amd-frontvit').iterdir():
    if src.name.startswith('block') or src.name in ('input.f32', 'head-matrix.f32', 'hwc-to-vit.i32', 'oracle-head.f32'):
        copy(src, src.name)
copy(Path('release/native-vit/repack640/vit-to-hwc.i32'), 'vit-to-hwc.i32')
copy(base / 'decoder39/weights.f32', 'decoder39-weights.f32')
previous = base / 'decoder39/result.output.fp8'
previous_hwc = base / 'decoder39/oracle.f32'
for b in range(40, 70):
    if b in (48, 56, 62, 66):
        src = base / f'upsample{b}'
        r = json.loads((src / 'validation.json').read_text())
        assert r['different'] == 0 and r['finite'] and r['tail_zero']
        assert (src / 'input.f32').read_bytes() == previous_hwc.read_bytes()
        encoder, channels = {48: (22, 256), 56: (14, 128), 62: (8, 64), 66: (4, 32)}[b]
        skip = base / f'encoder-c{channels}/block{encoder}-main.f32'
        assert (src / 'skip.f32').read_bytes() == skip.read_bytes()
        weights = Path('release/native-upsample48/block48.weights') if b == 48 else src / 'weights.bin'
        matrix, scale, body = unpack66('release/native-upsample66/weights.bin') if b == 66 else unpack(weights)
        np.concatenate([matrix.ravel(), scale]).astype('<f4').tofile(out / f'block{b}-weights.f32')
        if b == 66:
            w1,w2,q,k,v,p,bias,sc,fs,ats = body
            ffn = np.concatenate([np.zeros(512),w1.ravel(),w2.ravel(),fs])
            aw = np.concatenate([q.ravel(),k.ravel(),v.ravel(),p.ravel(),bias.ravel(),[sc],ats])
        else:
            f,qkv,p,bias,sc,ss = body
            ffn = np.concatenate([f[k].ravel() for k in ('W1','W2','W3','skip')])
            aw = np.concatenate([*[m.ravel() for m in qkv],p.ravel(),bias.ravel(),sc,ss])
        ffn.astype('<f4').tofile(out / f'block{b}-ffn.f32')
        aw.astype('<f4').tofile(out / f'block{b}-attention.f32')
    else:
        group = 'split' if b < 48 else 'c256' if b < 56 else 'c128' if b < 62 else 'c64' if b < 66 else 'c32'
        src = base / f'decoder-{group}/decoder-block{b}'
        r = json.loads((src / 'validation.json').read_text())
        assert r['status'] == 'pass' and Path(r['input']) == previous
        names = ('ffwd','ffwd-projection','attention') if b < 48 else ('ffn','attention')
        for name in names:
            copy(src / f'{name}.f32', f'block{b}-{name}.f32')
    previous = src / 'output.fp8'
    previous_hwc = src / ('oracle-0.f32' if b < 48 else 'oracle.f32')
for name in ('scales','ffn','attention','head'):
    copy(base / f'post70/amd/{name}.f32', f'post70-{name}.f32')
copy(base / 'post70/oracle.f32', 'oracle-final.f32')
copy(previous_hwc, 'oracle-decoder.f32')
(out / 'provenance.json').write_text(json.dumps({
    'scope': 'same valid1080 RGB0..70 GPU package; execution pending',
    'image_input': 'input.f32 only; no intermediate feature files are runtime inputs',
    'post_base': 'host derives reflected HWC from input RGB',
    'decoder_main_and_skip_continuity_checked': True,
}, indent=2) + '\n')
print(out)
