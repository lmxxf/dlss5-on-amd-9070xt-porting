"""Export verified original C256 decoder49..55 chain for AMD."""
from pathlib import Path
import json,argparse
p=argparse.ArgumentParser();p.add_argument('--c128',action='store_true');p.add_argument('--game-extent',action='store_true');a=p.parse_args()
first,last,entry,count=(57,61,56,524288) if a.c128 else (49,55,48,262144)
if a.game_extent:count=240*144*128 if a.c128 else 120*72*256
root=Path(('release/native-decoder-game-c128' if a.c128 else 'release/native-decoder-game-c256') if a.game_extent else 'release/native-rgb512');out=root/f'amd-decoder{first}-{last}';out.mkdir(exist_ok=False)
previous=Path(f'release/native-upsample{entry}/game/output.fp8') if a.game_extent else root/f'upsample{entry}-shift0/output.fp8'
for block,shift in zip(range(first,last+1),([2,0,3,1,2] if a.c128 else [3,1,2,0,3,1,2]) if a.game_extent else [0,1,3,2,0,1,3]):
    source=root/f'decoder-block{block}';report=json.loads((source/'validation.json').read_text())
    assert report['status']=='pass' and report['different']==0 and report['values']==count
    assert report['shift']==shift and Path(report['input'])==previous
    for name in ('ffn','attention'):(out/f'block{block}-{name}.f32').write_bytes((source/f'{name}.f32').read_bytes())
    previous=source/'output.fp8'
(out/'input.f32').write_bytes((root/f'decoder-block{first}/input.f32').read_bytes())
(out/'oracle-0.f32').write_bytes((root/f'decoder-block{last}/oracle.f32').read_bytes())
(out/'provenance.json').write_text(json.dumps({'input':f'original block{entry}','oracle':f'original block{last}','scope':'isolated decoder chain'},indent=2)+'\n')
