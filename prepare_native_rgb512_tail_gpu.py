"""Export decoder40..48 coefficients for the RGB512 resident chain."""
from pathlib import Path
import json
root=Path('release/native-rgb512');out=root/'amd-tail48';out.mkdir(exist_ok=False)
for block in range(40,48):
    source=root/f'decoder-block{block}';report=json.loads((source/'validation.json').read_text())
    assert report['status']=='pass' and all(x['different']==0 for x in report['checks'])
    for name in ('ffwd','ffwd-projection','attention'):
        (out/f'block{block}-{name}.f32').write_bytes((source/f'{name}.f32').read_bytes())
assert json.loads((root/'upsample48-shift0/validation.json').read_text())['status']=='pass'
for name in ('weights','ffn','attention'):
    (out/f'block48-{name}.f32').write_bytes((root/f'upsample48-gpu/{name}.f32').read_bytes())
