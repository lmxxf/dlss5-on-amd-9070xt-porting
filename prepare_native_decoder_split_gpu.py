"""Export the verified original decoder40..47 chain without model corrections."""
from pathlib import Path
import json
root=Path('release/native-rgb512');out=root/'amd-decoder40-47';out.mkdir(exist_ok=False)
previous=root/'decoder39-original.output.fp8'
for block,shift in zip(range(40,48),[0,1,3,2,0,1,3,2]):
    source=root/f'decoder-block{block}';report=json.loads((source/'validation.json').read_text())
    assert report['status']=='pass' and report['shift']==shift and Path(report['input'])==previous
    assert len(report['checks'])==4 and all(c['different']==0 for c in report['checks'])
    for name in ('ffwd','ffwd-projection','attention'):
        (out/f'block{block}-{name}.f32').write_bytes((source/f'{name}.f32').read_bytes())
    previous=source/'output.fp8'
(out/'input.f32').write_bytes((root/'decoder-block40/input.f32').read_bytes())
(out/'oracle-0.f32').write_bytes((root/'decoder-block47/oracle-0.f32').read_bytes())
(out/'provenance.json').write_text(json.dumps({'input':'original decoder39','oracle':'original block47',
    'original_CPU_stage_checks':32,'scope':'isolated decoder40-47 chain; not full RGB/game'},indent=2)+'\n')
