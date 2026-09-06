"""Export verified original actual-size decoder67..69 GPU fixture."""
from pathlib import Path
import json
root=Path('release/native-decoder-game-c32');out=root/'amd-decoder67-69';out.mkdir(exist_ok=False)
previous=Path('release/native-upsample66/game/output.fp8')
for b,s in zip(range(67,70),(3,1,2)):
 src=root/f'decoder-block{b}';r=json.loads((src/'validation.json').read_text())
 assert r['status']=='pass' and r['different']==0 and r['values']==17694720 and r['shift']==s and Path(r['input'])==previous
 for name in ('ffn','attention'):(out/f'block{b}-{name}.f32').write_bytes((src/f'{name}.f32').read_bytes())
 previous=src/'output.fp8'
(out/'input.f32').write_bytes((root/'decoder-block67/input.f32').read_bytes())
(out/'oracle.f32').write_bytes((root/'decoder-block69/oracle.f32').read_bytes())
