"""Summarize captured geometry without treating padded tensors as screen extents."""
import json,struct
from pathlib import Path
report=json.loads(Path('native-runtime-parameters-1080.json').read_text())
root=Path(report['source'])
pre=(root/'launch-0001.bin').read_bytes()
summary={'source':report['source'],'preblock_HW_at_f0':list(struct.unpack_from('<ii',pre,0xf0)),
 'encoder':[{'block':r['block'],'HW':r['HW']} for r in report['encoder'] if r['block'] in (1,5,9,15,23,30)],
 'vit_HW':report['vit'][0]['HW_pair'],
 'final_head_HW_at_20':list(struct.unpack_from('<ii',(root/'launch-0056.bin').read_bytes(),0x20)),
 'decoder':[{'block':r['block'],'HW':r['HW']} for r in report['decoder'] if r['block'] in (40,48,56,62,66,69)],
 'post_HW':report['post']['HW'],
 'scope':'captured parameter geometry, not inferred valid texel region or game pixel acceptance'}
print(json.dumps(summary,indent=2))
Path('native-1080-geometry-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
