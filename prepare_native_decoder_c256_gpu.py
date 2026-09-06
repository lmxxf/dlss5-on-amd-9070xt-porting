"""Export verified original C256 decoder49..55 chain for AMD."""
from pathlib import Path
import json
root=Path('release/native-rgb512');out=root/'amd-decoder49-55';out.mkdir(exist_ok=False)
previous=root/'upsample48-shift0/output.fp8'
for block,shift in zip(range(49,56),[0,1,3,2,0,1,3]):
    source=root/f'decoder-block{block}';report=json.loads((source/'validation.json').read_text())
    assert report['status']=='pass' and report['different']==0 and report['values']==262144
    assert report['shift']==shift and Path(report['input'])==previous
    for name in ('ffn','attention'):(out/f'block{block}-{name}.f32').write_bytes((source/f'{name}.f32').read_bytes())
    previous=source/'output.fp8'
(out/'input.f32').write_bytes((root/'decoder-block49/input.f32').read_bytes())
(out/'oracle-0.f32').write_bytes((root/'decoder-block55/oracle.f32').read_bytes())
(out/'provenance.json').write_text(json.dumps({'input':'original block48','oracle':'original block55','scope':'isolated C256 chain'},indent=2)+'\n')
