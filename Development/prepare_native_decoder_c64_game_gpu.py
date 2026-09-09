"""Export verified original actual decoder63..65 chain for AMD."""
from pathlib import Path
import json
root=Path('release/native-decoder-game-c64');out=root/'amd-decoder63-65';out.mkdir(exist_ok=False)
previous=Path('release/native-upsample62/game/output.fp8')
for block,shift in zip(range(63,66),(3,1,2)):
 src=root/f'decoder-block{block}';r=json.loads((src/'validation.json').read_text())
 assert r['status']=='pass' and r['different']==0 and r['values']==8847360 and r['shift']==shift and Path(r['input'])==previous
 for name in ('ffn','attention'):(out/f'block{block}-{name}.f32').write_bytes((src/f'{name}.f32').read_bytes())
 previous=src/'output.fp8'
(out/'input.f32').write_bytes((root/'decoder-block63/input.f32').read_bytes())
(out/'oracle-0.f32').write_bytes((root/'decoder-block65/oracle.f32').read_bytes())
